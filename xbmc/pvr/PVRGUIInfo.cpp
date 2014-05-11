/*
 *      Copyright (C) 2012-2013 Team XBMC
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

#include "Application.h"
#include "PVRGUIInfo.h"
#include "guilib/LocalizeStrings.h"
#include "utils/StringUtils.h"
#include "GUIInfoManager.h"
#include "Util.h"
#include "threads/SingleLock.h"
#include "PVRManager.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/timers/PVRTimers.h"
#include "pvr/recordings/PVRRecordings.h"
#include "pvr/channels/PVRChannel.h"
#include "epg/EpgInfoTag.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"

using namespace PVR;
using namespace EPG;
using namespace std;

CPVRGUIInfo::CPVRGUIInfo(void) :
    CThread("PVRGUIInfo"),
    m_playingEpgTag(NULL)
{
  ResetProperties();
}

CPVRGUIInfo::~CPVRGUIInfo(void)
{
  Stop();
}

void CPVRGUIInfo::ResetProperties(void)
{
  CSingleLock lock(m_critSection);
  m_strActiveTimerTitle         = StringUtils::EmptyString;
  m_strActiveTimerChannelName   = StringUtils::EmptyString;
  m_strActiveTimerChannelIcon   = StringUtils::EmptyString;
  m_strActiveTimerTime          = StringUtils::EmptyString;
  m_strNextTimerInfo            = StringUtils::EmptyString;
  m_strNextRecordingTitle       = StringUtils::EmptyString;
  m_strNextRecordingChannelName = StringUtils::EmptyString;
  m_strNextRecordingChannelIcon = StringUtils::EmptyString;
  m_strNextRecordingTime        = StringUtils::EmptyString;
  m_iTimerAmount                = 0;
  m_bHasRecordings              = false;
  m_iRecordingTimerAmount       = 0;
  m_iActiveClients              = 0;
  m_strPlayingClientName        = StringUtils::EmptyString;
  m_strBackendName              = StringUtils::EmptyString;
  m_strBackendVersion           = StringUtils::EmptyString;
  m_strBackendHost              = StringUtils::EmptyString;
  m_strBackendDiskspace         = StringUtils::EmptyString;
  m_strBackendTimers            = StringUtils::EmptyString;
  m_strBackendRecordings        = StringUtils::EmptyString;
  m_strBackendChannels          = StringUtils::EmptyString;
  m_strTotalDiskspace           = StringUtils::EmptyString;
  m_iAddonInfoToggleStart       = 0;
  m_iAddonInfoToggleCurrent     = 0;
  m_iTimerInfoToggleStart       = 0;
  m_iTimerInfoToggleCurrent     = 0;
  m_ToggleShowInfo.SetInfinite();
  m_iDuration                   = 0;
  m_bHasNonRecordingTimers      = false;
  m_bIsPlayingTV                = false;
  m_bIsPlayingRadio             = false;
  m_bIsPlayingRecording         = false;
  m_bIsPlayingEncryptedStream   = false;

  ResetPlayingTag();
  ClearQualityInfo(m_qualityInfo);
}

void CPVRGUIInfo::ClearQualityInfo(PVR_SIGNAL_STATUS &qualityInfo)
{
  memset(&qualityInfo, 0, sizeof(qualityInfo));
  strncpy(qualityInfo.strAdapterName, g_localizeStrings.Get(13106).c_str(), PVR_ADDON_NAME_STRING_LENGTH - 1);
  strncpy(qualityInfo.strAdapterStatus, g_localizeStrings.Get(13106).c_str(), PVR_ADDON_NAME_STRING_LENGTH - 1);
}

void CPVRGUIInfo::Start(void)
{
  ResetProperties();
  Create();
  SetPriority(-1);
}

void CPVRGUIInfo::Stop(void)
{
  StopThread();
  if (g_PVRTimers)
    g_PVRTimers->UnregisterObserver(this);
}

void CPVRGUIInfo::Notify(const Observable &obs, const ObservableMessage msg)
{
  if (msg == ObservableMessageTimers || msg == ObservableMessageTimersReset)
    UpdateTimersCache();
}

void CPVRGUIInfo::ShowPlayerInfo(int iTimeout)
{
  CSingleLock lock(m_critSection);

  if (iTimeout > 0)
    m_ToggleShowInfo.Set(iTimeout * 1000);

  g_infoManager.SetShowInfo(true);
}

void CPVRGUIInfo::ToggleShowInfo(void)
{
  CSingleLock lock(m_critSection);

  if (m_ToggleShowInfo.IsTimePast())
  {
    m_ToggleShowInfo.SetInfinite();
    g_infoManager.SetShowInfo(false);
  }
}

bool CPVRGUIInfo::AddonInfoToggle(void)
{
  CSingleLock lock(m_critSection);
  if (m_iAddonInfoToggleStart == 0)
  {
    m_iAddonInfoToggleStart = XbmcThreads::SystemClockMillis();
    m_iAddonInfoToggleCurrent = 0;
    return true;
  }

  if ((int) (XbmcThreads::SystemClockMillis() - m_iAddonInfoToggleStart) > g_advancedSettings.m_iPVRInfoToggleInterval)
  {
    unsigned int iPrevious = m_iAddonInfoToggleCurrent;
    if (((int) ++m_iAddonInfoToggleCurrent) > m_iActiveClients - 1)
      m_iAddonInfoToggleCurrent = 0;

    return m_iAddonInfoToggleCurrent != iPrevious;
  }

  return false;
}

bool CPVRGUIInfo::TimerInfoToggle(void)
{
  CSingleLock lock(m_critSection);
  if (m_iTimerInfoToggleStart == 0)
  {
    m_iTimerInfoToggleStart = XbmcThreads::SystemClockMillis();
    m_iTimerInfoToggleCurrent = 0;
    return true;
  }

  if ((int) (XbmcThreads::SystemClockMillis() - m_iTimerInfoToggleStart) > g_advancedSettings.m_iPVRInfoToggleInterval)
  {
    unsigned int iPrevious = m_iTimerInfoToggleCurrent;
    unsigned int iBoundary = m_iRecordingTimerAmount > 0 ? m_iRecordingTimerAmount : m_iTimerAmount;
    if (++m_iTimerInfoToggleCurrent > iBoundary - 1)
      m_iTimerInfoToggleCurrent = 0;

    return m_iTimerInfoToggleCurrent != iPrevious;
  }

  return false;
}

void CPVRGUIInfo::Process(void)
{
  unsigned int mLoop(0);

  /* updated on request */
  g_PVRTimers->RegisterObserver(this);
  UpdateTimersCache();

  while (!g_application.m_bStop && !m_bStop)
  {
    if (!m_bStop)
      ToggleShowInfo();
    Sleep(0);

    if (!m_bStop)
      UpdateQualityData();
    Sleep(0);

    if (!m_bStop)
      UpdateMisc();
    Sleep(0);

    if (!m_bStop)
      UpdatePlayingTag();
    Sleep(0);

    if (!m_bStop)
      UpdateTimersToggle();
    Sleep(0);

    if (!m_bStop)
      UpdateNextTimer();
    Sleep(0);

    if (!m_bStop && mLoop % 10 == 0)
      UpdateBackendCache();    /* updated every 10 iterations */

    if (++mLoop == 1000)
      mLoop = 0;

    if (!m_bStop)
      Sleep(1000);
  }

  if (!m_bStop)
    ResetPlayingTag();
}

