#pragma once

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

#include "settings/GUIDialogSettings.h"
#include "DSPropertyPage.h"
class CGUIDialogVideoSettings :
      public CGUIDialogSettings
{
public:
  CGUIDialogVideoSettings(void);
  virtual ~CGUIDialogVideoSettings(void);

  static CStdString FormatInteger(float value, float minimum);
  static CStdString FormatFloat(float value, float minimum);

protected:
  virtual void CreateSettings();
  virtual void OnSettingChanged(SettingInfo &setting);

#ifdef HAS_DS_PLAYER
private:
	CDSPropertyPage* m_pDSPropertyPage;
	int m_scalingMethod;
#endif
};

