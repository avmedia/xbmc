/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#if (defined HAVE_CONFIG_H) && (!defined TARGET_WINDOWS)
  #include "config.h"
#elif defined(TARGET_WINDOWS)
#include "system.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "OMXPlayerVideo.h"

#include "linux/XMemUtils.h"
#include "utils/BitstreamStats.h"

#include "DVDDemuxers/DVDDemuxUtils.h"
#include "DVDCodecs/DVDCodecUtils.h"
#include "windowing/WindowingFactory.h"
#include "DVDOverlayRenderer.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/MediaSettings.h"
#include "cores/VideoRenderers/RenderFormats.h"
#include "cores/VideoRenderers/RenderFlags.h"
#include "guilib/GraphicContext.h"

#include "OMXPlayer.h"
#include "linux/RBP.h"

using namespace RenderManager;

class COMXMsgVideoCodecChange : public CDVDMsg
{
public:
  COMXMsgVideoCodecChange(const CDVDStreamInfo &hints, COMXVideo *codec)
    : CDVDMsg(GENERAL_STREAMCHANGE)
    , m_codec(codec)
    , m_hints(hints)
  {}
 ~COMXMsgVideoCodecChange()
  {
    delete m_codec;
  }
  COMXVideo       *m_codec;
  CDVDStreamInfo  m_hints;
};

OMXPlayerVideo::OMXPlayerVideo(OMXClock *av_clock,
                               CDVDOverlayContainer* pOverlayContainer,
                               CDVDMessageQueue& parent)
: CThread("OMXPlayerVideo")
, m_messageQueue("video")
, m_codecname("")
, m_messageParent(parent)
{
  m_av_clock              = av_clock;
  m_pOverlayContainer     = pOverlayContainer;
  m_open                  = false;
  m_stream_id             = -1;
  m_fFrameRate            = 25.0f;
  m_hdmi_clock_sync       = false;
  m_speed                 = DVD_PLAYSPEED_NORMAL;
  m_stalled               = false;
  m_iSubtitleDelay        = 0;
  m_bRenderSubs           = false;
  m_flags                 = 0;
  m_bAllowFullscreen      = false;
  m_iCurrentPts           = DVD_NOPTS_VALUE;
  m_iVideoDelay           = 0;
  m_fForcedAspectRatio    = 0.0f;
  bool small_mem = g_RBP.GetArmMem() < 256;
  m_messageQueue.SetMaxDataSize((small_mem ? 10:40) * 1024 * 1024);
  m_messageQueue.SetMaxTimeSize(8.0);

  m_src_rect.SetRect(0, 0, 0, 0);
  m_dst_rect.SetRect(0, 0, 0, 0);
  m_started = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_nextOverlay = DVD_NOPTS_VALUE;
  m_flush = false;
  m_history_valid_pts = 0;
}

OMXPlayerVideo::~OMXPlayerVideo()
{
  CloseStream(false);
}

bool OMXPlayerVideo::OpenStream(CDVDStreamInfo &hints)
{
  m_hints       = hints;
  m_hdmi_clock_sync = (CSettings::Get().GetInt("videoplayer.adjustrefreshrate") != ADJUST_REFRESHRATE_OFF);
  m_started     = false;
  m_flush       = false;
  m_stalled     = m_messageQueue.GetPacketCount(CDVDMsg::DEMUXER_PACKET) == 0;
  m_nextOverlay = DVD_NOPTS_VALUE;
  // force SetVideoRect to be called initially
  m_src_rect.SetRect(0, 0, 0, 0);
  m_dst_rect.SetRect(0, 0, 0, 0);

  if (!m_DllBcmHost.Load())
    return false;

  if(!OpenDecoder())
  {
    return false;
  }

  if(m_messageQueue.IsInited())
    m_messageQueue.Put(new COMXMsgVideoCodecChange(hints, NULL), 0);
  else
  {
    if(!OpenStream(hints, NULL))
      return false;
    CLog::Log(LOGNOTICE, "Creating video thread");
    m_messageQueue.Init();
    Create();
  }

  m_open        = true;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_nextOverlay = DVD_NOPTS_VALUE;

  return true;
}