void CPVRGUIInfo::UpdateQualityData(void)
{
  PVR_SIGNAL_STATUS qualityInfo;
  ClearQualityInfo(qualityInfo);

  PVR_CLIENT client;
  if (CSettings::Get().GetBool("pvrplayback.signalquality") &&
      g_PVRClients->GetPlayingClient(client))
  {
    client->SignalQuality(qualityInfo);
  }

  memcpy(&m_qualityInfo, &qualityInfo, sizeof(m_qualityInfo));
}

void CPVRGUIInfo::UpdateMisc(void)
{
  bool bStarted = g_PVRManager.IsStarted();
  CStdString strPlayingClientName      = bStarted ? g_PVRClients->GetPlayingClientName() : StringUtils::EmptyString;
  bool       bHasRecordings            = bStarted && g_PVRRecordings->GetNumRecordings() > 0;
  bool       bIsPlayingTV              = bStarted && g_PVRClients->IsPlayingTV();
  bool       bIsPlayingRadio           = bStarted && g_PVRClients->IsPlayingRadio();
  bool       bIsPlayingRecording       = bStarted && g_PVRClients->IsPlayingRecording();
  bool       bIsPlayingEncryptedStream = bStarted && g_PVRClients->IsEncrypted();
  /* safe to fetch these unlocked, since they're updated from the same thread as this one */
  bool       bHasNonRecordingTimers    = bStarted && m_iTimerAmount - m_iRecordingTimerAmount > 0;

  CSingleLock lock(m_critSection);
  m_strPlayingClientName      = strPlayingClientName;
  m_bHasRecordings            = bHasRecordings;
  m_bHasNonRecordingTimers    = bHasNonRecordingTimers;
  m_bIsPlayingTV              = bIsPlayingTV;
  m_bIsPlayingRadio           = bIsPlayingRadio;
  m_bIsPlayingRecording       = bIsPlayingRecording;
  m_bIsPlayingEncryptedStream = bIsPlayingEncryptedStream;
}

