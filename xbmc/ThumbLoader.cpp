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

#include "filesystem/StackDirectory.h"
#include "ThumbLoader.h"
#include "utils/URIUtils.h"
#include "URL.h"
#include "filesystem/File.h"
#include "filesystem/DirectoryCache.h"
#include "FileItem.h"
#include "settings/GUISettings.h"
#include "GUIUserMessages.h"
#include "guilib/GUIWindowManager.h"
#include "TextureCache.h"
#include "utils/log.h"
#include "video/VideoInfoTag.h"
#include "video/VideoDatabase.h"
#include "cores/dvdplayer/DVDFileInfo.h"
#include "video/VideoInfoScanner.h"
#include "music/tags/MusicInfoTag.h"
#include "music/tags/MusicInfoTagLoaderFactory.h"
#include "music/infoscanner/MusicInfoScanner.h"
#include "music/Artist.h"

using namespace XFILE;
using namespace std;
using namespace VIDEO;
using namespace MUSIC_INFO;

CThumbLoader::CThumbLoader(int nThreads) :
  CBackgroundInfoLoader(nThreads)
{
}

CThumbLoader::~CThumbLoader()
{
}

CStdString CThumbLoader::GetCachedImage(const CFileItem &item, const CStdString &type)
{
  CTextureDatabase db;
  if (db.Open())
    return db.GetTextureForPath(item.GetPath(), type);
  return "";
}

void CThumbLoader::SetCachedImage(const CFileItem &item, const CStdString &type, const CStdString &image)
{
  CTextureDatabase db;
  if (db.Open())
    db.SetTextureForPath(item.GetPath(), type, image);
}

CThumbExtractor::CThumbExtractor(const CFileItem& item, const CStdString& listpath, bool thumb, const CStdString& target)
{
  m_listpath = listpath;
  m_target = target;
  m_thumb = thumb;
  m_item = item;

  m_path = item.GetPath();

  if (item.IsVideoDb() && item.HasVideoInfoTag())
    m_path = item.GetVideoInfoTag()->m_strFileNameAndPath;

  if (URIUtils::IsStack(m_path))
    m_path = CStackDirectory::GetFirstStackedFile(m_path);
}

CThumbExtractor::~CThumbExtractor()
{
}

bool CThumbExtractor::operator==(const CJob* job) const
{
  if (strcmp(job->GetType(),GetType()) == 0)
  {
    const CThumbExtractor* jobExtract = dynamic_cast<const CThumbExtractor*>(job);
    if (jobExtract && jobExtract->m_listpath == m_listpath)
      return true;
  }
  return false;
}

bool CThumbExtractor::DoWork()
{
  if (URIUtils::IsLiveTV(m_path)
  ||  URIUtils::IsUPnP(m_path)
  ||  URIUtils::IsDAAP(m_path)
  ||  m_item.IsDVD()
  ||  m_item.IsDVDImage()
  ||  m_item.IsDVDFile(false, true)
  ||  m_item.IsInternetStream()
  ||  m_item.IsDiscStub()
  ||  m_item.IsPlayList())
    return false;

  if (URIUtils::IsRemote(m_path) && !URIUtils::IsOnLAN(m_path))
    return false;

  bool result=false;
  if (m_thumb)
  {
    CLog::Log(LOGDEBUG,"%s - trying to extract thumb from video file %s", __FUNCTION__, m_path.c_str());
    // construct the thumb cache file
    CTextureDetails details;
    details.file = CTextureCache::GetCacheFile(m_target) + ".jpg";
    result = CDVDFileInfo::ExtractThumb(m_path, details, &m_item.GetVideoInfoTag()->m_streamDetails);
    if(result)
    {
      CTextureCache::Get().AddCachedTexture(m_target, details);
      m_item.SetProperty("HasAutoThumb", true);
      m_item.SetProperty("AutoThumbImage", m_target);
      m_item.SetThumbnailImage(CTextureCache::GetCachedPath(details.file));
    }
  }
  else if (m_item.HasVideoInfoTag() && !m_item.GetVideoInfoTag()->HasStreamDetails())
  {
    CLog::Log(LOGDEBUG,"%s - trying to extract filestream details from video file %s", __FUNCTION__, m_path.c_str());
    result = CDVDFileInfo::GetFileStreamDetails(&m_item);
  }

  return result;
}

