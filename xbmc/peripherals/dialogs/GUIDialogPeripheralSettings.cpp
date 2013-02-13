/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include "GUIDialogPeripheralSettings.h"
#include "addons/Skin.h"
#include "peripherals/Peripherals.h"
#include "settings/GUISettings.h"
#include "utils/log.h"
#include "video/dialogs/GUIDialogVideoSettings.h"

using namespace std;
using namespace PERIPHERALS;

#define BUTTON_DEFAULTS 50

CGUIDialogPeripheralSettings::CGUIDialogPeripheralSettings(void) :
    CGUIDialogSettings(WINDOW_DIALOG_PERIPHERAL_SETTINGS, "DialogPeripheralSettings.xml"),
    m_item(NULL),
    m_bIsInitialising(true)
{
}

CGUIDialogPeripheralSettings::~CGUIDialogPeripheralSettings(void)
{
  if (m_item)
    delete m_item;
}

void CGUIDialogPeripheralSettings::SetFileItem(CFileItemPtr item)
{
  if (m_item)
  {
    delete m_item;
    m_boolSettings.clear();
    m_intSettings.clear();
    m_intTextSettings.clear();
    m_floatSettings.clear();
    m_stringSettings.clear();
    m_settings.clear();
  }

  m_item = new CFileItem(*item.get());
}

void CGUIDialogPeripheralSettings::CreateSettings()
{
  m_bIsInitialising = true;
  m_usePopupSliders = g_SkinInfo->HasSkinFile("DialogSlider.xml");

  if (m_item)
  {
    CPeripheral *peripheral = g_peripherals.GetByPath(m_item->GetPath());
    if (peripheral)
    {
      vector<CSetting *> settings = peripheral->GetSettings();
      for (size_t iPtr = 0; iPtr < settings.size(); iPtr++)
      {
        CSetting *setting = settings[iPtr];
        if (!setting->IsVisible())
        {
          CLog::Log(LOGDEBUG, "%s - invisible", __FUNCTION__);
          continue;
        }

        switch(setting->GetType())
        {
        case SETTINGS_TYPE_BOOL:
          {
            CSettingBool *boolSetting = (CSettingBool *) setting;
            if (boolSetting)
            {
              m_boolSettings.insert(make_pair(CStdString(boolSetting->GetSetting()), boolSetting->GetData()));
              AddBool(boolSetting->GetOrder(), boolSetting->GetLabel(), &m_boolSettings[boolSetting->GetSetting()], true);
            }
          }
          break;
        case SETTINGS_TYPE_INT:
          {
            CSettingInt *intSetting = (CSettingInt *) setting;
            if (intSetting)
            {
              if (intSetting->GetControlType() == SPIN_CONTROL_INT)
              {
                m_intSettings.insert(make_pair(CStdString(intSetting->GetSetting()), (float) intSetting->GetData()));
                AddSlider(intSetting->GetOrder(), intSetting->GetLabel(), &m_intSettings[intSetting->GetSetting()], (float)intSetting->m_iMin, (float)intSetting->m_iStep, (float)intSetting->m_iMax, CGUIDialogVideoSettings::FormatInteger, false);
              }
              else if (intSetting->GetControlType() == SPIN_CONTROL_TEXT)
              {
                m_intTextSettings.insert(make_pair(CStdString(intSetting->GetSetting()), intSetting->GetData()));
                vector<pair<int, int> > entries;
                map<int, int>::iterator entriesItr = intSetting->m_entries.begin();
                while (entriesItr != intSetting->m_entries.end())
                {
                  entries.push_back(make_pair(entriesItr->first, entriesItr->second));
                  ++entriesItr;
                }
                AddSpin(intSetting->GetOrder(), intSetting->GetLabel(), &m_intTextSettings[intSetting->GetSetting()], entries);
              }
            }
          }
          break;
        case SETTINGS_TYPE_FLOAT:
          {
            CSettingFloat *floatSetting = (CSettingFloat *) setting;
            if (floatSetting)
            {
              m_floatSettings.insert(make_pair(CStdString(floatSetting->GetSetting()), floatSetting->GetData()));
              AddSlider(floatSetting->GetOrder(), floatSetting->GetLabel(), &m_floatSettings[floatSetting->GetSetting()], floatSetting->m_fMin, floatSetting->m_fStep, floatSetting->m_fMax, CGUIDialogVideoSettings::FormatFloat, false);
            }
          }
          break;
        case SETTINGS_TYPE_STRING:
          {
            CSettingString *stringSetting = (CSettingString *) setting;
            if (stringSetting)
            {
              m_stringSettings.insert(make_pair(CStdString(stringSetting->GetSetting()), stringSetting->GetData()));
              AddString(stringSetting->GetOrder(), stringSetting->GetLabel(), &m_stringSettings[stringSetting->GetSetting()]);
            }
          }
          break;
        default:
          //TODO add more types if needed
          CLog::Log(LOGDEBUG, "%s - unknown type", __FUNCTION__);
          break;
        }
      }
    }
    else
    {
      CLog::Log(LOGDEBUG, "%s - no peripheral", __FUNCTION__);
    }
  }

  m_bIsInitialising = false;
}