bool CPVRGUIInfo::TranslateCharInfo(DWORD dwInfo, CStdString &strValue) const
{
  bool bReturn(true);
  CSingleLock lock(m_critSection);

  switch(dwInfo)
  {
  case PVR_NOW_RECORDING_TITLE:
    CharInfoActiveTimerTitle(strValue);
    break;
  case PVR_NOW_RECORDING_CHANNEL:
    CharInfoActiveTimerChannelName(strValue);
    break;
  case PVR_NOW_RECORDING_CHAN_ICO:
    CharInfoActiveTimerChannelIcon(strValue);
    break;
  case PVR_NOW_RECORDING_DATETIME:
    CharInfoActiveTimerDateTime(strValue);
    break;
  case PVR_NEXT_RECORDING_TITLE:
    CharInfoNextTimerTitle(strValue);
    break;
  case PVR_NEXT_RECORDING_CHANNEL:
    CharInfoNextTimerChannelName(strValue);
    break;
  case PVR_NEXT_RECORDING_CHAN_ICO:
    CharInfoNextTimerChannelIcon(strValue);
    break;
  case PVR_NEXT_RECORDING_DATETIME:
    CharInfoNextTimerDateTime(strValue);
    break;
  case PVR_PLAYING_DURATION:
    CharInfoPlayingDuration(strValue);
    break;
  case PVR_PLAYING_TIME:
    CharInfoPlayingTime(strValue);
    break;
  case PVR_NEXT_TIMER:
    CharInfoNextTimer(strValue);
    break;
  case PVR_ACTUAL_STREAM_VIDEO_BR:
    CharInfoVideoBR(strValue);
    break;
  case PVR_ACTUAL_STREAM_AUDIO_BR:
    CharInfoAudioBR(strValue);
    break;
  case PVR_ACTUAL_STREAM_DOLBY_BR:
    CharInfoDolbyBR(strValue);
    break;
  case PVR_ACTUAL_STREAM_SIG:
    CharInfoSignal(strValue);
    break;
  case PVR_ACTUAL_STREAM_SNR:
    CharInfoSNR(strValue);
    break;
  case PVR_ACTUAL_STREAM_BER:
    CharInfoBER(strValue);
    break;
  case PVR_ACTUAL_STREAM_UNC:
    CharInfoUNC(strValue);
    break;
  case PVR_ACTUAL_STREAM_CLIENT:
    CharInfoPlayingClientName(strValue);
    break;
  case PVR_ACTUAL_STREAM_DEVICE:
    CharInfoFrontendName(strValue);
    break;
  case PVR_ACTUAL_STREAM_STATUS:
    CharInfoFrontendStatus(strValue);
    break;
  case PVR_ACTUAL_STREAM_CRYPTION:
    CharInfoEncryption(strValue);
    break;
  case PVR_ACTUAL_STREAM_SERVICE:
    CharInfoService(strValue);
    break;
  case PVR_ACTUAL_STREAM_MUX:
    CharInfoMux(strValue);
    break;
  case PVR_ACTUAL_STREAM_PROVIDER:
    CharInfoProvider(strValue);
    break;
  case PVR_BACKEND_NAME:
    CharInfoBackendName(strValue);
    break;
  case PVR_BACKEND_VERSION:
    CharInfoBackendVersion(strValue);
    break;
  case PVR_BACKEND_HOST:
    CharInfoBackendHost(strValue);
    break;
  case PVR_BACKEND_DISKSPACE:
    CharInfoBackendDiskspace(strValue);
    break;
  case PVR_BACKEND_CHANNELS:
    CharInfoBackendChannels(strValue);
    break;
  case PVR_BACKEND_TIMERS:
    CharInfoBackendTimers(strValue);
    break;
  case PVR_BACKEND_RECORDINGS:
    CharInfoBackendRecordings(strValue);
    break;
  case PVR_BACKEND_NUMBER:
    CharInfoBackendNumber(strValue);
    break;
  case PVR_TOTAL_DISKSPACE:
    CharInfoTotalDiskSpace(strValue);
    break;
  default:
    strValue = StringUtils::EmptyString;
    bReturn = false;
    break;
  }

  return bReturn;
}