CVideoThumbLoader::CVideoThumbLoader() :
  CThumbLoader(1), CJobQueue(true), m_pStreamDetailsObs(NULL)
{
  m_database = new CVideoDatabase();
}

CVideoThumbLoader::~CVideoThumbLoader()
{
  StopThread();
  delete m_database;
}

void CVideoThumbLoader::Initialize()
{
  m_database->Open();
}

void CVideoThumbLoader::OnLoaderStart()
{
  Initialize();
}

void CVideoThumbLoader::OnLoaderFinish()
{
  m_database->Close();
}

static void SetupRarOptions(CFileItem& item, const CStdString& path)
{
  CStdString path2(path);
  if (item.IsVideoDb() && item.HasVideoInfoTag())
    path2 = item.GetVideoInfoTag()->m_strFileNameAndPath;
  CURL url(path2);
  CStdString opts = url.GetOptions();
  if (opts.Find("flags") > -1)
    return;
  if (opts.size())
    opts += "&flags=8";
  else
    opts = "?flags=8";
  url.SetOptions(opts);
  if (item.IsVideoDb() && item.HasVideoInfoTag())
    item.GetVideoInfoTag()->m_strFileNameAndPath = url.Get();
  else
    item.SetPath(url.Get());
  g_directoryCache.ClearDirectory(url.GetWithoutFilename());
}

/**
 * Look for a thumbnail for pItem.  If one does not exist, look for an autogenerated
 * thumbnail.  If that does not exist, attempt to autogenerate one.  Finally, check
 * for the existance of fanart and set properties accordingly.
 * @return: true if pItem has been modified
 */
bool CVideoThumbLoader::LoadItem(CFileItem* pItem)
{
  if (pItem->m_bIsShareOrDrive
  ||  pItem->IsParentFolder())
    return false;

  m_database->Open();

  if (pItem->HasVideoInfoTag() && !pItem->GetVideoInfoTag()->HasStreamDetails() &&
     (pItem->GetVideoInfoTag()->m_type == "movie" || pItem->GetVideoInfoTag()->m_type == "episode" || pItem->GetVideoInfoTag()->m_type == "musicvideo"))
  {
    if (m_database->GetStreamDetails(*pItem->GetVideoInfoTag()))
      pItem->SetInvalid();
  }

  // video db items normally have info in the database
  if (pItem->HasVideoInfoTag() && pItem->GetArt().empty())
  {
    FillLibraryArt(*pItem);

    if (pItem->GetVideoInfoTag()->m_type == "set"      ||
        pItem->GetVideoInfoTag()->m_type == "actor"    ||
        pItem->GetVideoInfoTag()->m_type == "artist"   ||
        pItem->GetVideoInfoTag()->m_type == "director" ||
        pItem->GetVideoInfoTag()->m_type == "writer"   ||
        pItem->GetVideoInfoTag()->m_type == "season"   )
    {
      m_database->Close();
      return true; // nothing else to be done
    }
  }

  // fanart
  if (!pItem->HasProperty("fanart_image"))
  {
    CStdString fanart = GetCachedImage(*pItem, "fanart");
    if (fanart.IsEmpty())
    {
      fanart = pItem->GetLocalFanart();
      if (!fanart.IsEmpty()) // cache it
        SetCachedImage(*pItem, "fanart", fanart);
    }
    if (!fanart.IsEmpty())
    {
      CTextureCache::Get().BackgroundCacheImage(fanart);
      pItem->SetProperty("fanart_image", fanart);
    }
  }

  // thumbnails
  if (!pItem->HasThumbnail())
  {
    FillThumb(*pItem);
    if (!pItem->HasThumbnail() && !pItem->m_bIsFolder && pItem->IsVideo())
    {
      // create unique thumb for auto generated thumbs
      CStdString thumbURL = GetEmbeddedThumbURL(*pItem);
      if (CTextureCache::Get().HasCachedImage(thumbURL))
      {
        CTextureCache::Get().BackgroundCacheImage(thumbURL);
        pItem->SetProperty("HasAutoThumb", true);
        pItem->SetProperty("AutoThumbImage", thumbURL);
        pItem->SetThumbnailImage(thumbURL);
      }
      else if (g_guiSettings.GetBool("myvideos.extractthumb") &&
               g_guiSettings.GetBool("myvideos.extractflags"))
      {
        CFileItem item(*pItem);
        CStdString path(item.GetPath());
        if (URIUtils::IsInRAR(item.GetPath()))
          SetupRarOptions(item,path);

        CThumbExtractor* extract = new CThumbExtractor(item, path, true, thumbURL);
        AddJob(extract);

        m_database->Close();
        return true;
      }
    }
  }

  // flag extraction
  if (!pItem->m_bIsFolder &&
       pItem->HasVideoInfoTag() &&
       g_guiSettings.GetBool("myvideos.extractflags") &&
       (!pItem->GetVideoInfoTag()->HasStreamDetails() ||
         pItem->GetVideoInfoTag()->m_streamDetails.GetVideoDuration() <= 0))
  {
    CFileItem item(*pItem);
    CStdString path(item.GetPath());
    if (URIUtils::IsInRAR(item.GetPath()))
      SetupRarOptions(item,path);
    CThumbExtractor* extract = new CThumbExtractor(item,path,false);
    AddJob(extract);
  }

  m_database->Close();
  return true;
}

