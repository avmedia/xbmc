/*
 *      Copyright (C) 2005-2009 Team XBMC
 *      http://www.xbmc.org
 *
 *		Copyright (C) 2010-2013 Eduard Kytmanov
 *		http://www.avmedia.su
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
#include "ApplicationMessenger.h"
#include "DSInputStreamPVRManager.h"
#include "pvr/PVRManager.h"
#include "pvr/windows/GUIWindowPVR.h"
#include "pvr/channels/PVRChannel.h"
#include "settings/AdvancedSettings.h"
#include "Application.h"
#include "GUIUserMessages.h"

using namespace PVR;
using namespace std;

DSPLAYER_STATE CDSPlayer::PlayerState = DSPLAYER_CLOSED;
CFileItem CDSPlayer::currentFileItem;
CGUIDialogBoxBase *CDSPlayer::errorWindow = NULL;
ThreadIdentifier CDSPlayer::m_threadID = 0;

CDSPlayer::CDSPlayer(IPlayerCallback& callback)
    : IPlayer(callback), 
	CThread("CDSPlayer thread"), 
	m_hReadyEvent(true),
    m_pGraphThread(this),
	m_bEof(false)
{
	/* Suspend AE temporarily so exclusive or hog-mode sinks */
	/* don't block DSPlayer access to audio device  */
	if (!CAEFactory::Suspend())
	{
		CLog::Log(LOGNOTICE, __FUNCTION__, "Failed to suspend AudioEngine before launching DSPlayer");
	}

	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	m_pClock.GetClock(); // Reset the clock
	g_dsGraph = new CDSGraph(&m_pClock, callback);

	// Change DVD Clock, time base
	CDVDClock::SetTimeBase((int64_t) DS_TIME_BASE);
}

CDSPlayer::~CDSPlayer()
{
	/* Resume AE processing of XBMC native audio */
	if (!CAEFactory::Resume())
	{
		CLog::Log(LOGFATAL, __FUNCTION__, "Failed to restart AudioEngine after return from DSPlayer");
	}	

	if (PlayerState != DSPLAYER_CLOSED)
		CloseFile();

	UnloadExternalObjects();
	CLog::Log(LOGDEBUG, "%s External objects unloaded", __FUNCTION__);

	// Restore DVD Player time base clock
	CDVDClock::SetTimeBase(DVD_TIME_BASE);

	// Save Shader settings
	g_dsSettings.pixelShaderList->SaveXML();

	CoUninitialize();

	SAFE_DELETE(g_dsGraph);
	SAFE_DELETE(g_pPVRStream);

	CLog::Log(LOGNOTICE, "%s DSPlayer is now closed", __FUNCTION__);
}