bool CPVRGUIInfo::TranslateBoolInfo(DWORD dwInfo) const
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  switch (dwInfo)
  {
  case PVR_IS_RECORDING:
    bReturn = m_iRecordingTimerAmount > 0;
    break;
  case PVR_HAS_TIMER:
    bReturn = m_iTimerAmount > 0;
    break;
  case PVR_HAS_NONRECORDING_TIMER:
    bReturn = m_bHasNonRecordingTimers;
    break;
  case PVR_IS_PLAYING_TV:
    bReturn = m_bIsPlayingTV;
    break;
  case PVR_IS_PLAYING_RADIO:
    bReturn = m_bIsPlayingRadio;
    break;
  case PVR_IS_PLAYING_RECORDING:
    bReturn = m_bIsPlayingRecording;
    break;
  case PVR_ACTUAL_STREAM_ENCRYPTED:
    bReturn = m_bIsPlayingEncryptedStream;
    break;
  default:
    break;
  }

  return bReturn;
}

int CPVRGUIInfo::TranslateIntInfo(DWORD dwInfo) const
{
  int iReturn(0);
  CSingleLock lock(m_critSection);

  if (dwInfo == PVR_PLAYING_PROGRESS)
    iReturn = (int) ((float) GetStartTime() / m_iDuration * 100);
  else if (dwInfo == PVR_ACTUAL_STREAM_SIG_PROGR)
    iReturn = (int) ((float) m_qualityInfo.iSignal / 0xFFFF * 100);
  else if (dwInfo == PVR_ACTUAL_STREAM_SNR_PROGR)
    iReturn = (int) ((float) m_qualityInfo.iSNR / 0xFFFF * 100);

  return iReturn;
}

void CPVRGUIInfo::CharInfoActiveTimerTitle(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strActiveTimerTitle.c_str());
}