bool CVideoThumbLoader::FillLibraryArt(CFileItem &item)
{
  CVideoInfoTag &tag = *item.GetVideoInfoTag();
  if (tag.m_iDbId > -1 && !tag.m_type.IsEmpty())
  {
    map<string, string> artwork;
    m_database->Open();
    if (m_database->GetArtForItem(tag.m_iDbId, tag.m_type, artwork))
      item.SetArt(artwork);
    else if (tag.m_type == "artist")
    { // we retrieve music video art from the music database (no backward compat)
      CMusicDatabase database;
      database.Open();
      int idArtist = database.GetArtistByName(item.GetLabel());
      if (database.GetArtForItem(idArtist, "artist", artwork))
        item.SetArt(artwork);
    }
    else if (tag.m_type == "album")
    { // we retrieve music video art from the music database (no backward compat)
      CMusicDatabase database;
      database.Open();
      int idAlbum = database.GetAlbumByName(item.GetLabel(), tag.m_artist);
      if (database.GetArtForItem(idAlbum, "album", artwork))
        item.SetArt(artwork);
    }
    else
    {
      if (tag.m_type == "movie"  || tag.m_type == "episode" ||
          tag.m_type == "tvshow" || tag.m_type == "musicvideo")
      { // no art in the library, so find it locally and add
        SScanSettings settings;
        ADDON::ScraperPtr info = m_database->GetScraperForPath(tag.m_strPath, settings);
        if (info)
        {
          CFileItem tmpItem(item);
          tmpItem.SetPath(tag.GetPath());
          CVideoInfoScanner scanner;
          scanner.GetArtwork(&tmpItem, info->Content(), tag.m_type != "episode" && settings.parent_name_root, true);
          item.SetArt(tmpItem.GetArt());
        }
      }
      else if (tag.m_type == "set")
      { // no art for a set -> use the first movie for this set for art
        CFileItemList items;
        if (m_database->GetMoviesNav("videodb://1/2/", items, -1, -1, -1, -1, -1, -1, tag.m_iDbId) && items.Size() > 0)
        {
          LoadItem(items[0].get());
          if (!items[0]->GetArt().empty())
            item.SetArt(items[0]->GetArt());
        }
      }
      else if (tag.m_type == "actor"  ||
               tag.m_type == "writer" || tag.m_type == "director")
      {
        // We can't realistically get the local thumbs (as we'd need to check every movie that contains this actor)
        // and most users won't have local actor thumbs that are actually different than the scraped ones.
        if (g_guiSettings.GetBool("videolibrary.actorthumbs"))
        {
          tag.m_strPictureURL.Parse();
          CStdString thumb = CScraperUrl::GetThumbURL(tag.m_strPictureURL.GetFirstThumb());
          if (!thumb.IsEmpty())
            item.SetThumbnailImage(thumb);
        }
      }
      else if (tag.m_type == "season")
      {
        // season art is fetched on scan from the tvshow root path (m_strPath in the season info tag)
        // or from the show m_strPictureURL member of the tvshow, so grab the tvshow to get this.
        CVideoInfoTag show;
        m_database->GetTvShowInfo(tag.m_strPath, show, tag.m_iIdShow);
        map<int, string> seasons;
        CVideoInfoScanner::GetSeasonThumbs(show, seasons, true);
        map<int, string>::iterator season = seasons.find(tag.m_iSeason);
        if (season != seasons.end())
          item.SetThumbnailImage(season->second);
      }
      // add to the database for next time around
      map<string, string> artwork = item.GetArt();
      if (!artwork.empty())
      {
        m_database->SetArtForItem(tag.m_iDbId, tag.m_type, artwork);
        for (map<string, string>::iterator i = artwork.begin(); i != artwork.end(); ++i)
          CTextureCache::Get().BackgroundCacheImage(i->second);
      }
      else // nothing found - set an empty thumb so that next time around we don't hit here again
        m_database->SetArtForItem(tag.m_iDbId, tag.m_type, "thumb", "");
    }
    // For episodes and seasons, we want to set fanart for that of the show
    if (!item.HasProperty("fanart_image") && tag.m_iIdShow >= 0)
    {
      map<string, string> showArt;
      if (m_database->GetArtForItem(tag.m_iIdShow, "tvshow", showArt))
      {
        map<string, string>::iterator i = showArt.find("fanart");
        if (i != showArt.end())
          item.SetProperty("fanart_image", i->second);
        if ((i = showArt.find("thumb")) != showArt.end())
          item.SetProperty("tvshowthumb", i->second);
      }
    }
    m_database->Close();
  }
  return !item.GetArt().empty();
}

