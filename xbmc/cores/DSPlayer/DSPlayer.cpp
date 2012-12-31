/*
 *      Copyright (C) 2005-2009 Team XBMC
 *      http://www.xbmc.org
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

#ifdef HAS_DS_PLAYER

#include "DSPlayer.h"
#include "DSUtil/DSUtil.h" // unload loaded filters
#include "DSUtil/SmartPtr.h"
#include "Filters/RendererSettings.h"

#include "windowing/windows/winsystemwin32.h" //Important needed to get the right hwnd
#include "xbmc/GUIInfoManager.h"
#include "utils/SystemInfo.h"
#include "input/MouseStat.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "FileItem.h"
#include "utils/log.h"
#include "URL.h"

#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogBusy.h"
#include "windowing/WindowingFactory.h"
#include "dialogs/GUIDialogOK.h"
#include "PixelShaderList.h"
#include "guilib/LocalizeStrings.h"
#include "dialogs/GUIDialogSelect.h"
#include "video/windows/GUIWindowVideoBase.h"
#include "cores/AudioEngine/AEFactory.h"

using namespace std;

DSPLAYER_STATE CDSPlayer::PlayerState = DSPLAYER_CLOSED;
CFileItem CDSPlayer::currentFileItem;
CGUIDialogBoxBase *CDSPlayer::errorWindow = NULL;

CDSPlayer::CDSPlayer(IPlayerCallback& callback)
    : IPlayer(callback), 
	CThread("CDSPlayer thread"), 
	m_hReadyEvent(true),
    m_pGraphThread(this),
	m_bEof(false)
{
  // Change DVD Clock, time base
  CDVDClock::SetTimeBase((int64_t) DS_TIME_BASE);
  m_pClock.GetClock(); // Reset the clock

  g_dsGraph = new CDSGraph(&m_pClock, callback);
}

CDSPlayer::~CDSPlayer()
{
  CSingleLock lock(m_CleanSection);

  if (PlayerState != DSPLAYER_CLOSED)
    CloseFile();

  CDSGraph::StopThread();
  StopThread(false);
  m_pGraphThread.StopThread(false);

  SAFE_DELETE(g_dsGraph);

  // Save Shader settings
  g_dsSettings.pixelShaderList->SaveXML();

  UnloadExternalObjects();
  CLog::Log(LOGDEBUG, "%s External objects unloaded", __FUNCTION__);

  CLog::Log(LOGNOTICE, "%s DSPlayer is now closed", __FUNCTION__);

  // Restore DVD Player time base clock
  CDVDClock::SetTimeBase(DVD_TIME_BASE);
}

void CDSPlayer::ShowEditionDlg(bool playStart)
{
	UINT count = GetEditionsCount();

	if (count < 2)
		return;

	if(playStart && m_PlayerOptions.starttime > 0)
	{
		CVideoDatabase db;
		int iEdition = GetEdition();

		if (iEdition < 0 && db.Open())
		{
			CBookmark bookmark;
			if (db.GetResumeBookMark(currentFileItem.GetPath(), bookmark))
			{
				iEdition = bookmark.editionNumber;
			}
		}

		if(iEdition != -1)
		{
			CLog::Log(LOGDEBUG,"%s select bookmark, edition with idx %i selected", __FUNCTION__, iEdition);
			SetEdition(iEdition);
			return;
		}
	}


	CGUIDialogSelect *dialog = (CGUIDialogSelect *) g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);

	bool listAllTitles = false;
	UINT minLength = g_guiSettings.GetInt("dsplayer.mintitlelength");

	while (true)
	{
		std::vector<UINT> editionOptions;

		dialog->SetHeading(IsMatroskaEditions() ? 55025 : 55026);

		CLog::Log(LOGDEBUG,"%s Edition count - %i", __FUNCTION__, count);

		for (UINT i = 0; i < count; i++)
		{
			CStdString name;
			REFERENCE_TIME duration;

			GetEditionInfo(i, name, &duration);

			if (duration == _I64_MIN || listAllTitles || count == 1 || duration >= DS_TIME_BASE * 60 * minLength) 
			{
				if (name.length() == 0)
					name = "Unnamed";
				dialog->Add(name);
				editionOptions.push_back(i);
			}
		}

		if (count > 1 && count != editionOptions.size())
		{
			dialog->Add(g_localizeStrings.Get(55027));
		}

		dialog->SetSelected(GetEdition());

		dialog->DoModal();

		int selected = dialog->GetSelectedLabel();
		if (selected >= 0)
		{
			if (selected == editionOptions.size())
			{
				listAllTitles = true;
				continue;
			}
			UINT idx = editionOptions[selected];
			CLog::Log(LOGDEBUG,"%s edition with idx %i selected", __FUNCTION__, idx);
			SetEdition(idx);
			break;
		}
		break;
	}
}

bool CDSPlayer::OpenFile(const CFileItem& file,const CPlayerOptions &options)
{
	try
	{
		CLog::Log(LOGNOTICE, "%s - DSPlayer: Opening: %s", __FUNCTION__, file.GetPath().c_str());
		if(PlayerState != DSPLAYER_CLOSED)
			CloseFile();

		PlayerState = DSPLAYER_LOADING;
		currentFileItem = file;
		m_PlayerOptions = options;
		m_pGraphThread.SetCurrentRate(1);

		if (currentFileItem.IsInternetStream())
		{
			CURL url(currentFileItem.GetPath());
			url.SetProtocolOptions("");
			currentFileItem.SetPath(url.Get());
		}

		m_hReadyEvent.Reset();
		Create();

		/* Show busy dialog while SetFile() not returned */
		if(!m_hReadyEvent.WaitMSec(100))
		{
			CGUIDialogBusy* dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
			if(dialog)
			{
				dialog->Show();
				while(!m_hReadyEvent.WaitMSec(1))
					g_windowManager.ProcessRenderLoop(false);
				dialog->Close();
			}
		}

		if (PlayerState != DSPLAYER_ERROR)
		{
			if(g_guiSettings.GetBool("dsplayer.showbdtitlechoice"))
				ShowEditionDlg(true);

			// Seek
			if (m_PlayerOptions.starttime > 0)
				g_dsGraph->PostMessage( new CDSMsgPlayerSeekTime(SEC_TO_DS_TIME(m_PlayerOptions.starttime), 1U, false) , false );
			else
				g_dsGraph->PostMessage( new CDSMsgPlayerSeekTime(0, 1U, false) , false );

			// Starts playback
			g_dsGraph->PostMessage( new CDSMsgBool(CDSMsg::PLAYER_PLAY, true), false );
			if (CGraphFilters::Get()->IsDVD())
				CStreamsManager::Get()->LoadDVDStreams();
		}

		return (PlayerState != DSPLAYER_ERROR);
	}
	catch(...)
	{
		CLog::Log(LOGERROR, "%s - Exception thrown on open", __FUNCTION__);
		return false;
	}
}