bool OMXPlayerVideo::OpenStream(CDVDStreamInfo &hints, COMXVideo *codec)
{
  return true;
}

bool OMXPlayerVideo::CloseStream(bool bWaitForBuffers)
{
  // wait until buffers are empty
  if (bWaitForBuffers && m_speed > 0) m_messageQueue.WaitUntilEmpty();

  m_messageQueue.Abort();

  if(IsRunning())
    StopThread();

  m_messageQueue.End();

  m_open          = false;
  m_stream_id     = -1;
  m_speed         = DVD_PLAYSPEED_NORMAL;
  m_started       = false;

  m_omxVideo.Close();

  if(m_DllBcmHost.IsLoaded())
    m_DllBcmHost.Unload();

  return true;
}

void OMXPlayerVideo::OnStartup()
{
}

void OMXPlayerVideo::OnExit()
{
  CLog::Log(LOGNOTICE, "thread end: video_thread");
}

double OMXPlayerVideo::NextOverlay(double pts)
{
  double delta_start, delta_stop, min_delta = DVD_NOPTS_VALUE;

  CSingleLock lock(*m_pOverlayContainer);
  VecOverlays* pVecOverlays = m_pOverlayContainer->GetOverlays();
  VecOverlaysIter it = pVecOverlays->begin();

  //Find the minimum time before a subtitle is added or removed
  while (it != pVecOverlays->end())
  {
    CDVDOverlay* pOverlay = *it++;
    if(!pOverlay->bForced && !m_bRenderSubs)
      continue;

    double pts2 = pOverlay->bForced ? pts : pts - m_iSubtitleDelay;

    delta_start = pOverlay->iPTSStartTime - pts2;
    delta_stop = pOverlay->iPTSStopTime - pts2;

    // when currently on screen, we periodically update to allow (limited rate) ASS animation
    if (delta_start <= 0.0 && delta_stop > 0.0 && (min_delta == DVD_NOPTS_VALUE || DVD_MSEC_TO_TIME(100) < min_delta))
      min_delta = DVD_MSEC_TO_TIME(100);

    else if (delta_start > 0.0 && (min_delta == DVD_NOPTS_VALUE || delta_start < min_delta))
      min_delta = delta_start;

    else if (delta_stop > 0.0 && (min_delta == DVD_NOPTS_VALUE || delta_stop < min_delta))
      min_delta = delta_stop;
  }
  return min_delta == DVD_NOPTS_VALUE ? pts+DVD_MSEC_TO_TIME(500) : pts+min_delta;
}


void OMXPlayerVideo::ProcessOverlays(double pts)
{
  // remove any overlays that are out of time
  if (m_started)
    m_pOverlayContainer->CleanUp(pts - m_iSubtitleDelay);

  VecOverlays overlays;

  CSingleLock lock(*m_pOverlayContainer);

  VecOverlays* pVecOverlays = m_pOverlayContainer->GetOverlays();
  VecOverlaysIter it = pVecOverlays->begin();

  //Check all overlays and render those that should be rendered, based on time and forced
  //Both forced and subs should check timing
  while (it != pVecOverlays->end())
  {
    CDVDOverlay* pOverlay = *it++;
    if(!pOverlay->bForced && !m_bRenderSubs)
      continue;

    double pts2 = pOverlay->bForced ? pts : pts - m_iSubtitleDelay;

    if((pOverlay->iPTSStartTime <= pts2 && (pOverlay->iPTSStopTime > pts2 || pOverlay->iPTSStopTime == 0LL)))
    {
      if(pOverlay->IsOverlayType(DVDOVERLAY_TYPE_GROUP))
        overlays.insert(overlays.end(), static_cast<CDVDOverlayGroup*>(pOverlay)->m_overlays.begin()
                                      , static_cast<CDVDOverlayGroup*>(pOverlay)->m_overlays.end());
      else
        overlays.push_back(pOverlay);
    }
  }

  for(it = overlays.begin(); it != overlays.end(); ++it)
  {
    double pts2 = (*it)->bForced ? pts : pts - m_iSubtitleDelay;
    g_renderManager.AddOverlay(*it, pts2);
  }
}

