/*
 *      Copyright (C) 2005-2012 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"
#include "GUIDialogVideoSettings.h"
#include "guilib/GUIWindowManager.h"
#include "GUIPassword.h"
#include "utils/MathUtils.h"
#include "settings/GUISettings.h"
#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif
#include "video/VideoDatabase.h"
#include "dialogs/GUIDialogYesNo.h"
#include "settings/Settings.h"
#include "addons/Skin.h"
#include "pvr/PVRManager.h"

using namespace std;
using namespace PVR;

#ifdef HAS_DS_PLAYER
#include "cores/DSPlayer/Filters/RendererSettings.h"
#include "cores/DSPlayer/dsgraph.h"
#include "DSUtil/DSUtil.h"
#include "utils/CharsetConverter.h"
#include "guilib/LocalizeStrings.h"
#include "application.h"
#endif

CGUIDialogVideoSettings::CGUIDialogVideoSettings(void)
    : CGUIDialogSettings(WINDOW_DIALOG_VIDEO_OSD_SETTINGS, "VideoOSDSettings.xml")
{
#ifdef HAS_DS_PLAYER
	  m_scalingMethod = 0;
#endif
}

CGUIDialogVideoSettings::~CGUIDialogVideoSettings(void)
{
}

#define VIDEO_SETTINGS_CROP               1
#define VIDEO_SETTINGS_VIEW_MODE          2
#define VIDEO_SETTINGS_ZOOM               3
#define VIDEO_SETTINGS_PIXEL_RATIO        4
#define VIDEO_SETTINGS_BRIGHTNESS         5
#define VIDEO_SETTINGS_CONTRAST           6
#define VIDEO_SETTINGS_GAMMA              7
#define VIDEO_SETTINGS_INTERLACEMETHOD    8
// separator 9
#define VIDEO_SETTINGS_MAKE_DEFAULT       10

#define VIDEO_SETTINGS_CALIBRATION        11
#define VIDEO_SETTINGS_SOFTEN             13
#define VIDEO_SETTINGS_SCALINGMETHOD      18

#define VIDEO_SETTING_VDPAU_NOISE         19
#define VIDEO_SETTING_VDPAU_SHARPNESS     20

#define VIDEO_SETTINGS_NONLIN_STRETCH     21
#define VIDEO_SETTINGS_POSTPROCESS        22
#define VIDEO_SETTINGS_VERTICAL_SHIFT     23
#define VIDEO_SETTINGS_DEINTERLACEMODE    24

#ifdef HAS_DS_PLAYER
#define VIDEO_SETTINGS_DS_STATS           24
#define VIDEO_SETTINGS_DS_FILTERS         0x20
#endif

void CGUIDialogVideoSettings::CreateSettings()
{
  m_usePopupSliders = g_SkinInfo->HasSkinFile("DialogSlider.xml");
  // clear out any old settings
  m_settings.clear();
  // create our settings
  {
    vector<pair<int, int> > entries;

    if (g_renderManager.Supports(VS_DEINTERLACEMODE_OFF))
      entries.push_back(make_pair(VS_DEINTERLACEMODE_OFF    , 16039));

    if (g_renderManager.Supports(VS_DEINTERLACEMODE_AUTO))
      entries.push_back(make_pair(VS_DEINTERLACEMODE_AUTO   , 16040));

    if (g_renderManager.Supports(VS_DEINTERLACEMODE_FORCE))
      entries.push_back(make_pair(VS_DEINTERLACEMODE_FORCE  , 16041));

    if (entries.size())
      AddSpin(VIDEO_SETTINGS_DEINTERLACEMODE, 16037, (int*)&g_settings.m_currentVideoSettings.m_DeinterlaceMode, entries);
  }
  {
    vector<pair<int, int> > entries;
    entries.push_back(make_pair(VS_INTERLACEMETHOD_AUTO                 , 16019));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_RENDER_BLEND         , 20131));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_RENDER_WEAVE_INVERTED, 20130));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_RENDER_WEAVE         , 20129));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_RENDER_BOB_INVERTED  , 16022));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_RENDER_BOB           , 16021));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_DEINTERLACE          , 16020));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_DEINTERLACE_HALF     , 16036));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_SW_BLEND             , 16324));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_INVERSE_TELECINE     , 16314));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL     , 16311));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_VDPAU_TEMPORAL             , 16310));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_VDPAU_BOB                  , 16021));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL_HALF, 16318));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_VDPAU_TEMPORAL_HALF        , 16317));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_VDPAU_INVERSE_TELECINE     , 16314));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_DXVA_BOB                   , 16320));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_DXVA_BEST                  , 16321));
    entries.push_back(make_pair(VS_INTERLACEMETHOD_AUTO_ION                   , 16325));

    /* remove unsupported methods */
    for(vector<pair<int, int> >::iterator it = entries.begin(); it != entries.end();)
    {
      if(g_renderManager.Supports((EINTERLACEMETHOD)it->first))
        it++;
      else
        it = entries.erase(it);
    }

    if (entries.size() > 1)
    {
      AddSpin(VIDEO_SETTINGS_INTERLACEMETHOD, 16038, (int*)&g_settings.m_currentVideoSettings.m_InterlaceMethod, entries);
      if (g_settings.m_currentVideoSettings.m_DeinterlaceMode == VS_DEINTERLACEMODE_OFF)
        EnableSettings(VIDEO_SETTINGS_INTERLACEMETHOD, false);
    }
  }