bool CDSPlayer::CloseFile()
{
	CSingleLock lock(m_CleanSection);

  if (PlayerState == DSPLAYER_CLOSED || PlayerState == DSPLAYER_CLOSING)
    return true;

  PlayerState = DSPLAYER_CLOSING;

  // set the abort request so that other threads can finish up
  m_bEof = g_dsGraph->IsEof();

  g_dsGraph->CloseFile();
  
  CLog::Log(LOGDEBUG, "%s File closed", __FUNCTION__);
  
  PlayerState = DSPLAYER_CLOSED;

  // Stop threads
  CDSGraph::StopThread();
  m_pGraphThread.StopThread(false);
  StopThread(false);

  return true;
}

bool CDSPlayer::IsPlaying() const
{
  return !m_bStop;
}

bool CDSPlayer::HasVideo() const
{
  return true;
}
bool CDSPlayer::HasAudio() const
{
  return true;
}

void CDSPlayer::GetAudioInfo(CStdString& strAudioInfo)
{
  CSingleLock lock(m_StateSection);
  strAudioInfo = g_dsGraph->GetAudioInfo();
}

void CDSPlayer::GetVideoInfo(CStdString& strVideoInfo)
{
  CSingleLock lock(m_StateSection);
  strVideoInfo = g_dsGraph->GetVideoInfo();
}

void CDSPlayer::GetGeneralInfo(CStdString& strGeneralInfo)
{
  CSingleLock lock(m_StateSection);
  strGeneralInfo = g_dsGraph->GetGeneralInfo();
}

//CThread
void CDSPlayer::OnStartup()
{
	CSingleLock lock(m_CleanSection);
	try
	{
		HRESULT hr = E_FAIL;
		CLog::Log(LOGNOTICE, "%s - Creating DS Graph",  __FUNCTION__);

		CoInitializeEx(NULL, COINIT_MULTITHREADED);

		START_PERFORMANCE_COUNTER
			hr = g_dsGraph->SetFile(currentFileItem, m_PlayerOptions);
		END_PERFORMANCE_COUNTER("Loading file");
		CHECK_HR(hr);
		// Start playback
		// If there's an error, the lock must be released in order to show the error dialog
		m_hReadyEvent.Set();

		m_pGraphThread.Create();
		CLog::Log(LOGNOTICE, "%s - Successfully creating DS Graph",  __FUNCTION__);
		return;
	}
	catch(...)
	{
		CLog::Log(LOGERROR, "%s - Exception thrown when creating DS Graph", __FUNCTION__);
	}

done:
	CLog::Log(LOGERROR, "%s - Failed creating DS Graph", __FUNCTION__);
	PlayerState = DSPLAYER_ERROR;
	CThread::StopThread(false);
}