std::string OMXPlayerVideo::GetStereoMode()
{
  std::string  stereo_mode;

  switch(CMediaSettings::Get().GetCurrentVideoSettings().m_StereoMode)
  {
    case RENDER_STEREO_MODE_SPLIT_VERTICAL:   stereo_mode = "left_right"; break;
    case RENDER_STEREO_MODE_SPLIT_HORIZONTAL: stereo_mode = "top_bottom"; break;
    default:                                  stereo_mode = m_hints.stereo_mode; break;
  }

  if(CMediaSettings::Get().GetCurrentVideoSettings().m_StereoInvert)
    stereo_mode = GetStereoModeInvert(stereo_mode);
  return stereo_mode;
}

void OMXPlayerVideo::Output(double pts, bool bDropPacket)
{
  if (!g_renderManager.IsStarted()) {
    CLog::Log(LOGINFO, "%s - renderer not started", __FUNCTION__);
    return;
  }

  if (CThread::m_bStop)
    return;

  // we aim to submit subtitles 100ms early
  const double preroll = DVD_MSEC_TO_TIME(100);
  double media_pts = m_av_clock->OMXMediaTime();

  if (m_nextOverlay != DVD_NOPTS_VALUE && media_pts + preroll <= m_nextOverlay)
    return;

  int buffer = g_renderManager.WaitForBuffer(CThread::m_bStop);
  if (buffer < 0)
    return;

  double subtitle_pts = m_nextOverlay;
  double time = subtitle_pts != DVD_NOPTS_VALUE ? subtitle_pts - media_pts : 0.0;

  m_nextOverlay = NextOverlay(media_pts);

  ProcessOverlays(media_pts);

  time += m_av_clock->GetAbsoluteClock();
  g_renderManager.FlipPage(CThread::m_bStop, time/DVD_TIME_BASE);
}