void CDSPlayer::ShowEditionDlg(bool playStart)
{
	UINT count = GetEditionsCount();

	if (count < 2)
		return;

	if(playStart && m_PlayerOptions.starttime > 0)
	{
		CDSPlayerDatabase db;
		if (db.Open())
		{
			CEdition edition;
			if (db.GetResumeEdition(currentFileItem.GetPath(), edition))
			{
				CLog::Log(LOGDEBUG,"%s select bookmark, edition with idx %i selected", __FUNCTION__, edition.editionNumber);
				SetEdition(edition.editionNumber);
				return;
			}
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
		int selected = GetEdition();
		for (UINT i = 0; i < count; i++)
		{
			CStdString name;
			REFERENCE_TIME duration;

			GetEditionInfo(i, name, &duration);

			if (duration == _I64_MIN || listAllTitles || count == 1 || duration >= DS_TIME_BASE * 60 * minLength) 
			{
				if(i == selected)
					selected = editionOptions.size();

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

		dialog->SetSelected(selected);
		dialog->DoModal();

		selected = dialog->GetSelectedLabel();
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

bool CDSPlayer::OpenFileInternal(const CFileItem& file)
{
	try
	{
		CLog::Log(LOGNOTICE, "%s - DSPlayer: Opening: %s", __FUNCTION__, file.GetPath().c_str());
		if(PlayerState != DSPLAYER_CLOSED)
			CloseFile();

		if(!WaitForThreadExit(100) || !m_pGraphThread.WaitForThreadExit(100))
		{
			return false;
		}

		PlayerState = DSPLAYER_LOADING;
		currentFileItem = file;

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
				PostMessage( new CDSMsgPlayerSeekTime(SEC_TO_DS_TIME(m_PlayerOptions.starttime), 1U, false) , false );
			else
				PostMessage( new CDSMsgPlayerSeekTime(0, 1U, false) , false );

			// Starts playback
			PostMessage( new CDSMsgBool(CDSMsg::PLAYER_PLAY, true), false );
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

bool CDSPlayer::OpenFile(const CFileItem& file,const CPlayerOptions &options)
{
	CLog::Log(LOGNOTICE, "%s - DSPlayer: Opening: %s", __FUNCTION__, file.GetPath().c_str());

	CFileItem fileItem = file;
	m_PlayerOptions = options;

	if (fileItem.IsInternetStream())
	{
		CURL url(fileItem.GetPath());
		url.SetProtocolOptions("");
		fileItem.SetPath(url.Get());
	} 
	else if(fileItem.IsPVR())
	{
		g_pPVRStream = new CDSInputStreamPVRManager(this);
		return g_pPVRStream->Open(fileItem);
	}

	return OpenFileInternal(fileItem);
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

  PlayerState = DSPLAYER_CLOSED;

  // Stop threads
  m_pGraphThread.StopThread(false);
  StopThread(false);

  CLog::Log(LOGDEBUG, "%s File closed", __FUNCTION__);
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
	m_threadID = CThread::GetCurrentThreadId();
}

void CDSPlayer::OnExit()
{
	if (PlayerState == DSPLAYER_LOADING)
		PlayerState = DSPLAYER_ERROR;

	// In case of, set the ready event
	// Prevent a dead loop
	m_hReadyEvent.Set();
	m_bStop = true;
	m_threadID = 0;
	if(m_PlayerOptions.identify == false)
	{
		if (!m_bEof || PlayerState == DSPLAYER_ERROR)
			m_callback.OnPlayBackStopped();
		else
			m_callback.OnPlayBackEnded();
	}

	m_PlayerOptions.identify = false;
}

void CDSPlayer::Process()
{
	HRESULT hr = E_FAIL;
	CLog::Log(LOGNOTICE, "%s - Creating DS Graph",  __FUNCTION__);

	START_PERFORMANCE_COUNTER
		hr = g_dsGraph->SetFile(currentFileItem, m_PlayerOptions);
	END_PERFORMANCE_COUNTER("Loading file");

	// Start playback
	// If there's an error, the lock must be released in order to show the error dialog
	m_hReadyEvent.Set();

	if(FAILED(hr))
	{
		CLog::Log(LOGERROR, "%s - Failed creating DS Graph", __FUNCTION__);
		PlayerState = DSPLAYER_ERROR;
		return;
	}

	m_pGraphThread.SetCurrentRate(1);
	m_pGraphThread.Create();

	CLog::Log(LOGNOTICE, "%s - Successfully creating DS Graph",  __FUNCTION__);

	if(g_pPVRStream)
	{
		CFileItem item(g_application.CurrentFileItem());
		if(g_pPVRStream->UpdateItem(item))
		{
			g_application.CurrentFileItem() = item;
			CApplicationMessenger::Get().SetCurrentItem(item);
		}
	}

	g_dsSettings.pRendererSettings->bAllowFullscreen = m_PlayerOptions.fullscreen;

	while (!m_bStop && PlayerState != DSPLAYER_CLOSED && PlayerState != DSPLAYER_LOADING)
		HandleMessages();
}

void CDSPlayer::HandleMessages()
{
  MSG msg;
  while (GetMessage(&msg, (HWND) -1, 0, 0) != 0 && msg.message == WM_GRAPHMESSAGE)
  {
    CDSMsg* pMsg = reinterpret_cast<CDSMsg *>( msg.lParam );
    CLog::Log(LOGDEBUG, "%s Message received : %d on thread 0x%X", __FUNCTION__, pMsg->GetMessageType(), m_threadID);

    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED || CDSPlayer::PlayerState == DSPLAYER_LOADING)
	{
		pMsg->Set();
		pMsg->Release();
		break;
	}

    if ( pMsg->IsType(CDSMsg::GENERAL_SET_WINDOW_POS) )
    {
      g_dsGraph->UpdateWindowPosition();
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_SEEK_TIME) )
    {
      CDSMsgPlayerSeekTime* speMsg = reinterpret_cast<CDSMsgPlayerSeekTime *>( pMsg );
      g_dsGraph->Seek(speMsg->GetTime(), speMsg->GetFlags(), speMsg->ShowPopup());
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_SEEK) )
    {
      CDSMsgPlayerSeek* speMsg = reinterpret_cast<CDSMsgPlayerSeek*>( pMsg );
      g_dsGraph->Seek( speMsg->Forward(), speMsg->LargeStep() );
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_SEEK_PERCENT) )
    {
      CDSMsgDouble * speMsg = reinterpret_cast<CDSMsgDouble *>( pMsg );
      g_dsGraph->SeekPercentage((float) speMsg->m_value );
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_PAUSE) )
    {
      g_dsGraph->Pause();
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_STOP) )
    {
      CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool *>( pMsg );
      g_dsGraph->Stop(speMsg->m_value);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_PLAY) )
    {
      CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool *>( pMsg );
      g_dsGraph->Play(speMsg->m_value);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_UPDATE_TIME) )
    {
      g_dsGraph->UpdateTime();
    }

    /*DVD COMMANDS*/
    if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MOUSE_MOVE) )
    {
      CDSMsgInt* speMsg = reinterpret_cast<CDSMsgInt *>( pMsg );
      //TODO make the xbmc gui stay hidden when moving mouse over menu
      POINT pt;
      pt.x = GET_X_LPARAM(speMsg->m_value);
      pt.y = GET_Y_LPARAM(speMsg->m_value);
      ULONG pButtonIndex;
      /**** Didnt found really where dvdplayer are doing it exactly so here it is *****/
      XBMC_Event newEvent;
      newEvent.type = XBMC_MOUSEMOTION;
      newEvent.motion.x = (uint16_t) pt.x;
      newEvent.motion.y = (uint16_t) pt.y;
      g_application.OnEvent(newEvent);
      /*CGUIMessage pMsg(GUI_MSG_VIDEO_MENU_STARTED, 0, 0);
      g_windowManager.SendMessage(pMsg);*/
      /**** End of ugly hack ***/
      if (SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetButtonAtPosition(pt, &pButtonIndex)))
        CGraphFilters::Get()->DVD.dvdControl->SelectButton(pButtonIndex);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MOUSE_CLICK) )
    {
      CDSMsgInt* speMsg = reinterpret_cast<CDSMsgInt *>( pMsg );
      POINT pt;
      pt.x = GET_X_LPARAM(speMsg->m_value);
      pt.y = GET_Y_LPARAM(speMsg->m_value);
      ULONG pButtonIndex;
      if (SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetButtonAtPosition(pt, &pButtonIndex)))
        CGraphFilters::Get()->DVD.dvdControl->SelectAndActivateButton(pButtonIndex);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_UP) )
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Upper);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_DOWN) )
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Lower);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_LEFT) )
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Left);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_RIGHT) )
    {
      CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Right);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_ROOT) )
    {
      CGUIMessage _msg(GUI_MSG_VIDEO_MENU_STARTED, 0, 0);
      g_windowManager.SendMessage(_msg);
      CGraphFilters::Get()->DVD.dvdControl->ShowMenu(DVD_MENU_Root , DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_EXIT) )
    {
      CGraphFilters::Get()->DVD.dvdControl->Resume(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_BACK) )
    {
      CGraphFilters::Get()->DVD.dvdControl->ReturnFromSubmenu(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_SELECT) )
    {
      CGraphFilters::Get()->DVD.dvdControl->ActivateButton();
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_TITLE) )
    {
      CGraphFilters::Get()->DVD.dvdControl->ShowMenu(DVD_MENU_Title, DVD_CMD_FLAG_Block|DVD_CMD_FLAG_Flush, NULL);
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_SUBTITLE) )
    {
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_AUDIO) )
    {
    }
    else if ( pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_ANGLE) )
    {
    }
    pMsg->Set();
    pMsg->Release();
  }
}