void CDSPlayer::OnExit()
{
	if (PlayerState == DSPLAYER_LOADING)
		PlayerState = DSPLAYER_ERROR;

	// In case of, set the ready event
	// Prevent a dead loop
	m_hReadyEvent.Set();

	if(m_PlayerOptions.identify == false)
	{
		if (!m_bEof || PlayerState == DSPLAYER_ERROR)
			m_callback.OnPlayBackStopped();
		else
			m_callback.OnPlayBackEnded();
	}

	m_bStop = true;
	CoUninitialize();
}

void CDSPlayer::Process()
{
	/* Suspend AE temporarily so exclusive or hog-mode sinks */
	/* don't block DSPlayer access to audio device  */
	if (!CAEFactory::Suspend())
	{
		CLog::Log(LOGNOTICE, __FUNCTION__, "Failed to suspend AudioEngine before launching DSPlayer");
	}

	// Create the messages queue
	MSG msg;
	PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

	while (!m_bStop && PlayerState != DSPLAYER_CLOSED)
		// Process thread message
		g_dsGraph->ProcessThreadMessages();

	/* Resume AE processing of XBMC native audio */
	if (!CAEFactory::Resume())
	{
		CLog::Log(LOGFATAL, __FUNCTION__, "Failed to restart AudioEngine after return from DSPlayer");
	}
}

void CDSPlayer::Stop()
{
  g_dsGraph->Stop(true);
}

void CDSPlayer::Pause()
{
  if (PlayerState == DSPLAYER_LOADING || PlayerState == DSPLAYER_LOADED)
	return;

  m_pGraphThread.SetSpeedChanged(true);
  if ( PlayerState == DSPLAYER_PAUSED )
  {
    m_pGraphThread.SetCurrentRate(1);
    m_callback.OnPlayBackResumed();    
  } 
  else
  {
    m_pGraphThread.SetCurrentRate(0);
    m_callback.OnPlayBackPaused();
  }
  g_dsGraph->Pause();
}
void CDSPlayer::ToFFRW(int iSpeed)
{
  if (iSpeed != 1)
    g_infoManager.SetDisplayAfterSeek();

  m_pGraphThread.SetCurrentRate(iSpeed);
  m_pGraphThread.SetSpeedChanged(true);
}

void CDSPlayer::Seek(bool bPlus, bool bLargeStep)
{
  CDSGraph::PostMessage( new CDSMsgPlayerSeek(bPlus, bLargeStep) );
}

void CDSPlayer::SeekPercentage(float iPercent)
{
  CDSGraph::PostMessage( new CDSMsgDouble(CDSMsg::PLAYER_SEEK_PERCENT, iPercent) );
}

bool CDSPlayer::OnAction(const CAction &action)
{
  if ( g_dsGraph->IsDvd() )
  {
    if ( action.GetID() == ACTION_SHOW_VIDEOMENU )
    {
      CDSGraph::PostMessage( new CDSMsg(CDSMsg::PLAYER_DVD_MENU_ROOT), false );
      return true;
    }
    if ( g_dsGraph->IsInMenu() )
    {
      switch (action.GetID())
      {
        case ACTION_PREVIOUS_MENU:
          CDSGraph::PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_BACK), false);
        break;
        case ACTION_MOVE_LEFT:
          CDSGraph::PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_LEFT), false);
        break;
        case ACTION_MOVE_RIGHT:
          CDSGraph::PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_RIGHT), false);
        break;
        case ACTION_MOVE_UP:
          CDSGraph::PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_UP), false);
        break;
        case ACTION_MOVE_DOWN:
          CDSGraph::PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_DOWN), false);
        break;
        /*case ACTION_MOUSE_MOVE:
        case ACTION_MOUSE_LEFT_CLICK:
        {
          CRect rs, rd;
          GetVideoRect(rs, rd);
          CPoint pt(action.GetAmount(), action.GetAmount(1));
          if (!rd.PtInRect(pt))
            return false;
          pt -= CPoint(rd.x1, rd.y1);
          pt.x *= rs.Width() / rd.Width();
          pt.y *= rs.Height() / rd.Height();
          pt += CPoint(rs.x1, rs.y1);
          if (action.GetID() == ACTION_MOUSE_LEFT_CLICK)
            SendMessage(g_hWnd, WM_COMMAND, ID_DVD_MOUSE_CLICK,MAKELPARAM(pt.x,pt.y));
          else
            SendMessage(g_hWnd, WM_COMMAND, ID_DVD_MOUSE_MOVE,MAKELPARAM(pt.x,pt.y));
          return true;
        }
        break;*/
      case ACTION_SELECT_ITEM:
        {
          // show button pushed overlay
          CDSGraph::PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_SELECT), false);
        }
        break;
      case REMOTE_0:
      case REMOTE_1:
      case REMOTE_2:
      case REMOTE_3:
      case REMOTE_4:
      case REMOTE_5:
      case REMOTE_6:
      case REMOTE_7:
      case REMOTE_8:
      case REMOTE_9:
      {
        // Offset from key codes back to button number
        // int button = action.actionId - REMOTE_0;
        //CLog::Log(LOGDEBUG, " - button pressed %d", button);
        //pStream->SelectButton(button);
      }
      break;
      default:
        return false;
        break;
      }
      return true; // message is handled
    }
  }

  switch(action.GetID())
  {
    case ACTION_NEXT_ITEM:
    case ACTION_PAGE_UP:
      if(GetChapterCount() > 0)
      {
        SeekChapter(GetChapter() + 1);
        g_infoManager.SetDisplayAfterSeek();
        return true;
      }
      else
        break;
    case ACTION_PREV_ITEM:
    case ACTION_PAGE_DOWN:
      if(GetChapterCount() > 0)
      {
        SeekChapter(GetChapter() - 1);
        g_infoManager.SetDisplayAfterSeek();
        return true;
      }
      else
        break;
  }
  
  // return false to inform the caller we didn't handle the message
  return false;
}