static unsigned count_bits(int32_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

void OMXPlayerVideo::Process()
{
  double frametime = (double)DVD_TIME_BASE / m_fFrameRate;
  bool bRequestDrop = false;

  m_videoStats.Start();

  while(!m_bStop)
  {
    CDVDMsg* pMsg;
    int iQueueTimeOut = (int)(m_stalled ? frametime / 4 : frametime * 10) / 1000;
    int iPriority = (m_speed == DVD_PLAYSPEED_PAUSE && m_started) ? 1 : 0;
    MsgQueueReturnCode ret = m_messageQueue.Get(&pMsg, iQueueTimeOut, iPriority);

    if (MSGQ_IS_ERROR(ret) || ret == MSGQ_ABORT)
    {
      CLog::Log(LOGERROR, "OMXPlayerVideo: Got MSGQ_IS_ERROR(%d) Aborting", (int)ret);
      break;
    }
    else if (ret == MSGQ_TIMEOUT)
    {
      continue;
    }

    if (pMsg->IsType(CDVDMsg::GENERAL_SYNCHRONIZE))
    {
      if(((CDVDMsgGeneralSynchronize*)pMsg)->Wait(100, SYNCSOURCE_VIDEO))
      {
        CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::GENERAL_SYNCHRONIZE");

      }
      else
        m_messageQueue.Put(pMsg->Acquire(), 1); /* push back as prio message, to process other prio messages */

      pMsg->Release();

      continue;
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_RESYNC))
    {
      CDVDMsgGeneralResync* pMsgGeneralResync = (CDVDMsgGeneralResync*)pMsg;

      double delay = 0;

      if(pMsgGeneralResync->m_clock && pMsgGeneralResync->m_timestamp != DVD_NOPTS_VALUE)
      {
        CLog::Log(LOGDEBUG, "CDVDPlayerVideo - CDVDMsg::GENERAL_RESYNC(%f, %f, 1)", m_iCurrentPts, pMsgGeneralResync->m_timestamp);
        m_av_clock->Discontinuity(pMsgGeneralResync->m_timestamp - delay);
      }
      else
        CLog::Log(LOGDEBUG, "CDVDPlayerVideo - CDVDMsg::GENERAL_RESYNC(%f, 0)", m_iCurrentPts);

      m_nextOverlay = DVD_NOPTS_VALUE;
      m_iCurrentPts = DVD_NOPTS_VALUE;
      pMsgGeneralResync->Release();
      continue;
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_DELAY))
    {
      double timeout = static_cast<CDVDMsgDouble*>(pMsg)->m_value;
      CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::GENERAL_DELAY(%f)", timeout);
    }
    else if (pMsg->IsType(CDVDMsg::VIDEO_SET_ASPECT))
    {
      m_fForcedAspectRatio = *((CDVDMsgDouble*)pMsg);
      CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::VIDEO_SET_ASPECT %.2f", m_fForcedAspectRatio);
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_RESET))
    {
      CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::GENERAL_RESET");
      m_omxVideo.Reset();
      m_started = false;
      m_nextOverlay = DVD_NOPTS_VALUE;
      m_iCurrentPts = DVD_NOPTS_VALUE;
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_FLUSH)) // private message sent by (COMXPlayerVideo::Flush())
    {
      CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::GENERAL_FLUSH");
      m_stalled = true;
      m_started = false;
      m_nextOverlay = DVD_NOPTS_VALUE;
      m_iCurrentPts = DVD_NOPTS_VALUE;
      m_omxVideo.Reset();
      m_flush = false;
    }
    else if (pMsg->IsType(CDVDMsg::PLAYER_SETSPEED))
    {
      if (m_speed != static_cast<CDVDMsgInt*>(pMsg)->m_value)
      {
        m_speed = static_cast<CDVDMsgInt*>(pMsg)->m_value;
        CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::PLAYER_SETSPEED %d", m_speed);
      }
    }
    else if (pMsg->IsType(CDVDMsg::PLAYER_STARTED))
    {
      CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::PLAYER_STARTED %d", m_started);
      if(m_started)
        m_messageParent.Put(new CDVDMsgInt(CDVDMsg::PLAYER_STARTED, DVDPLAYER_VIDEO));
    }
    else if (pMsg->IsType(CDVDMsg::PLAYER_DISPLAYTIME))
    {
      COMXPlayer::SPlayerState& state = ((CDVDMsgType<COMXPlayer::SPlayerState>*)pMsg)->m_value;
      double pts = m_iCurrentPts;
      double stamp = m_av_clock->OMXMediaTime();

      if(state.time_src == COMXPlayer::ETIMESOURCE_CLOCK)
        state.time      = stamp == 0.0 ? state.time : DVD_TIME_TO_MSEC(stamp + state.time_offset);
      else
        state.time      = stamp == 0.0 || pts == DVD_NOPTS_VALUE ? state.time : state.time + DVD_TIME_TO_MSEC(stamp - pts);
      state.timestamp = m_av_clock->GetAbsoluteClock();
      state.player    = DVDPLAYER_VIDEO;
      m_messageParent.Put(pMsg->Acquire());
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_STREAMCHANGE))
    {
      COMXMsgVideoCodecChange* msg(static_cast<COMXMsgVideoCodecChange*>(pMsg));
      OpenStream(msg->m_hints, msg->m_codec);
      msg->m_codec = NULL;
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_EOF))
    {
      CLog::Log(LOGDEBUG, "COMXPlayerVideo - CDVDMsg::GENERAL_EOF");
      SubmitEOS();
    }
    else if (pMsg->IsType(CDVDMsg::DEMUXER_PACKET))
    {
      DemuxPacket* pPacket = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacket();
      bool bPacketDrop     = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacketDrop();

      #ifdef _DEBUG
      CLog::Log(LOGINFO, "Video: dts:%.0f pts:%.0f size:%d (s:%d f:%d d:%d l:%d) s:%d %d/%d late:%d\n", pPacket->dts, pPacket->pts, 
          (int)pPacket->iSize, m_started, m_flush, bPacketDrop, m_stalled, m_speed, 0, 0, 0);
      #endif
      if (m_messageQueue.GetDataSize() == 0
      ||  m_speed < 0)
      {
        bRequestDrop = false;
      }

      // if player want's us to drop this packet, do so nomatter what
      if(bPacketDrop)
        bRequestDrop = true;

      m_omxVideo.SetDropState(bRequestDrop);

      while (!m_bStop)
      {
        // discard if flushing as clocks may be stopped and we'll never submit it
        if (m_flush)
           break;

        if((int)m_omxVideo.GetFreeSpace() < pPacket->iSize)
        {
          Sleep(10);
          continue;
        }
  
        if (m_stalled)
        {
          CLog::Log(LOGINFO, "COMXPlayerVideo - Stillframe left, switching to normal playback");
          m_stalled = false;
        }

        // some packed bitstream AVI files set almost all pts values to DVD_NOPTS_VALUE, but have a scattering of real pts values.
        // the valid pts values match the dts values.
        // if a stream has had more than 4 valid pts values in the last 16, the use UNKNOWN, otherwise use dts
        m_history_valid_pts = (m_history_valid_pts << 1) | (pPacket->pts != DVD_NOPTS_VALUE);
        double pts = pPacket->pts;
        if(pPacket->pts == DVD_NOPTS_VALUE && count_bits(m_history_valid_pts & 0xffff) < 4)
          pts = pPacket->dts;

        if (pts != DVD_NOPTS_VALUE)
          pts += m_iVideoDelay;

        m_omxVideo.Decode(pPacket->pData, pPacket->iSize, pts);
        Output(pts, bRequestDrop);
        if(pts != DVD_NOPTS_VALUE)
          m_iCurrentPts = pts;

        if(m_started == false)
        {
          m_codecname = m_omxVideo.GetDecoderName();
          m_started = true;
          m_messageParent.Put(new CDVDMsgInt(CDVDMsg::PLAYER_STARTED, DVDPLAYER_VIDEO));
        }

        break;
      }

      bRequestDrop = false;

      m_videoStats.AddSampleBytes(pPacket->iSize);
    }
    pMsg->Release();

  }
}

