#pragma once

/*
*      Copyright (C) 2005-2009 Team XBMC
*      http://www.xbmc.org
*
*	   Copyright (C) 2010-2013 Eduard Kytmanov
*	   http://www.avmedia.su
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
*  along with XBMC; see the file COPYING.  If not, write to
*  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*  http://www.gnu.org/copyleft/gpl.html
*
*/

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include "cores/IPlayer.h"
#include "threads/Thread.h"
#include "threads/SingleLock.h"
#include "utils/StdString.h"
#include "DSGraph.h"
#include "DVDClock.h"
#include "url.h"
#include "StreamsManager.h"
#include "ChaptersManager.h"

#include "utils/TimeUtils.h"
#include "Event.h"
#include "dialogs/GUIDialogBoxBase.h"
#include "settings/Settings.h"

#include "filesystem/PVRFile.h"
#include "pvr/PVRManager.h"

#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif

#define START_PERFORMANCE_COUNTER { int64_t start = CurrentHostCounter();
#define END_PERFORMANCE_COUNTER(fn) int64_t end = CurrentHostCounter(); \
  CLog::Log(LOGINFO, "%s %s. Elapsed time: %.2fms", __FUNCTION__, fn, 1000.f * (end - start) / CurrentHostFrequency()); }

enum DSPLAYER_STATE
{
  DSPLAYER_LOADING,
  DSPLAYER_LOADED,
  DSPLAYER_PLAYING,
  DSPLAYER_PAUSED,
  DSPLAYER_STOPPED,
  DSPLAYER_CLOSING,
  DSPLAYER_CLOSED,
  DSPLAYER_ERROR
};


class CDSInputStreamPVRManager;
class CDSPlayer;
class CGraphManagementThread : public CThread
{
private:
  bool          m_bSpeedChanged;
  double        m_clockStart;
  double        m_currentRate;
  CDSPlayer*    m_pPlayer;
public:
  CGraphManagementThread(CDSPlayer * pPlayer);

  void SetSpeedChanged(bool value) { m_bSpeedChanged = value; }
  void SetCurrentRate(double rate) { m_currentRate = rate; }
  double GetCurrentRate() const { return m_currentRate; }
protected:
  void OnStartup();
  void Process();
};