#ifdef HAS_DS_PLAYER
  if ( g_application.GetCurrentPlayer() == PCID_DVDPLAYER )
#endif
  {
    vector<pair<int, int> > entries;
    entries.push_back(make_pair(VS_SCALINGMETHOD_NEAREST          , 16301));
    entries.push_back(make_pair(VS_SCALINGMETHOD_LINEAR           , 16302));
    entries.push_back(make_pair(VS_SCALINGMETHOD_CUBIC            , 16303));
    entries.push_back(make_pair(VS_SCALINGMETHOD_LANCZOS2         , 16304));
    entries.push_back(make_pair(VS_SCALINGMETHOD_SPLINE36_FAST    , 16323));
    entries.push_back(make_pair(VS_SCALINGMETHOD_LANCZOS3_FAST    , 16315));
    entries.push_back(make_pair(VS_SCALINGMETHOD_SPLINE36         , 16322));
    entries.push_back(make_pair(VS_SCALINGMETHOD_LANCZOS3         , 16305));
    entries.push_back(make_pair(VS_SCALINGMETHOD_SINC8            , 16306));
//    entries.push_back(make_pair(VS_SCALINGMETHOD_NEDI             , ?????));
    entries.push_back(make_pair(VS_SCALINGMETHOD_BICUBIC_SOFTWARE , 16307));
    entries.push_back(make_pair(VS_SCALINGMETHOD_LANCZOS_SOFTWARE , 16308));
    entries.push_back(make_pair(VS_SCALINGMETHOD_SINC_SOFTWARE    , 16309));
    entries.push_back(make_pair(VS_SCALINGMETHOD_VDPAU_HARDWARE   , 13120));
    entries.push_back(make_pair(VS_SCALINGMETHOD_DXVA_HARDWARE    , 16319));
    entries.push_back(make_pair(VS_SCALINGMETHOD_AUTO             , 16316));

    /* remove unsupported methods */
    for(vector<pair<int, int> >::iterator it = entries.begin(); it != entries.end();)
    {
      if(g_renderManager.Supports((ESCALINGMETHOD)it->first))
        it++;
      else
        it = entries.erase(it);
    }

    AddSpin(VIDEO_SETTINGS_SCALINGMETHOD, 16300, (int*)&g_settings.m_currentVideoSettings.m_ScalingMethod, entries);
  }
#ifdef HAS_DS_PLAYER
  else if ( g_application.GetCurrentPlayer() == PCID_DSPLAYER )
  {
	  vector<pair<int, int> > entries;
	  entries.push_back(make_pair(DS_SCALINGMETHOD_NEAREST_NEIGHBOR  , 55005));
	  entries.push_back(make_pair(DS_SCALINGMETHOD_BILINEAR          , 55006));
	  entries.push_back(make_pair(DS_SCALINGMETHOD_BILINEAR_2        , 55007));
	  entries.push_back(make_pair(DS_SCALINGMETHOD_BILINEAR_2_60     , 55008));
	  entries.push_back(make_pair(DS_SCALINGMETHOD_BILINEAR_2_75     , 55009));
	  entries.push_back(make_pair(DS_SCALINGMETHOD_BILINEAR_2_100    , 55010));

	  m_scalingMethod = g_settings.m_currentVideoSettings.GetDSPlayerScalingMethod();
	  AddSpin(VIDEO_SETTINGS_SCALINGMETHOD, 16300, &m_scalingMethod, entries);

	  entries.clear();
	  entries.push_back(make_pair(DS_STATS_NONE, 55011));
	  entries.push_back(make_pair(DS_STATS_1, 55012));
	  entries.push_back(make_pair(DS_STATS_2, 55013));
	  entries.push_back(make_pair(DS_STATS_3, 55014));
	  AddSpin(VIDEO_SETTINGS_DS_STATS, 55015, (int *) &g_dsSettings.pRendererSettings->displayStats, entries);
  }