void OMXPlayerVideo::Flush()
{
  m_flush = true;
  m_messageQueue.Flush();
  m_messageQueue.Put(new CDVDMsg(CDVDMsg::GENERAL_FLUSH), 1);
}

bool OMXPlayerVideo::OpenDecoder()
{
  if(!m_av_clock)
    return false;

  if (m_hints.fpsrate && m_hints.fpsscale)
    m_fFrameRate = DVD_TIME_BASE / OMXClock::NormalizeFrameduration((double)DVD_TIME_BASE * m_hints.fpsscale / m_hints.fpsrate);
  else
    m_fFrameRate = 25;

  if( m_fFrameRate > 100 || m_fFrameRate < 5 )
  {
    CLog::Log(LOGINFO, "OMXPlayerVideo::OpenDecoder : Invalid framerate %d, using forced 25fps and just trust timestamps\n", (int)m_fFrameRate);
    m_fFrameRate = 25;
  }
  // use aspect in stream if available
  if (m_hints.forced_aspect)
    m_fForcedAspectRatio = m_hints.aspect;
  else
    m_fForcedAspectRatio = 0.0;

  bool bVideoDecoderOpen = m_omxVideo.Open(m_hints, m_av_clock, CMediaSettings::Get().GetCurrentVideoSettings().m_DeinterlaceMode, m_hdmi_clock_sync);
  m_omxVideo.RegisterResolutionUpdateCallBack((void *)this, ResolutionUpdateCallBack);

  if(!bVideoDecoderOpen)
  {
    CLog::Log(LOGERROR, "OMXPlayerVideo : Error open video output");
    m_omxVideo.Close();
  }
  else
  {
    CLog::Log(LOGINFO, "OMXPlayerVideo::OpenDecoder : Video codec %s width %d height %d profile %d fps %f\n",
        m_omxVideo.GetDecoderName().c_str() , m_hints.width, m_hints.height, m_hints.profile, m_fFrameRate);

    m_codecname = m_omxVideo.GetDecoderName();

    // if we are closer to ntsc version of framerate, let gpu know
    int   iFrameRate  = (int)(m_fFrameRate + 0.5f);
    bool  bNtscFreq  = fabs(m_fFrameRate * 1001.0f / 1000.0f - iFrameRate) < fabs(m_fFrameRate - iFrameRate);
    char  response[80], command[80];
    sprintf(command, "hdmi_ntsc_freqs %d", bNtscFreq);
    CLog::Log(LOGINFO, "OMXPlayerVideo::OpenDecoder fps: %f %s\n", m_fFrameRate, command);
    m_DllBcmHost.vc_gencmd(response, sizeof response, command);

    m_av_clock->SetRefreshRate(m_fFrameRate);
  }

  // start from assuming all recent frames had valid pts
  m_history_valid_pts = ~0;

  return bVideoDecoderOpen;
}