void CDSPlayer::Stop()
{
  PostMessage( new CDSMsgBool(CDSMsg::PLAYER_STOP, true));
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
  PostMessage( new CDSMsg(CDSMsg::PLAYER_PAUSE) );
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
  PostMessage( new CDSMsgPlayerSeek(bPlus, bLargeStep) );
}

void CDSPlayer::SeekPercentage(float iPercent)
{
  PostMessage( new CDSMsgDouble(CDSMsg::PLAYER_SEEK_PERCENT, iPercent) );
}

bool CDSPlayer::OnAction(const CAction &action)
{
  if ( g_dsGraph->IsDvd() )
  {
    if ( action.GetID() == ACTION_SHOW_VIDEOMENU )
    {
      PostMessage( new CDSMsg(CDSMsg::PLAYER_DVD_MENU_ROOT), false );
      return true;
    }
    if ( g_dsGraph->IsInMenu() )
    {
      switch (action.GetID())
      {
        case ACTION_PREVIOUS_MENU:
          PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_BACK), false);
        break;
        case ACTION_MOVE_LEFT:
          PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_LEFT), false);
        break;
        case ACTION_MOVE_RIGHT:
          PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_RIGHT), false);
        break;
        case ACTION_MOVE_UP:
          PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_UP), false);
        break;
        case ACTION_MOVE_DOWN:
          PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_DOWN), false);
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
          PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_SELECT), false);
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

  if (g_pPVRStream)
  {
	  switch (action.GetID())
	  {
	  case ACTION_MOVE_UP:
	  case ACTION_NEXT_ITEM:
	  case ACTION_CHANNEL_UP:
		  SelectChannel(true);
		  g_infoManager.SetDisplayAfterSeek();
		  ShowPVRChannelInfo();
		  return true;
		  break;

	  case ACTION_MOVE_DOWN:
	  case ACTION_PREV_ITEM:
	  case ACTION_CHANNEL_DOWN:
		  SelectChannel(false);
		  g_infoManager.SetDisplayAfterSeek();
		  ShowPVRChannelInfo();
		  return true;
		  break;

	  case ACTION_CHANNEL_SWITCH:
		  {
			  // Offset from key codes back to button number
			  int channel = action.GetAmount();
			  SwitchChannel(channel);
			  g_infoManager.SetDisplayAfterSeek();
			  ShowPVRChannelInfo();
			  return true;
		  }
		  break;
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
  PostMessage( new CDSMsgPlayerSeekTime(MSEC_TO_DS_TIME(iTime)) );
  m_callback.OnPlayBackSeek((int) iTime, seekOffset);
}

bool CDSPlayer::SwitchChannel(unsigned int iChannelNumber)
{
	m_PlayerOptions.identify = true;

	return g_pPVRStream->SelectChannelByNumber(iChannelNumber);
}

bool CDSPlayer::SwitchChannel(const CPVRChannel &channel)
{
	if (!g_PVRManager.CheckParentalLock(channel))
		return false;

	/* set GUI info */
	if (!g_PVRManager.PerformChannelSwitch(channel, true))
		return false;


	/* make sure the pvr window is updated */
	CGUIWindowPVR *pWindow = (CGUIWindowPVR *) g_windowManager.GetWindow(WINDOW_PVR);
	if (pWindow)
		pWindow->SetInvalid();

	m_PlayerOptions.identify = true;

	return g_pPVRStream->SelectChannel(channel);
}

bool CDSPlayer::SelectChannel(bool bNext)
{
	m_PlayerOptions.identify = true;

	bool bShowPreview = false;/*(g_guiSettings.GetInt("pvrplayback.channelentrytimeout") > 0);*/ // TODO

	if (!bShowPreview)
	{
		g_infoManager.SetDisplayAfterSeek(100000);
	}

	return bNext ? g_pPVRStream->NextChannel(bShowPreview) : g_pPVRStream->PrevChannel(bShowPreview);
}

bool CDSPlayer::ShowPVRChannelInfo()
{
	bool bReturn(false);

	if (g_guiSettings.GetBool("pvrmenu.infoswitch"))
	{
		int iTimeout = g_guiSettings.GetBool("pvrmenu.infotimeout") ? g_guiSettings.GetInt("pvrmenu.infotime") : 0;
		g_PVRManager.ShowPlayerInfo(iTimeout);

		bReturn = true;
	}

	return bReturn;
}

bool CDSPlayer::CachePVRStream(void) const
{
	return g_pPVRStream && !g_PVRManager.IsPlayingRecording() && g_advancedSettings.m_bPVRCacheInDvdPlayer;
}

CGraphManagementThread::CGraphManagementThread(CDSPlayer * pPlayer)
  : m_pPlayer(pPlayer), m_bSpeedChanged(false), CThread("CGraphManagementThread thread")
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

    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
    // Handle Rewind or Fast Forward
    if (m_currentRate != 1)
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