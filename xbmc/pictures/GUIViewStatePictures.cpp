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

#include "GUIViewStatePictures.h"
#include "FileItem.h"
#include "view/ViewState.h"
#include "settings/GUISettings.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSourceSettings.h"
#include "filesystem/Directory.h"
#include "filesystem/PluginDirectory.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "view/ViewStateSettings.h"

using namespace XFILE;
using namespace ADDON;

CGUIViewStateWindowPictures::CGUIViewStateWindowPictures(const CFileItemList& items) : CGUIViewState(items)
{
  if (items.IsVirtualDirectoryRoot())
  {
    AddSortMethod(SORT_METHOD_LABEL, 551, LABEL_MASKS());
    AddSortMethod(SORT_METHOD_DRIVE_TYPE, 564, LABEL_MASKS());
    SetSortMethod(SORT_METHOD_LABEL);

    SetViewAsControl(DEFAULT_VIEW_LIST);

    SetSortOrder(SortOrderAscending);
  }
  else
  {
    AddSortMethod(SORT_METHOD_LABEL, 551, LABEL_MASKS("%L", "%I", "%L", ""));  // Filename, Size | Foldername, empty
    AddSortMethod(SORT_METHOD_SIZE, 553, LABEL_MASKS("%L", "%I", "%L", "%I"));  // Filename, Size | Foldername, Size
    AddSortMethod(SORT_METHOD_DATE, 552, LABEL_MASKS("%L", "%J", "%L", "%J"));  // Filename, Date | Foldername, Date
    AddSortMethod(SORT_METHOD_DATE_TAKEN, 577, LABEL_MASKS("%L", "%t", "%L", "%J"));  // Filename, DateTaken | Foldername, Date
    AddSortMethod(SORT_METHOD_FILE, 561, LABEL_MASKS("%L", "%I", "%L", ""));  // Filename, Size | FolderName, empty
    
    CViewState *viewState = CViewStateSettings::Get().Get("pictures");
    SetSortMethod(viewState->m_sortMethod);
    SetViewAsControl(viewState->m_viewMode);
    SetSortOrder(viewState->m_sortOrder);
  }
  LoadViewState(items.GetPath(), WINDOW_PICTURES);
}

void CGUIViewStateWindowPictures::SaveViewState()
{
  SaveViewToDb(m_items.GetPath(), WINDOW_PICTURES, CViewStateSettings::Get().Get("pictures"));
}

CStdString CGUIViewStateWindowPictures::GetLockType()
{
  return "pictures";
}

CStdString CGUIViewStateWindowPictures::GetExtensions()
{
  if (g_guiSettings.GetBool("pictures.showvideos"))
    return g_advancedSettings.m_pictureExtensions+"|"+g_advancedSettings.m_videoExtensions;

  return g_advancedSettings.m_pictureExtensions;
}

VECSOURCES& CGUIViewStateWindowPictures::GetSources()
{
  VECSOURCES *pictureSources = CMediaSourceSettings::Get().GetSources("pictures");
  AddAddonsSource("image", g_localizeStrings.Get(1039), "DefaultAddonPicture.png");
  AddOrReplace(*pictureSources, CGUIViewState::GetSources());
  return *pictureSources;
}

