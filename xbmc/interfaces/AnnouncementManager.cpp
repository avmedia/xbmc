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

#include "AnnouncementManager.h"
#include "threads/SingleLock.h"
#include <stdio.h>
#include "utils/log.h"
#include "utils/Variant.h"
#include "utils/StringUtils.h"
#include "FileItem.h"
#include "music/tags/MusicInfoTag.h"
#include "music/MusicDatabase.h"
#include "video/VideoDatabase.h"

#define LOOKUP_PROPERTY "database-lookup"

using namespace std;
using namespace ANNOUNCEMENT;

#define m_announcers XBMC_GLOBAL_USE(ANNOUNCEMENT::CAnnouncementManager::Globals).m_announcers
#define m_critSection XBMC_GLOBAL_USE(ANNOUNCEMENT::CAnnouncementManager::Globals).m_critSection

void CAnnouncementManager::AddAnnouncer(IAnnouncer *listener)
{
  if (!listener)
    return;

  CSingleLock lock (m_critSection);
  m_announcers.push_back(listener);
}

void CAnnouncementManager::RemoveAnnouncer(IAnnouncer *listener)
{
  if (!listener)
    return;

  CSingleLock lock (m_critSection);
  for (unsigned int i = 0; i < m_announcers.size(); i++)
  {
    if (m_announcers[i] == listener)
    {
      m_announcers.erase(m_announcers.begin() + i);
      return;
    }
  }
}

void CAnnouncementManager::Announce(AnnouncementFlag flag, const char *sender, const char *message)
{
  CVariant data;
  Announce(flag, sender, message, data);
}

void CAnnouncementManager::Announce(AnnouncementFlag flag, const char *sender, const char *message, CVariant &data)
{
  CLog::Log(LOGDEBUG, "CAnnouncementManager - Announcement: %s from %s", message, sender);
  CSingleLock lock (m_critSection);
  for (unsigned int i = 0; i < m_announcers.size(); i++)
    m_announcers[i]->Announce(flag, sender, message, data);
}

void CAnnouncementManager::Announce(AnnouncementFlag flag, const char *sender, const char *message, CFileItemPtr item)
{
  CVariant data;
  Announce(flag, sender, message, item, data);
}

void CAnnouncementManager::Announce(AnnouncementFlag flag, const char *sender, const char *message, CFileItemPtr item, CVariant &data)
{
  if (!item.get())
  {
    Announce(flag, sender, message, data);
    return;
  }

  // Extract db id of item
  CVariant object = data.isNull() || data.isObject() ? data : CVariant::VariantTypeObject;
  CStdString type;
  int id = 0;

  if (item->HasVideoInfoTag())
  {
    id = item->GetVideoInfoTag()->m_iDbId;

    // TODO: Can be removed once this is properly handled when starting playback of a file
    if (id <= 0 && !item->GetPath().empty() &&
       (!item->HasProperty(LOOKUP_PROPERTY) || item->GetProperty(LOOKUP_PROPERTY).asBoolean()))
    {
      CVideoDatabase videodatabase;
      if (videodatabase.Open())
      {
        if (videodatabase.LoadVideoInfo(item->GetPath(), *item->GetVideoInfoTag()))
          id = item->GetVideoInfoTag()->m_iDbId;

        videodatabase.Close();
      }
    }

    CVideoDatabase::VideoContentTypeToString((VIDEODB_CONTENT_TYPE)item->GetVideoContentType(), type);

    if (id <= 0)
    {
      // TODO: Can be removed once this is properly handled when starting playback of a file
      item->SetProperty(LOOKUP_PROPERTY, false);

      object["title"] = item->GetVideoInfoTag()->m_strTitle;

      switch (item->GetVideoContentType())
      {
      case VIDEODB_CONTENT_MOVIES:
        if (item->GetVideoInfoTag()->m_iYear > 0)
          object["year"] = item->GetVideoInfoTag()->m_iYear;
        break;
      case VIDEODB_CONTENT_EPISODES:
        if (item->GetVideoInfoTag()->m_iEpisode >= 0)
          object["episode"] = item->GetVideoInfoTag()->m_iEpisode;
        if (item->GetVideoInfoTag()->m_iSeason >= 0)
          object["season"] = item->GetVideoInfoTag()->m_iSeason;
        if (!item->GetVideoInfoTag()->m_strShowTitle.empty())
          object["showtitle"] = item->GetVideoInfoTag()->m_strShowTitle;
        break;
      case VIDEODB_CONTENT_MUSICVIDEOS:
        if (!item->GetVideoInfoTag()->m_strAlbum.empty())
          object["album"] = item->GetVideoInfoTag()->m_strAlbum;
        if (!item->GetVideoInfoTag()->m_artist.empty())
          object["artist"] = StringUtils::Join(item->GetVideoInfoTag()->m_artist, " / ");
        break;
      }
    }
  }
  else if (item->HasMusicInfoTag())
  {
    id = item->GetMusicInfoTag()->GetDatabaseId();
    type = "song";

    // TODO: Can be removed once this is properly handled when starting playback of a file
    if (id <= 0 && !item->GetPath().empty() &&
       (!item->HasProperty(LOOKUP_PROPERTY) || item->GetProperty(LOOKUP_PROPERTY).asBoolean()))
    {
      CMusicDatabase musicdatabase;
      if (musicdatabase.Open())
      {
        CSong song;
        if (musicdatabase.GetSongByFileName(item->GetPath(), song, item->m_lStartOffset))
        {
          item->GetMusicInfoTag()->SetSong(song);
          id = item->GetMusicInfoTag()->GetDatabaseId();
        }

        musicdatabase.Close();
      }
    }

    if (id <= 0)
    {
      // TODO: Can be removed once this is properly handled when starting playback of a file
      item->SetProperty(LOOKUP_PROPERTY, false);

      object["title"] = item->GetMusicInfoTag()->GetTitle();

      if (item->GetMusicInfoTag()->GetTrackNumber() > 0)
        object["track"] = item->GetMusicInfoTag()->GetTrackNumber();
      if (!item->GetMusicInfoTag()->GetAlbum().empty())
        object["album"] = item->GetMusicInfoTag()->GetAlbum();
      if (!item->GetMusicInfoTag()->GetArtist().empty())
        object["artist"] = item->GetMusicInfoTag()->GetArtist();
    }
  }
  else if (item->HasPictureInfoTag())
  {
    type = "picture";
    object["file"] = item->GetPath();
  }
  else
    type = "unknown";

  object["item"]["type"] = type;
  if (id > 0)
    object["item"]["id"] = id;

  Announce(flag, sender, message, object);
}