void CPVRGUIInfo::CharInfoActiveTimerChannelName(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strActiveTimerChannelName.c_str());
}

void CPVRGUIInfo::CharInfoActiveTimerChannelIcon(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strActiveTimerChannelIcon.c_str());
}

void CPVRGUIInfo::CharInfoActiveTimerDateTime(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strActiveTimerTime.c_str());
}

void CPVRGUIInfo::CharInfoNextTimerTitle(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strNextRecordingTitle.c_str());
}

void CPVRGUIInfo::CharInfoNextTimerChannelName(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strNextRecordingChannelName.c_str());
}

void CPVRGUIInfo::CharInfoNextTimerChannelIcon(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strNextRecordingChannelIcon.c_str());
}

void CPVRGUIInfo::CharInfoNextTimerDateTime(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strNextRecordingTime.c_str());
}

void CPVRGUIInfo::CharInfoPlayingDuration(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", StringUtils::SecondsToTimeString(m_iDuration / 1000, TIME_FORMAT_GUESS).c_str());
}

void CPVRGUIInfo::CharInfoPlayingTime(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", StringUtils::SecondsToTimeString(GetStartTime()/1000, TIME_FORMAT_GUESS).c_str());
}

void CPVRGUIInfo::CharInfoNextTimer(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strNextTimerInfo.c_str());
}

void CPVRGUIInfo::CharInfoBackendNumber(CStdString &strValue) const
{
  if (m_iActiveClients > 0)
    strValue = StringUtils::Format("%u %s %u", m_iAddonInfoToggleCurrent+1, g_localizeStrings.Get(20163).c_str(), m_iActiveClients);
  else
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(14023).c_str());
}

void CPVRGUIInfo::CharInfoTotalDiskSpace(CStdString &strValue) const
{
  strValue = StringUtils::Format("%s", m_strTotalDiskspace.c_str());
}

void CPVRGUIInfo::CharInfoVideoBR(CStdString &strValue) const
{
  strValue = StringUtils::Format("%.2f Mbit/s", m_qualityInfo.dVideoBitrate);
}

void CPVRGUIInfo::CharInfoAudioBR(CStdString &strValue) const
{
  strValue = StringUtils::Format("%.0f kbit/s", m_qualityInfo.dAudioBitrate);
}

void CPVRGUIInfo::CharInfoDolbyBR(CStdString &strValue) const
{
  strValue = StringUtils::Format("%.0f kbit/s", m_qualityInfo.dDolbyBitrate);
}

void CPVRGUIInfo::CharInfoSignal(CStdString &strValue) const
{
  strValue = StringUtils::Format("%d %%", m_qualityInfo.iSignal / 655);
}

void CPVRGUIInfo::CharInfoSNR(CStdString &strValue) const
{
  strValue = StringUtils::Format("%d %%", m_qualityInfo.iSNR / 655);
}

void CPVRGUIInfo::CharInfoBER(CStdString &strValue) const
{
  strValue = StringUtils::Format("%08X", m_qualityInfo.iBER);
}

void CPVRGUIInfo::CharInfoUNC(CStdString &strValue) const
{
  strValue = StringUtils::Format("%08X", m_qualityInfo.iUNC);
}

void CPVRGUIInfo::CharInfoFrontendName(CStdString &strValue) const
{
  if (!strcmp(m_qualityInfo.strAdapterName, StringUtils::EmptyString))
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_qualityInfo.strAdapterName);
}

void CPVRGUIInfo::CharInfoFrontendStatus(CStdString &strValue) const
{
  if (!strcmp(m_qualityInfo.strAdapterStatus, StringUtils::EmptyString))
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_qualityInfo.strAdapterStatus);
}

void CPVRGUIInfo::CharInfoBackendName(CStdString &strValue) const
{
  if (m_strBackendName.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strBackendName.c_str());
}