class CDSPlayer : public IPlayer, public CThread
{
public:
//IPlayer
  CDSPlayer(IPlayerCallback& callback);
  virtual ~CDSPlayer();
  virtual bool OpenFile(const CFileItem& file, const CPlayerOptions &options);
  virtual bool CloseFile();
  virtual bool IsPlaying() const;
  virtual bool IsCaching() const { return false; };
  virtual bool IsPaused() const { return (m_pGraphThread.GetCurrentRate() == 0) && g_dsGraph->IsPaused(); };
  virtual bool HasVideo() const;
  virtual bool HasAudio() const;
  virtual bool HasMenu() { return g_dsGraph->IsDvd(); };
  bool IsInMenu() const { return g_dsGraph->IsInMenu(); };
  virtual void Pause();
  virtual bool CanSeek()                                        { return g_dsGraph->CanSeek(); }
  virtual void Seek(bool bPlus, bool bLargeStep);
  virtual void SeekPercentage(float iPercent);
  virtual float GetPercentage()                                 { return g_dsGraph->GetPercentage(); }
  virtual float GetCachePercentage()							{ return g_dsGraph->GetCachePercentage(); }
  virtual bool ControlsVolume()									{ return true;}
  virtual void SetMute(bool bOnOff)								{ if(bOnOff) g_dsGraph->SetVolume(VOLUME_MINIMUM);}
  virtual void SetVolume(float nVolume)                         { g_dsGraph->SetVolume(nVolume); }
  virtual void GetAudioInfo(CStdString& strAudioInfo);
  virtual void GetVideoInfo(CStdString& strVideoInfo);
  virtual void GetGeneralInfo(CStdString& strGeneralInfo);
  virtual bool Closing()                                      { return PlayerState == DSPLAYER_CLOSING; }

//Audio stream selection
  virtual int  GetAudioStreamCount() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioStreamCount() : 0; }
  virtual int  GetAudioStream() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioStream() : 0; }
  virtual void GetAudioStreamName(int iStream, CStdString &strStreamName) {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->GetAudioStreamName(iStream,strStreamName);
  };
  virtual void SetAudioStream(int iStream) {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SetAudioStream(iStream);
  };

  //Editions selection
  virtual int  GetEditionsCount() 
  { 
	  return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetEditionsCount() : 0; 
  }
  virtual int  GetEdition() 
  { 
	  return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetEdition() : 0; 
  }
  virtual void GetEditionInfo(int iEdition, CStdString &strEditionName, REFERENCE_TIME *prt) {
	  if (CStreamsManager::Get())
		  CStreamsManager::Get()->GetEditionInfo(iEdition, strEditionName, prt);
  };
  virtual void SetEdition(int iEdition) {
	  if (CStreamsManager::Get())
		  CStreamsManager::Get()->SetEdition(iEdition);
  };

  virtual bool IsMatroskaEditions()
  {
	  return (CStreamsManager::Get()) ? CStreamsManager::Get()->IsMatroskaEditions() : false; 
  }

  virtual int  GetSubtitleCount() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitleCount() : 0; }
  virtual int  GetSubtitle() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitle() : 0; }
  virtual void GetSubtitleName(int iStream, CStdString &strStreamName) {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SubtitleManager->GetSubtitleName(iStream, strStreamName);
  }
  virtual void SetSubtitle(int iStream) {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SubtitleManager->SetSubtitle(iStream);
  }
  virtual bool GetSubtitleVisible() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitleVisible() : true; }
  virtual void SetSubtitleVisible( bool bVisible ) {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SubtitleManager->SetSubtitleVisible(bVisible);
  }

  virtual int AddSubtitle(const CStdString& strSubPath) { return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->AddSubtitle(strSubPath) : -1; };
  virtual void SetSubTitleDelay(float fValue = 0.0f) {
    if (CStreamsManager::Get())
      CStreamsManager::Get()->SubtitleManager->SetSubtitleDelay(fValue);
  };
  virtual float GetSubTileDelay(void) { return (CStreamsManager::Get()) ? CStreamsManager::Get()->SubtitleManager->GetSubtitleDelay() : 0; }
  // Chapters

  virtual int  GetChapterCount() { CSingleLock lock(m_StateSection); return CChaptersManager::Get()->GetChapterCount(); }
  virtual int  GetChapter() { CSingleLock lock(m_StateSection); return CChaptersManager::Get()->GetChapter(); }
  virtual void GetChapterName(CStdString& strChapterName) { CSingleLock lock(m_StateSection); CChaptersManager::Get()->GetChapterName(strChapterName); }
  virtual int  SeekChapter(int iChapter) { return CChaptersManager::Get()->SeekChapter(iChapter); }

  void Update(bool bPauseDrawing) { g_renderManager.Update(bPauseDrawing); }
  void GetVideoRect(CRect& SrcRect, CRect& DestRect) { g_renderManager.GetVideoRect(SrcRect, DestRect); }
  virtual void GetVideoAspectRatio(float& fAR) { fAR = g_renderManager.GetAspectRatio(); }

  virtual int GetChannels() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetChannels() : 0; };
  virtual int GetBitsPerSample() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetBitsPerSample() : 0; };
  virtual int GetSampleRate() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSampleRate() : 0; };
  virtual int GetPictureWidth() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetPictureWidth() : 0; }
  virtual int GetPictureHeight()  { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetPictureHeight() : 0; }
  virtual CStdString GetAudioCodecName() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioCodecName() : ""; }
  virtual CStdString GetVideoCodecName() { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetVideoCodecName() : ""; }

  virtual void SeekTime(__int64 iTime = 0);
  virtual __int64 GetTime() { CSingleLock lock(m_StateSection); return llrint(DS_TIME_TO_MSEC(g_dsGraph->GetTime())); }
  virtual __int64 GetTotalTime() { CSingleLock lock(m_StateSection); return llrint(DS_TIME_TO_MSEC(g_dsGraph->GetTotalTime())); }
  virtual void ToFFRW(int iSpeed);
  virtual bool OnAction(const CAction &action);

  virtual bool SwitchChannel(const PVR::CPVRChannel &channel);
  virtual bool CachePVRStream(void) const;
  
  //CDSPlayer
  virtual void Stop();
  CDVDClock&  GetClock() { return m_pClock; }
  IPlayerCallback& GetPlayerCallback() { return m_callback; }

  static DSPLAYER_STATE PlayerState;
  static CFileItem currentFileItem;
  static CGUIDialogBoxBase* errorWindow;

  CCriticalSection m_StateSection;
  CCriticalSection m_CleanSection;

  void ShowEditionDlg(bool playStart);
  bool OpenFileInternal(const CFileItem& file);

  static void PostMessage(CDSMsg *msg, bool wait = true)
  {
	  if (! m_threadID)
	  {
		  msg->Release();
		  return;
	  }

	  if (wait)
		  msg->Acquire();

	  CLog::Log(LOGDEBUG, "%s Message posted : %d on thread 0x%X", __FUNCTION__, msg->GetMessageType(), m_threadID);
	  PostThreadMessage(m_threadID, WM_GRAPHMESSAGE, msg->GetMessageType(), (LPARAM) msg);

	  if (wait)
	  {
		  msg->Wait();
		  msg->Release();
	  }
  }

  static bool IsCurrentThread() {return CThread::IsCurrentThread(m_threadID); } 

  protected:

	  void StopThread(bool bWait = true)
	  {
		  if (m_threadID)
		  {
			  PostThreadMessage(m_threadID, WM_QUIT, 0, 0);
			  m_threadID = 0;
		  }
		  CThread::StopThread(bWait);
	  }

	  void HandleMessages();

	  bool ShowPVRChannelInfo();

	  CGraphManagementThread m_pGraphThread;
	  CDVDClock m_pClock;
	  CPlayerOptions m_PlayerOptions;
	  CEvent m_hReadyEvent;
	  static ThreadIdentifier m_threadID;
	  bool m_bEof;

	  
	  bool SelectChannel(bool bNext);
	  bool SwitchChannel(unsigned int iChannelNumber);


	  // CThread
	  virtual void OnStartup();
	  virtual void OnExit();
	  virtual void Process();

};