int  OMXPlayerVideo::GetDecoderBufferSize()
{
  return m_omxVideo.GetInputBufferSize();
}

int  OMXPlayerVideo::GetDecoderFreeSpace()
{
  return m_omxVideo.GetFreeSpace();
}

void OMXPlayerVideo::SubmitEOS()
{
  m_omxVideo.SubmitEOS();
}

bool OMXPlayerVideo::SubmittedEOS()
{
  return m_omxVideo.SubmittedEOS();
}

bool OMXPlayerVideo::IsEOS()
{
  return m_omxVideo.IsEOS();
}

void OMXPlayerVideo::SetSpeed(int speed)
{
  if(m_messageQueue.IsInited())
    m_messageQueue.Put( new CDVDMsgInt(CDVDMsg::PLAYER_SETSPEED, speed), 1 );
  else
    m_speed = speed;
}

std::string OMXPlayerVideo::GetPlayerInfo()
{
  std::ostringstream s;
  s << "fr:"     << fixed << setprecision(3) << m_fFrameRate;
  s << ", vq:"   << setw(2) << min(99,GetLevel()) << "%";
  s << ", dc:"   << m_codecname;
  s << ", Mb/s:" << fixed << setprecision(2) << (double)GetVideoBitrate() / (1024.0*1024.0);

  return s.str();
}

int OMXPlayerVideo::GetVideoBitrate()
{
  return (int)m_videoStats.GetBitrate();
}

double OMXPlayerVideo::GetOutputDelay()
{
  double time = m_messageQueue.GetPacketCount(CDVDMsg::DEMUXER_PACKET);
  if( m_fFrameRate )
    time = (time * DVD_TIME_BASE) / m_fFrameRate;
  else
    time = 0.0;

  if( m_speed != 0 )
    time = time * DVD_PLAYSPEED_NORMAL / abs(m_speed);

  return time;
}

int OMXPlayerVideo::GetFreeSpace()
{
  return m_omxVideo.GetFreeSpace();
}

void OMXPlayerVideo::SetVideoRect(const CRect &InSrcRect, const CRect &InDestRect)
{
  CRect SrcRect = InSrcRect, DestRect = InDestRect;

  // in 3d modes skip this - we get called as the gui switches from left eye to right eye
  unsigned flags = GetStereoModeFlags(GetStereoMode());

  if (CONF_FLAGS_STEREO_MODE_MASK(flags))
  {
    if (g_graphicsContext.GetStereoMode() == RENDER_STEREO_MODE_MONO)
    {
       if (GetStereoMode() == "left_right")
         SrcRect.SetRect(0, 0, m_hints.width>>1, m_hints.height);
       else if (GetStereoMode() == "right_left")
         SrcRect.SetRect(m_hints.width>>1, 0, m_hints.width, m_hints.height);
       else if (GetStereoMode() == "top_bottom")
         SrcRect.SetRect(0, 0, m_hints.width, m_hints.height>>1);
       else if (GetStereoMode() == "bottom_top")
         SrcRect.SetRect(0, m_hints.height>>1, m_hints.width, m_hints.height);
    }
    else
      SrcRect.SetRect(0, 0, m_hints.width, m_hints.height);
    // interpreted as fullscreen
    DestRect.SetRect(0, 0, 0, 0);
  }

  // check if destination rect or video view mode has changed
  if (m_dst_rect != DestRect || m_src_rect != SrcRect)
  {
    m_src_rect  = SrcRect;
    m_dst_rect  = DestRect;
  }
  else
  {
    return;
  }

  // might need to scale up m_dst_rect to display size as video decodes
  // to separate video plane that is at display size.
  RESOLUTION res = g_graphicsContext.GetVideoResolution();
  CRect gui(0, 0, CDisplaySettings::Get().GetResolutionInfo(res).iWidth, CDisplaySettings::Get().GetResolutionInfo(res).iHeight);
  CRect display(0, 0, CDisplaySettings::Get().GetResolutionInfo(res).iScreenWidth, CDisplaySettings::Get().GetResolutionInfo(res).iScreenHeight);
  CRect dst_rect(m_dst_rect);

  if (gui != display)
  {
    float xscale = display.Width()  / gui.Width();
    float yscale = display.Height() / gui.Height();
    dst_rect.x1 *= xscale;
    dst_rect.x2 *= xscale;
    dst_rect.y1 *= yscale;
    dst_rect.y2 *= yscale;
  }
  m_omxVideo.SetVideoRect(SrcRect, dst_rect);
}