void CPVRGUIInfo::CharInfoBackendVersion(CStdString &strValue) const
{
  if (m_strBackendVersion.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s",  m_strBackendVersion.c_str());
}

void CPVRGUIInfo::CharInfoBackendHost(CStdString &strValue) const
{
  if (m_strBackendHost.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strBackendHost.c_str());
}

void CPVRGUIInfo::CharInfoBackendDiskspace(CStdString &strValue) const
{
  if (m_strBackendDiskspace.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strBackendDiskspace.c_str());
}

void CPVRGUIInfo::CharInfoBackendChannels(CStdString &strValue) const
{
  if (m_strBackendChannels.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strBackendChannels.c_str());
}

void CPVRGUIInfo::CharInfoBackendTimers(CStdString &strValue) const
{
  if (m_strBackendTimers.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strBackendTimers.c_str());
}

void CPVRGUIInfo::CharInfoBackendRecordings(CStdString &strValue) const
{
  if (m_strBackendRecordings.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strBackendRecordings.c_str());
}

void CPVRGUIInfo::CharInfoPlayingClientName(CStdString &strValue) const
{
  if (m_strPlayingClientName.empty())
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_strPlayingClientName.c_str());
}

void CPVRGUIInfo::CharInfoEncryption(CStdString &strValue) const
{
  CPVRChannelPtr channel;
  if (g_PVRClients->GetPlayingChannel(channel))
    strValue = StringUtils::Format("%s", channel->EncryptionName().c_str());
  else
    strValue = StringUtils::EmptyString;
}

void CPVRGUIInfo::CharInfoService(CStdString &strValue) const
{
  if (!strcmp(m_qualityInfo.strServiceName, StringUtils::EmptyString))
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_qualityInfo.strServiceName);
}

void CPVRGUIInfo::CharInfoMux(CStdString &strValue) const
{
  if (!strcmp(m_qualityInfo.strMuxName, StringUtils::EmptyString))
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_qualityInfo.strMuxName);
}

void CPVRGUIInfo::CharInfoProvider(CStdString &strValue) const
{
  if (!strcmp(m_qualityInfo.strProviderName, StringUtils::EmptyString))
    strValue = StringUtils::Format("%s", g_localizeStrings.Get(13205).c_str());
  else
    strValue = StringUtils::Format("%s", m_qualityInfo.strProviderName);
}

void CPVRGUIInfo::UpdateBackendCache(void)
{
  CStdString strBackendName;
  CStdString strBackendVersion;
  CStdString strBackendHost;
  CStdString strBackendDiskspace;
  CStdString strBackendTimers;
  CStdString strBackendRecordings;
  CStdString strBackendChannels;
  int        iActiveClients(0);

  if (!AddonInfoToggle())
    return;

  CPVRClients *clients = g_PVRClients;
  PVR_CLIENTMAP activeClients;
  iActiveClients = clients->GetConnectedClients(activeClients);
  if (iActiveClients > 0)
  {
    PVR_CLIENTMAP_CITR activeClient = activeClients.begin();
    /* safe to read unlocked */
    for (unsigned int i = 0; i < m_iAddonInfoToggleCurrent; i++)
      activeClient++;

    long long kBTotal = 0;
    long long kBUsed  = 0;

    if (activeClient->second->GetDriveSpace(&kBTotal, &kBUsed) == PVR_ERROR_NO_ERROR)
    {
      kBTotal /= 1024; // Convert to MBytes
      kBUsed /= 1024;  // Convert to MBytes
      strBackendDiskspace = StringUtils::Format("%s %.1f GByte - %s: %.1f GByte",
          g_localizeStrings.Get(20161).c_str(), (float) kBTotal / 1024, g_localizeStrings.Get(20162).c_str(), (float) kBUsed / 1024);
    }
    else
    {
      strBackendDiskspace = g_localizeStrings.Get(19055);
    }

    int NumChannels = activeClient->second->GetChannelsAmount();
    if (NumChannels >= 0)
      strBackendChannels = StringUtils::Format("%i", NumChannels);
    else
      strBackendChannels = g_localizeStrings.Get(161);

    int NumTimers = activeClient->second->GetTimersAmount();
    if (NumTimers >= 0)
      strBackendTimers = StringUtils::Format("%i", NumTimers);
    else
      strBackendTimers = g_localizeStrings.Get(161);

    int NumRecordings = activeClient->second->GetRecordingsAmount();
    if (NumRecordings >= 0)
      strBackendRecordings = StringUtils::Format("%i", NumRecordings);
    else
      strBackendRecordings = g_localizeStrings.Get(161);

    strBackendName    = activeClient->second->GetBackendName();
    strBackendVersion = activeClient->second->GetBackendVersion();
    strBackendHost    = activeClient->second->GetConnectionString();
  }

  CSingleLock lock(m_critSection);
  m_strBackendName         = strBackendName;
  m_strBackendVersion      = strBackendVersion;
  m_strBackendHost         = strBackendHost;
  m_strBackendDiskspace    = strBackendDiskspace;
  m_strBackendTimers       = strBackendTimers;
  m_strBackendRecordings   = strBackendRecordings;
  m_strBackendChannels     = strBackendChannels;
  m_iActiveClients         = iActiveClients;
}