#endif
  if (g_renderManager.Supports(RENDERFEATURE_CROP))
    AddBool(VIDEO_SETTINGS_CROP, 644, &g_settings.m_currentVideoSettings.m_Crop);
  if (g_renderManager.Supports(RENDERFEATURE_STRETCH) || g_renderManager.Supports(RENDERFEATURE_PIXEL_RATIO))
  {
    const int entries[] = {630, 631, 632, 633, 634, 635, 636 };
    AddSpin(VIDEO_SETTINGS_VIEW_MODE, 629, &g_settings.m_currentVideoSettings.m_ViewMode, 7, entries);
  }
  if (g_renderManager.Supports(RENDERFEATURE_ZOOM))
    AddSlider(VIDEO_SETTINGS_ZOOM, 216, &g_settings.m_currentVideoSettings.m_CustomZoomAmount, 0.5f, 0.01f, 2.0f, FormatFloat);
  if (g_renderManager.Supports(RENDERFEATURE_VERTICAL_SHIFT))
    AddSlider(VIDEO_SETTINGS_VERTICAL_SHIFT, 225, &g_settings.m_currentVideoSettings.m_CustomVerticalShift, -2.0f, 0.01f, 2.0f, FormatFloat);
  if (g_renderManager.Supports(RENDERFEATURE_PIXEL_RATIO))
    AddSlider(VIDEO_SETTINGS_PIXEL_RATIO, 217, &g_settings.m_currentVideoSettings.m_CustomPixelRatio, 0.5f, 0.01f, 2.0f, FormatFloat);
  if (g_renderManager.Supports(RENDERFEATURE_POSTPROCESS))
    AddBool(VIDEO_SETTINGS_POSTPROCESS, 16400, &g_settings.m_currentVideoSettings.m_PostProcess);

#ifdef HAS_VIDEO_PLAYBACK
  if (g_renderManager.Supports(RENDERFEATURE_BRIGHTNESS))
    AddSlider(VIDEO_SETTINGS_BRIGHTNESS, 464, &g_settings.m_currentVideoSettings.m_Brightness, 0, 1, 100, FormatInteger);
  if (g_renderManager.Supports(RENDERFEATURE_CONTRAST))
    AddSlider(VIDEO_SETTINGS_CONTRAST, 465, &g_settings.m_currentVideoSettings.m_Contrast, 0, 1, 100, FormatInteger);
  if (g_renderManager.Supports(RENDERFEATURE_GAMMA))
    AddSlider(VIDEO_SETTINGS_GAMMA, 466, &g_settings.m_currentVideoSettings.m_Gamma, 0, 1, 100, FormatInteger);
#ifdef HAS_DS_PLAYER
  if (g_application.GetCurrentPlayer() == PCID_DSPLAYER)
  {
	  SettingInfo setting;
	  setting.type = SettingInfo::BUTTON;
	  setting.id = VIDEO_SETTINGS_DS_FILTERS;

	  BeginEnumFilters(g_dsGraph->pFilterGraph, pEF, pBF)
	  {
		  if ((pBF == CGraphFilters::Get()->AudioRenderer.pBF && CGraphFilters::Get()->AudioRenderer.guid != CLSID_ReClock) || pBF == CGraphFilters::Get()->VideoRenderer.pBF )
			  continue;

		  Com::SmartQIPtr<ISpecifyPropertyPages> pProp = pBF;
		  CAUUID pPages;
		  if ( pProp )
		  {
			  pProp->GetPages(&pPages);
			  if (pPages.cElems > 0)
			  {
				  g_charsetConverter.wToUTF8(GetFilterName(pBF), setting.name);
				  m_settings.push_back(setting);
			  }
			  CoTaskMemFree(pPages.pElems);
		  } 
	  }
	  EndEnumFilters
  }
#endif
  if (g_renderManager.Supports(RENDERFEATURE_NOISE))
    AddSlider(VIDEO_SETTING_VDPAU_NOISE, 16312, &g_settings.m_currentVideoSettings.m_NoiseReduction, 0.0f, 0.01f, 1.0f, FormatFloat);
  if (g_renderManager.Supports(RENDERFEATURE_SHARPNESS))
    AddSlider(VIDEO_SETTING_VDPAU_SHARPNESS, 16313, &g_settings.m_currentVideoSettings.m_Sharpness, -1.0f, 0.02f, 1.0f, FormatFloat);
  if (g_renderManager.Supports(RENDERFEATURE_NONLINSTRETCH))
    AddBool(VIDEO_SETTINGS_NONLIN_STRETCH, 659, &g_settings.m_currentVideoSettings.m_CustomNonLinStretch);