bool CVideoThumbLoader::FillThumb(CFileItem &item)
{
  if (item.HasThumbnail())
    return true;
  CStdString thumb = GetCachedImage(item, "thumb");
  if (thumb.IsEmpty())
  {
    thumb = item.GetUserVideoThumb();
    if (!thumb.IsEmpty())
      SetCachedImage(item, "thumb", thumb);
  }
  item.SetThumbnailImage(thumb);
  return !thumb.IsEmpty();
}

CStdString CVideoThumbLoader::GetEmbeddedThumbURL(const CFileItem &item)
{
  CStdString path(item.GetPath());
  if (item.IsVideoDb() && item.HasVideoInfoTag())
    path = item.GetVideoInfoTag()->m_strFileNameAndPath;
  if (URIUtils::IsStack(path))
    path = CStackDirectory::GetFirstStackedFile(path);

  return CTextureCache::GetWrappedImageURL(path, "video");
}

void CVideoThumbLoader::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  if (success)
  {
    CThumbExtractor* loader = (CThumbExtractor*)job;
    loader->m_item.SetPath(loader->m_listpath);
    CVideoInfoTag* info = loader->m_item.GetVideoInfoTag();
    if (m_pStreamDetailsObs)
      m_pStreamDetailsObs->OnStreamDetails(info->m_streamDetails, info->m_strFileNameAndPath, info->m_iFileId);
    if (m_pObserver)
      m_pObserver->OnItemLoaded(&loader->m_item);
    CFileItemPtr pItem(new CFileItem(loader->m_item));
    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, pItem);
    g_windowManager.SendThreadMessage(msg);
  }
  CJobQueue::OnJobComplete(jobID, success, job);
}

CProgramThumbLoader::CProgramThumbLoader()
{
}

CProgramThumbLoader::~CProgramThumbLoader()
{
}

bool CProgramThumbLoader::LoadItem(CFileItem *pItem)
{
  if (pItem->IsParentFolder()) return true;
  return FillThumb(*pItem);
}

bool CProgramThumbLoader::FillThumb(CFileItem &item)
{
  // no need to do anything if we already have a thumb set
  CStdString thumb = item.GetThumbnailImage();

  if (thumb.IsEmpty())
  { // see whether we have a cached image for this item
    thumb = GetCachedImage(item, "thumb");
    if (thumb.IsEmpty())
    {
      thumb = GetLocalThumb(item);
      if (!thumb.IsEmpty())
        SetCachedImage(item, "thumb", thumb);
    }
  }

  if (!thumb.IsEmpty())
  {
    CTextureCache::Get().BackgroundCacheImage(thumb);
    item.SetThumbnailImage(thumb);
  }
  return true;
}

CStdString CProgramThumbLoader::GetLocalThumb(const CFileItem &item)
{
  // look for the thumb
  if (item.m_bIsFolder)
  {
    CStdString folderThumb = item.GetFolderThumb();
    if (XFILE::CFile::Exists(folderThumb))
      return folderThumb;
  }
  else
  {
    CStdString fileThumb(item.GetTBNFile());
    if (CFile::Exists(fileThumb))
      return fileThumb;
  }
  return "";
}