void CPVRGUIInfo::UpdateTimersCache(void)
{
  int iTimerAmount          = g_PVRTimers->AmountActiveTimers();
  int iRecordingTimerAmount = g_PVRTimers->AmountActiveRecordings();

  {
    CSingleLock lock(m_critSection);
    m_iTimerAmount          = iTimerAmount;
    m_iRecordingTimerAmount = iRecordingTimerAmount;
    m_iTimerInfoToggleStart = 0;
  }

  UpdateTimersToggle();
}

void CPVRGUIInfo::UpdateNextTimer(void)
{
  CStdString strNextRecordingTitle;
  CStdString strNextRecordingChannelName;
  CStdString strNextRecordingChannelIcon;
  CStdString strNextRecordingTime;
  CStdString strNextTimerInfo;

  CFileItemPtr tag = g_PVRTimers->GetNextActiveTimer();
  if (tag && tag->HasPVRTimerInfoTag())
  {
    CPVRTimerInfoTag *timer = tag->GetPVRTimerInfoTag();
    strNextRecordingTitle = StringUtils::Format("%s",       timer->Title().c_str());
    strNextRecordingChannelName = StringUtils::Format("%s", timer->ChannelName().c_str());
    strNextRecordingChannelIcon = StringUtils::Format("%s", timer->ChannelIcon().c_str());
    strNextRecordingTime = StringUtils::Format("%s",        timer->StartAsLocalTime().GetAsLocalizedDateTime(false, false).c_str());

    strNextTimerInfo = StringUtils::Format("%s %s %s %s",
        g_localizeStrings.Get(19106).c_str(),
        timer->StartAsLocalTime().GetAsLocalizedDate(true).c_str(),
        g_localizeStrings.Get(19107).c_str(),
        timer->StartAsLocalTime().GetAsLocalizedTime("HH:mm", false).c_str());
  }

  CSingleLock lock(m_critSection);
  m_strNextRecordingTitle       = strNextRecordingTitle;
  m_strNextRecordingChannelName = strNextRecordingChannelName;
  m_strNextRecordingChannelIcon = strNextRecordingChannelIcon;
  m_strNextRecordingTime        = strNextRecordingTime;
  m_strNextTimerInfo            = strNextTimerInfo;
}