#endif
  AddSeparator(8);
  AddButton(VIDEO_SETTINGS_MAKE_DEFAULT, 12376);
  AddButton(VIDEO_SETTINGS_CALIBRATION, 214);
}

void CGUIDialogVideoSettings::OnSettingChanged(SettingInfo &setting)
{
  // check and update anything that needs it
#ifdef HAS_VIDEO_PLAYBACK
  if (setting.id == VIDEO_SETTINGS_CROP)
  {
    // AutoCrop changes will get picked up automatically by dvdplayer
  }
  else if (setting.id == VIDEO_SETTINGS_VIEW_MODE)
  {
    g_renderManager.SetViewMode(g_settings.m_currentVideoSettings.m_ViewMode);
    UpdateSetting(VIDEO_SETTINGS_ZOOM);
    UpdateSetting(VIDEO_SETTINGS_PIXEL_RATIO);
    UpdateSetting(VIDEO_SETTINGS_NONLIN_STRETCH);
    UpdateSetting(VIDEO_SETTINGS_VERTICAL_SHIFT);
  }
  else if (setting.id == VIDEO_SETTINGS_ZOOM || setting.id == VIDEO_SETTINGS_PIXEL_RATIO
        || setting.id == VIDEO_SETTINGS_NONLIN_STRETCH
        || setting.id == VIDEO_SETTINGS_VERTICAL_SHIFT)
  {
    g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
    g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
    UpdateSetting(VIDEO_SETTINGS_VIEW_MODE);
  }
#ifdef HAS_DS_PLAYER
  else if (setting.id == VIDEO_SETTINGS_SCALINGMETHOD)
  {
	  if (g_application.GetCurrentPlayer() == PCID_DSPLAYER)
		  g_settings.m_currentVideoSettings.SetDSPlayerScalingMethod((EDSSCALINGMETHOD) m_scalingMethod);
  }
#endif
  else
#endif
  if (setting.id == VIDEO_SETTINGS_CALIBRATION)
  {
    // launch calibration window
    if (g_settings.GetCurrentProfile().settingsLocked() && g_settings.GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE)
      if (!g_passwordManager.IsMasterLockUnlocked(true))
        return;
    g_windowManager.ActivateWindow(WINDOW_SCREEN_CALIBRATION);
  }
  else if (setting.id == VIDEO_SETTINGS_MAKE_DEFAULT)
  {
    if (g_settings.GetCurrentProfile().settingsLocked() && g_settings.GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE)
      if (!g_passwordManager.IsMasterLockUnlocked(true))
        return;

    // prompt user if they are sure
    if (CGUIDialogYesNo::ShowAndGetInput(12376, 750, 0, 12377))
    { // reset the settings
      CVideoDatabase db;
      db.Open();
      db.EraseVideoSettings();
      db.Close();
      g_settings.m_defaultVideoSettings = g_settings.m_currentVideoSettings;
      g_settings.m_defaultVideoSettings.m_SubtitleStream = -1;
      g_settings.m_defaultVideoSettings.m_AudioStream = -1;
      g_settings.Save();
    }
  }
#ifdef HAS_DS_PLAYER
  else if (setting.id  == VIDEO_SETTINGS_DS_FILTERS)
  {
	  IBaseFilter *pBF = NULL;
	  CStdStringW strNameW;
	  g_charsetConverter.utf8ToW(setting.name, strNameW);
	  if(SUCCEEDED(g_dsGraph->pFilterGraph->FindFilterByName(strNameW, &pBF)))
	  {
		  //Showing the property page for this filter
		  m_pDSPropertyPage = new CDSPropertyPage(pBF);
		  m_pDSPropertyPage->Initialize();
	  }
  }
#endif
  else if (setting.id == VIDEO_SETTINGS_DEINTERLACEMODE)
  {
    EnableSettings(VIDEO_SETTINGS_INTERLACEMETHOD, g_settings.m_currentVideoSettings.m_DeinterlaceMode != VS_DEINTERLACEMODE_OFF);
  }

  if (g_PVRManager.IsPlayingRadio() || g_PVRManager.IsPlayingTV())
    g_PVRManager.TriggerSaveChannelSettings();
}

CStdString CGUIDialogVideoSettings::FormatInteger(float value, float minimum)
{
  CStdString text;
  text.Format("%i", MathUtils::round_int(value));
  return text;
}

CStdString CGUIDialogVideoSettings::FormatFloat(float value, float minimum)
{
  CStdString text;
  text.Format("%2.2f", value);
  return text;
}