void OMXPlayerVideo::RenderUpdateCallBack(const void *ctx, const CRect &SrcRect, const CRect &DestRect)
{
  OMXPlayerVideo *player = (OMXPlayerVideo*)ctx;
  player->SetVideoRect(SrcRect, DestRect);
}

void OMXPlayerVideo::ResolutionUpdateCallBack(uint32_t width, uint32_t height, float display_aspect)
{
  RESOLUTION res  = g_graphicsContext.GetVideoResolution();
  uint32_t video_width   = CDisplaySettings::Get().GetResolutionInfo(res).iScreenWidth;
  uint32_t video_height  = CDisplaySettings::Get().GetResolutionInfo(res).iScreenHeight;

  unsigned flags = 0;
  ERenderFormat format = RENDER_FMT_BYPASS;

  if(m_bAllowFullscreen)
  {
    flags |= CONF_FLAGS_FULLSCREEN;
    m_bAllowFullscreen = false; // only allow on first configure
  }

  flags |= GetStereoModeFlags(GetStereoMode());

  if(flags & CONF_FLAGS_STEREO_MODE_SBS)
  {
    if(g_Windowing.Support3D(video_width, video_height, D3DPRESENTFLAG_MODE3DSBS))
      CLog::Log(LOGNOTICE, "3DSBS movie found");
    else
    {
      flags &= ~CONF_FLAGS_STEREO_MODE_MASK(~0);
      CLog::Log(LOGNOTICE, "3DSBS movie found but not supported");
    }
  }
  else if(flags & CONF_FLAGS_STEREO_MODE_TAB)
  {
    if(g_Windowing.Support3D(video_width, video_height, D3DPRESENTFLAG_MODE3DTB))
      CLog::Log(LOGNOTICE, "3DTB movie found");
    else
    {
      flags &= ~CONF_FLAGS_STEREO_MODE_MASK(~0);
      CLog::Log(LOGNOTICE, "3DTB movie found but not supported");
    }
  }
  else
    CLog::Log(LOGNOTICE, "not a 3D movie");

  unsigned int iDisplayWidth  = width;
  unsigned int iDisplayHeight = height;

  /* use forced aspect if any */
  if( m_fForcedAspectRatio != 0.0f )
    iDisplayWidth = (int) (iDisplayHeight * m_fForcedAspectRatio);
  else if( display_aspect != 0.0f )
    iDisplayWidth = (int) (iDisplayHeight * display_aspect);

  CLog::Log(LOGDEBUG,"%s - change configuration. video:%dx%d. framerate: %4.2f. %dx%d format: BYPASS",
      __FUNCTION__, video_width, video_height, m_fFrameRate, iDisplayWidth, iDisplayHeight);

  if(!g_renderManager.Configure(width, height,
        iDisplayWidth, iDisplayHeight, m_fFrameRate, flags, format, 0,
        m_hints.orientation, 0))
  {
    CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
    return;
  }

  g_renderManager.RegisterRenderUpdateCallBack((const void*)this, RenderUpdateCallBack);
}

void OMXPlayerVideo::ResolutionUpdateCallBack(void *ctx, uint32_t width, uint32_t height, float display_aspect)
{
  OMXPlayerVideo *player = static_cast<OMXPlayerVideo*>(ctx);
  player->ResolutionUpdateCallBack(width, height, display_aspect);
}