CMusicThumbLoader::CMusicThumbLoader() : CThumbLoader(1)
{
  m_database = new CMusicDatabase;
}

CMusicThumbLoader::~CMusicThumbLoader()
{
  delete m_database;
}

void CMusicThumbLoader::Initialize()
{
  m_database->Open();
}

void CMusicThumbLoader::OnLoaderStart()
{
  Initialize();
}

void CMusicThumbLoader::OnLoaderFinish()
{
  m_database->Close();
}

bool CMusicThumbLoader::LoadItem(CFileItem* pItem)
{
  if (pItem->m_bIsShareOrDrive)
    return true;

  if (pItem->HasMusicInfoTag() && pItem->GetArt().empty())
  {
    if (FillLibraryArt(*pItem))
      return true;
    if (pItem->GetMusicInfoTag()->GetType() == "artist")
      return true; // no fallback
  }

  if (!pItem->HasProperty("fanart_image"))
  {
    if (pItem->HasMusicInfoTag() && !pItem->GetMusicInfoTag()->GetArtist().empty())
    {
      std::string artist = pItem->GetMusicInfoTag()->GetArtist()[0];
      m_database->Open();
      int idArtist = m_database->GetArtistByName(artist);
      if (idArtist >= 0)
      {
        string fanart = m_database->GetArtForItem(idArtist, "artist", "fanart");
        if (!fanart.empty())
          pItem->SetProperty("fanart_image", fanart);
      }
      m_database->Close();
    }
  }

  if (!pItem->HasThumbnail())
    FillThumb(*pItem);

  return true;
}

bool CMusicThumbLoader::FillThumb(CFileItem &item)
{
  if (item.HasThumbnail())
    return true;
  CStdString thumb = GetCachedImage(item, "thumb");
  if (thumb.IsEmpty())
  {
    thumb = item.GetUserMusicThumb();
    if (!thumb.IsEmpty())
      SetCachedImage(item, "thumb", thumb);
  }
  item.SetThumbnailImage(thumb);
  return !thumb.IsEmpty();
}

bool CMusicThumbLoader::FillLibraryArt(CFileItem &item)
{
  CMusicInfoTag &tag = *item.GetMusicInfoTag();
  if (tag.GetDatabaseId() > -1 && !tag.GetType().empty())
  {
    m_database->Open();
    map<string, string> artwork;
    if (m_database->GetArtForItem(tag.GetDatabaseId(), tag.GetType(), artwork))
      item.SetArt(artwork);
    else if (tag.GetType() == "song")
    { // no art for the song, try the album
      if (m_database->GetArtForItem(tag.GetAlbumId(), "album", artwork))
        item.SetArt(artwork);
    }
    else if (tag.GetType() == "artist")
    {
      { // Need the artist thumb/fanart which isn't grabbed during normal directory fetches
        CArtist artist;
        m_database->GetArtistInfo(tag.GetDatabaseId(), artist, false);
        CMusicInfoScanner scanner;
        artwork = scanner.GetArtistArtwork(tag.GetDatabaseId(), &artist);
        item.SetArt(artwork);
      }
      // add to the database for next time around
      map<string, string> artwork = item.GetArt();
      if (!artwork.empty())
      {
        m_database->SetArtForItem(tag.GetDatabaseId(), tag.GetType(), artwork);
        for (map<string, string>::iterator i = artwork.begin(); i != artwork.end(); ++i)
          CTextureCache::Get().BackgroundCacheImage(i->second);
      }
      else // nothing found - set an empty thumb so that next time around we don't hit here again
        m_database->SetArtForItem(tag.GetDatabaseId(), tag.GetType(), "thumb", "");
    }
    if (tag.GetType() == "song" || tag.GetType() == "album")
    { // fanart from the artist
      item.SetProperty("fanart_image", m_database->GetArtistArtForItem(tag.GetDatabaseId(), tag.GetType(), "fanart"));
    }
    m_database->Close();
  }
  return !item.GetArt().empty();
}

bool CMusicThumbLoader::GetEmbeddedThumb(const std::string &path, EmbeddedArt &art)
{
  auto_ptr<IMusicInfoTagLoader> pLoader (CMusicInfoTagLoaderFactory::CreateLoader(path));
  CMusicInfoTag tag;
  if (NULL != pLoader.get())
    pLoader->Load(path, tag, &art);

  return !art.empty();
}
