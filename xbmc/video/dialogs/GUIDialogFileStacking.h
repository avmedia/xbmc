#pragma once

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

#include "guilib/GUIDialog.h"

class CFileItemList;

class CGUIDialogFileStacking :
      public CGUIDialog
{
public:
  CGUIDialogFileStacking(void);
  virtual ~CGUIDialogFileStacking(void);
  virtual bool OnMessage(CGUIMessage& message);

  int GetSelectedFile() const;
  void SetNumberOfFiles(int iFiles);
protected:
  virtual void OnInitWindow();
  int m_iSelectedFile;
  int m_iNumberOfFiles;
  CFileItemList* m_stackItems;
};