void CPVRGUIInfo::UpdateTimersToggle(void)
{
  if (!TimerInfoToggle())
    return;

  CStdString strActiveTimerTitle;
  CStdString strActiveTimerChannelName;
  CStdString strActiveTimerChannelIcon;
  CStdString strActiveTimerTime;

  /* safe to fetch these unlocked, since they're updated from the same thread as this one */
  if (m_iRecordingTimerAmount > 0)
  {
    vector<CFileItemPtr> activeTags = g_PVRTimers->GetActiveRecordings();
    if (m_iTimerInfoToggleCurrent < activeTags.size() && activeTags.at(m_iTimerInfoToggleCurrent)->HasPVRTimerInfoTag())
    {
      CPVRTimerInfoTag *tag = activeTags.at(m_iTimerInfoToggleCurrent)->GetPVRTimerInfoTag();
      strActiveTimerTitle = StringUtils::Format("%s",       tag->Title().c_str());
      strActiveTimerChannelName = StringUtils::Format("%s", tag->ChannelName().c_str());
      strActiveTimerChannelIcon = StringUtils::Format("%s", tag->ChannelIcon().c_str());
      strActiveTimerTime = StringUtils::Format("%s",        tag->StartAsLocalTime().GetAsLocalizedDateTime(false, false).c_str());
    }
  }

  CSingleLock lock(m_critSection);
  m_strActiveTimerTitle         = strActiveTimerTitle;
  m_strActiveTimerChannelName   = strActiveTimerChannelName;
  m_strActiveTimerChannelIcon   = strActiveTimerChannelIcon;
  m_strActiveTimerTime          = strActiveTimerTime;
}

int CPVRGUIInfo::GetDuration(void) const
{
  CSingleLock lock(m_critSection);
  return m_iDuration;
}

int CPVRGUIInfo::GetStartTime(void) const
{
  CSingleLock lock(m_critSection);
  if (m_playingEpgTag)
  {
    /* Calculate here the position we have of the running live TV event.
     * "position in ms" = ("current UTC" - "event start UTC") * 1000
     */
    CDateTime current = g_PVRClients->GetPlayingTime();
    CDateTime start = m_playingEpgTag->StartAsUTC();
    CDateTimeSpan time = current > start ? current - start : CDateTimeSpan(0, 0, 0, 0);
    return (time.GetDays()   * 60 * 60 * 24
         + time.GetHours()   * 60 * 60
         + time.GetMinutes() * 60
         + time.GetSeconds()) * 1000;
  }
  else
  {
    return 0;
  }
}

void CPVRGUIInfo::ResetPlayingTag(void)
{
  CSingleLock lock(m_critSection);
  SAFE_DELETE(m_playingEpgTag);
  m_iDuration = 0;
}

bool CPVRGUIInfo::GetPlayingTag(CEpgInfoTag &tag) const
{
  bool bReturn(false);

  CSingleLock lock(m_critSection);
  if (m_playingEpgTag)
  {
    tag = *m_playingEpgTag;
    bReturn = true;
  }

  return bReturn;
}

void CPVRGUIInfo::UpdatePlayingTag(void)
{
  CPVRChannelPtr currentChannel;
  CPVRRecording recording;
  if (g_PVRManager.GetCurrentChannel(currentChannel))
  {
    CEpgInfoTag epgTag;
    bool bHasEpgTag  = GetPlayingTag(epgTag);
    CPVRChannelPtr channel;
    if (bHasEpgTag)
      channel = epgTag.ChannelTag();

    if (!bHasEpgTag || !epgTag.IsActive() ||
        !channel || *channel != *currentChannel)
    {
      CEpgInfoTag newTag;
      {
        CSingleLock lock(m_critSection);
        ResetPlayingTag();
        if (currentChannel->GetEPGNow(newTag))
        {
          m_playingEpgTag = new CEpgInfoTag(newTag);
          m_iDuration     = m_playingEpgTag->GetDuration() * 1000;
        }
      }
      g_PVRManager.UpdateCurrentFile();
    }
  }
  else if (g_PVRClients->GetPlayingRecording(recording))
  {
    ResetPlayingTag();
    m_iDuration = recording.GetDuration() * 1000;
  }
}