// Time is in millisecond
void CDSPlayer::SeekTime(__int64 iTime)
{
  int seekOffset = (int)(iTime - DS_TIME_TO_MSEC(g_dsGraph->GetTime()));
  CDSGraph::PostMessage( new CDSMsgPlayerSeekTime(MSEC_TO_DS_TIME(iTime)) );
  m_callback.OnPlayBackSeek((int) iTime, seekOffset);
}

CGraphManagementThread::CGraphManagementThread(CDSPlayer * pPlayer)
  : m_pPlayer(pPlayer), m_bSpeedChanged(false), m_bDoNotUseDSFF(false),CThread("CGraphManagementThread thread")
{
}

void CGraphManagementThread::OnStartup()
{
}

void CGraphManagementThread::Process()
{

  while (! this->m_bStop)
  {

    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
    if (m_bSpeedChanged)
    {
      m_pPlayer->GetClock().SetSpeed(m_currentRate * 1000);
      m_clockStart = m_pPlayer->GetClock().GetClock();
      m_bSpeedChanged = false;

      if (m_currentRate == 1)
        g_dsGraph->Play();
      else if (((m_currentRate < 1) || (m_bDoNotUseDSFF && (m_currentRate > 1)))
        && (!g_dsGraph->IsPaused()))
        g_dsGraph->Pause(); // Pause only if not using SetRate

      // Fast forward. DirectShow does all the hard work for us.
      if (m_currentRate >= 1)
      {
        HRESULT hr = S_OK;
        Com::SmartQIPtr<IMediaSeeking> pSeeking = g_dsGraph->pFilterGraph;
        if (pSeeking)
        {
          if (m_currentRate == 1)
            pSeeking->SetRate(m_currentRate);
          else if (!m_bDoNotUseDSFF)
          {
            HRESULT hr = pSeeking->SetRate(m_currentRate);
            if (FAILED(hr))
            {
              m_bDoNotUseDSFF = true;
              pSeeking->SetRate(1);
              m_bSpeedChanged = true;
            }
          }
        }
      }
    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
    // Handle Rewind
    if ((m_currentRate < 0) || ( m_bDoNotUseDSFF && (m_currentRate > 1)))
    {
      double clock = m_pPlayer->GetClock().GetClock() - m_clockStart; // Time elapsed since the rate change
      // Only seek if elapsed time is greater than 250 ms
      if (abs(DS_TIME_TO_MSEC(clock)) >= 250)
      {
        //CLog::Log(LOGDEBUG, "Seeking time : %f", DS_TIME_TO_MSEC(clock));

        // New position
        uint64_t newPos = g_dsGraph->GetTime() + (uint64_t) clock;
        //CLog::Log(LOGDEBUG, "New position : %f", DS_TIME_TO_SEC(newPos));

        // Check boundaries
        if (newPos <= 0)
        {
          newPos = 0;
          m_currentRate = 1;
          m_pPlayer->GetPlayerCallback().OnPlayBackSpeedChanged(1);
          m_bSpeedChanged = true;
        } else if (newPos >= g_dsGraph->GetTotalTime())
        {
          m_pPlayer->CloseFile();
          break;
        }

        g_dsGraph->Seek(newPos);

        m_clockStart = m_pPlayer->GetClock().GetClock();
      }
    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;

    // Handle rare graph event
    g_dsGraph->HandleGraphEvent();

    // Update displayed time
    g_dsGraph->UpdateTime();

    Sleep(250);
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
  }
}
#endif