void CGUIDialogPeripheralSettings::UpdatePeripheralSettings(void)
{
  if (!m_item || m_bIsInitialising)
    return;

  CPeripheral *peripheral = g_peripherals.GetByPath(m_item->GetPath());
  if (!peripheral)
    return;

  map<CStdString, bool>::iterator boolItr = m_boolSettings.begin();
  while (boolItr != m_boolSettings.end())
  {
    peripheral->SetSetting((*boolItr).first, (*boolItr).second);
    ++boolItr;
  }

  map<CStdString, float>::iterator intItr = m_intSettings.begin();
  while (intItr != m_intSettings.end())
  {
    peripheral->SetSetting((*intItr).first, (int) (*intItr).second);
    ++intItr;
  }

  map<CStdString, int>::iterator intTextItr = m_intTextSettings.begin();
  while (intTextItr != m_intTextSettings.end())
  {
    peripheral->SetSetting((*intTextItr).first, (*intTextItr).second);
    ++intTextItr;
  }

  map<CStdString, float>::iterator floatItr = m_floatSettings.begin();
  while (floatItr != m_floatSettings.end())
  {
    peripheral->SetSetting((*floatItr).first, (*floatItr).second);
    ++floatItr;
  }

  map<CStdString, CStdString>::iterator stringItr = m_stringSettings.begin();
  while (stringItr != m_stringSettings.end())
  {
    peripheral->SetSetting((*stringItr).first, (*stringItr).second);
    ++stringItr;
  }

  peripheral->PersistSettings();
}

bool CGUIDialogPeripheralSettings::OnMessage(CGUIMessage &message)
{
  if (message.GetMessage() == GUI_MSG_CLICKED &&
      message.GetSenderId() == BUTTON_DEFAULTS)
  {
    ResetDefaultSettings();
    return true;
  }

  return CGUIDialogSettings::OnMessage(message);
}

void CGUIDialogPeripheralSettings::OnOkay(void)
{
  UpdatePeripheralSettings();
}

void CGUIDialogPeripheralSettings::ResetDefaultSettings(void)
{
  if (m_item)
  {
    CPeripheral *peripheral = g_peripherals.GetByPath(m_item->GetPath());
    if (!peripheral)
      return;

    /* reset the settings in the peripheral */
    peripheral->ResetDefaultSettings();

    CSingleLock lock(g_graphicsContext);

    /* clear the settings */
    m_boolSettings.clear();
    m_intSettings.clear();
    m_intTextSettings.clear();
    m_floatSettings.clear();
    m_stringSettings.clear();
    m_settings.clear();

    /* reinit the window */
    CreateSettings();
    SetupPage(); // will clear the previous controls first
  }
}
