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

#include "threads/SystemClock.h"
#include "FileItem.h"
#include "VideoInfoScanner.h"
#include "addons/AddonManager.h"
#include "filesystem/DirectoryCache.h"
#include "Util.h"
#include "NfoFile.h"
#include "utils/RegExp.h"
#include "utils/md5.h"
#include "filesystem/StackDirectory.h"
#include "VideoInfoDownloader.h"
#include "GUIInfoManager.h"
#include "filesystem/File.h"
#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogYesNo.h"
#include "dialogs/GUIDialogOK.h"
#include "interfaces/AnnouncementManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "utils/StringUtils.h"
#include "guilib/LocalizeStrings.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "ThumbLoader.h"
#include "TextureCache.h"
#include "URL.h"

using namespace std;
using namespace XFILE;
using namespace ADDON;

namespace VIDEO
{

  CVideoInfoScanner::CVideoInfoScanner() : CThread("CVideoInfoScanner")
  {
    m_bRunning = false;
    m_pObserver = NULL;
    m_bCanInterrupt = false;
    m_currentItem = 0;
    m_itemCount = 0;
    m_bClean = false;
    m_scanAll = false;
  }

  CVideoInfoScanner::~CVideoInfoScanner()
  {
  }

  void CVideoInfoScanner::Process()
  {
    try
    {
      unsigned int tick = XbmcThreads::SystemClockMillis();

      m_database.Open();

      if (m_pObserver)
        m_pObserver->OnStateChanged(PREPARING);

      m_bCanInterrupt = true;

      CLog::Log(LOGNOTICE, "VideoInfoScanner: Starting scan ..");
      ANNOUNCEMENT::CAnnouncementManager::Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "OnScanStarted");

      // Reset progress vars
      m_currentItem = 0;
      m_itemCount = -1;

      SetPriority(GetMinPriority());

      // Database operations should not be canceled
      // using Interupt() while scanning as it could
      // result in unexpected behaviour.
      m_bCanInterrupt = false;

      bool bCancelled = false;
      while (!bCancelled && m_pathsToScan.size())
      {
        /*
         * A copy of the directory path is used because the path supplied is
         * immediately removed from the m_pathsToScan set in DoScan(). If the
         * reference points to the entry in the set a null reference error
         * occurs.
         */
        CStdString directory = *m_pathsToScan.begin();
        if (!DoScan(directory))
          bCancelled = true;
      }

      if (!bCancelled)
      {
        if (m_bClean)
          CleanDatabase(m_pObserver,&m_pathsToClean);
        else
        {
          if (m_pObserver)
            m_pObserver->OnStateChanged(COMPRESSING_DATABASE);
          m_database.Compress(false);
        }
      }

      m_database.Close();

      tick = XbmcThreads::SystemClockMillis() - tick;
      CLog::Log(LOGNOTICE, "VideoInfoScanner: Finished scan. Scanning for video info took %s", StringUtils::SecondsToTimeString(tick / 1000).c_str());
      ANNOUNCEMENT::CAnnouncementManager::Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "OnScanFinished");
      
      m_bRunning = false;
      if (m_pObserver)
        m_pObserver->OnFinished();
    }
    catch (...)
    {
      CLog::Log(LOGERROR, "VideoInfoScanner: Exception while scanning.");
    }
  }

  void CVideoInfoScanner::Start(const CStdString& strDirectory, bool scanAll)
  {
    m_strStartDir = strDirectory;
    m_scanAll = scanAll;
    m_pathsToScan.clear();
    m_pathsToClean.clear();

    if (strDirectory.IsEmpty())
    { // scan all paths in the database.  We do this by scanning all paths in the db, and crossing them off the list as
      // we go.
      m_database.Open();
      m_database.GetPaths(m_pathsToScan);
      m_database.Close();
    }
    else
    {
      m_pathsToScan.insert(strDirectory);
    }
    m_bClean = g_advancedSettings.m_bVideoLibraryCleanOnUpdate;

    StopThread();
    Create();
    m_bRunning = true;
  }

  bool CVideoInfoScanner::IsScanning()
  {
    return m_bRunning;
  }

  void CVideoInfoScanner::Stop()
  {
    if (m_bCanInterrupt)
      m_database.Interupt();

    StopThread();
  }

  void CVideoInfoScanner::CleanDatabase(IVideoInfoScannerObserver* pObserver /*= NULL */, const set<int>* paths /*= NULL */)
  {
    m_bRunning = true;
    m_database.Open();
    m_database.CleanDatabase(pObserver, paths);
    m_database.Close();
    m_bRunning = false;
  }

  void CVideoInfoScanner::SetObserver(IVideoInfoScannerObserver* pObserver)
  {
    m_pObserver = pObserver;
  }

  bool CVideoInfoScanner::DoScan(const CStdString& strDirectory)
  {
    if (m_pObserver)
    {
      m_pObserver->OnDirectoryChanged(strDirectory);
      m_pObserver->OnSetTitle(g_localizeStrings.Get(20415));
    }

    /*
     * Remove this path from the list we're processing. This must be done prior to
     * the check for file or folder exclusion to prevent an infinite while loop
     * in Process().
     */
    set<CStdString>::iterator it = m_pathsToScan.find(strDirectory);
    if (it != m_pathsToScan.end())
      m_pathsToScan.erase(it);

    // load subfolder
    CFileItemList items;
    bool foundDirectly = false;
    bool bSkip = false;

    SScanSettings settings;
    ScraperPtr info = m_database.GetScraperForPath(strDirectory, settings, foundDirectly);
    CONTENT_TYPE content = info ? info->Content() : CONTENT_NONE;

    // exclude folders that match our exclude regexps
    CStdStringArray regexps = content == CONTENT_TVSHOWS ? g_advancedSettings.m_tvshowExcludeFromScanRegExps
                                                         : g_advancedSettings.m_moviesExcludeFromScanRegExps;

    if (CUtil::ExcludeFileOrFolder(strDirectory, regexps))
      return true;

    bool ignoreFolder = !m_scanAll && settings.noupdate;
    if (content == CONTENT_NONE || ignoreFolder)
      return true;

    CStdString hash, dbHash;
    if (content == CONTENT_MOVIES ||content == CONTENT_MUSICVIDEOS)
    {
      if (m_pObserver)
        m_pObserver->OnStateChanged(content == CONTENT_MOVIES ? FETCHING_MOVIE_INFO : FETCHING_MUSICVIDEO_INFO);

      CStdString fastHash = GetFastHash(strDirectory);
      if (m_database.GetPathHash(strDirectory, dbHash) && !fastHash.IsEmpty() && fastHash == dbHash)
      { // fast hashes match - no need to process anything
        CLog::Log(LOGDEBUG, "VideoInfoScanner: Skipping dir '%s' due to no change (fasthash)", strDirectory.c_str());
        hash = fastHash;
        bSkip = true;
      }
      if (!bSkip)
      { // need to fetch the folder
        CDirectory::GetDirectory(strDirectory, items, g_settings.m_videoExtensions);
        items.Stack();
        // compute hash
        GetPathHash(items, hash);
        if (hash != dbHash && !hash.IsEmpty())
        {
          if (dbHash.IsEmpty())
            CLog::Log(LOGDEBUG, "VideoInfoScanner: Scanning dir '%s' as not in the database", strDirectory.c_str());
          else
            CLog::Log(LOGDEBUG, "VideoInfoScanner: Rescanning dir '%s' due to change (%s != %s)", strDirectory.c_str(), dbHash.c_str(), hash.c_str());
        }
        else
        { // they're the same or the hash is empty (dir empty/dir not retrievable)
          if (hash.IsEmpty() && !dbHash.IsEmpty())
          {
            CLog::Log(LOGDEBUG, "VideoInfoScanner: Skipping dir '%s' as it's empty or doesn't exist - adding to clean list", strDirectory.c_str());
            m_pathsToClean.insert(m_database.GetPathId(strDirectory));
          }
          else
            CLog::Log(LOGDEBUG, "VideoInfoScanner: Skipping dir '%s' due to no change", strDirectory.c_str());
          bSkip = true;
          if (m_pObserver)
            m_pObserver->OnDirectoryScanned(strDirectory);
        }
        // update the hash to a fast hash if needed
        if (CanFastHash(items) && !fastHash.IsEmpty())
          hash = fastHash;
      }
    }
    else if (content == CONTENT_TVSHOWS)
    {
      if (m_pObserver)
        m_pObserver->OnStateChanged(FETCHING_TVSHOW_INFO);

      if (foundDirectly && !settings.parent_name_root)
      {
        CDirectory::GetDirectory(strDirectory, items, g_settings.m_videoExtensions);
        items.SetPath(strDirectory);
        GetPathHash(items, hash);
        bSkip = true;
        if (!m_database.GetPathHash(strDirectory, dbHash) || dbHash != hash)
        {
          m_database.SetPathHash(strDirectory, hash);
          bSkip = false;
        }
        else
          items.Clear();
      }
      else
      {
        CFileItemPtr item(new CFileItem(URIUtils::GetFileName(strDirectory)));
        item->SetPath(strDirectory);
        item->m_bIsFolder = true;
        items.Add(item);
        items.SetPath(URIUtils::GetParentPath(item->GetPath()));
      }
    }

    if (!bSkip)
    {
      if (RetrieveVideoInfo(items, settings.parent_name_root, content))
      {
        if (!m_bStop && (content == CONTENT_MOVIES || content == CONTENT_MUSICVIDEOS))
        {
          m_database.SetPathHash(strDirectory, hash);
          m_pathsToClean.insert(m_database.GetPathId(strDirectory));
          CLog::Log(LOGDEBUG, "VideoInfoScanner: Finished adding information from dir %s", strDirectory.c_str());
        }
      }
      else
      {
        m_pathsToClean.insert(m_database.GetPathId(strDirectory));
        CLog::Log(LOGDEBUG, "VideoInfoScanner: No (new) information was found in dir %s", strDirectory.c_str());
      }
    }
    else if (hash != dbHash && (content == CONTENT_MOVIES || content == CONTENT_MUSICVIDEOS))
    { // update the hash either way - we may have changed the hash to a fast version
      m_database.SetPathHash(strDirectory, hash);
    }

    if (m_pObserver)
      m_pObserver->OnDirectoryScanned(strDirectory);

    for (int i = 0; i < items.Size(); ++i)
    {
      CFileItemPtr pItem = items[i];

      if (m_bStop)
        break;

      // if we have a directory item (non-playlist) we then recurse into that folder
      // do not recurse for tv shows - we have already looked recursively for episodes
      if (pItem->m_bIsFolder && !pItem->IsParentFolder() && !pItem->IsPlayList() && settings.recurse > 0 && content != CONTENT_TVSHOWS)
      {
        if (!DoScan(pItem->GetPath()))
        {
          m_bStop = true;
        }
      }
    }
    return !m_bStop;
  }

  bool CVideoInfoScanner::RetrieveVideoInfo(CFileItemList& items, bool bDirNames, CONTENT_TYPE content, bool useLocal, CScraperUrl* pURL, bool fetchEpisodes, CGUIDialogProgress* pDlgProgress)
  {
    if (pDlgProgress)
    {
      if (items.Size() > 1 || (items[0]->m_bIsFolder && fetchEpisodes))
      {
        pDlgProgress->ShowProgressBar(true);
        pDlgProgress->SetPercentage(0);
      }
      else
        pDlgProgress->ShowProgressBar(false);

      pDlgProgress->Progress();
    }

    m_database.Open();

    bool FoundSomeInfo = false;
    vector<int> seenPaths;
    for (int i = 0; i < (int)items.Size(); ++i)
    {
      m_nfoReader.Close();
      CFileItemPtr pItem = items[i];

      // we do this since we may have a override per dir
      ScraperPtr info2 = m_database.GetScraperForPath(pItem->m_bIsFolder ? pItem->GetPath() : items.GetPath());
      if (!info2) // skip
        continue;

      // Discard all exclude files defined by regExExclude
      if (CUtil::ExcludeFileOrFolder(pItem->GetPath(), (content == CONTENT_TVSHOWS) ? g_advancedSettings.m_tvshowExcludeFromScanRegExps
                                                                                    : g_advancedSettings.m_moviesExcludeFromScanRegExps))
        continue;

      if (info2->Content() == CONTENT_MOVIES || info2->Content() == CONTENT_MUSICVIDEOS)
      {
        if (m_pObserver)
        {
          m_pObserver->OnSetCurrentProgress(i, items.Size());
          if (!pItem->m_bIsFolder && m_itemCount)
            m_pObserver->OnSetProgress(m_currentItem++, m_itemCount);
        }

      }

      // clear our scraper cache
      info2->ClearCache();

      INFO_RET ret = INFO_CANCELLED;
      if (info2->Content() == CONTENT_TVSHOWS)
        ret = RetrieveInfoForTvShow(pItem, bDirNames, info2, useLocal, pURL, fetchEpisodes, pDlgProgress);
      else if (info2->Content() == CONTENT_MOVIES)
        ret = RetrieveInfoForMovie(pItem, bDirNames, info2, useLocal, pURL, pDlgProgress);
      else if (info2->Content() == CONTENT_MUSICVIDEOS)
        ret = RetrieveInfoForMusicVideo(pItem, bDirNames, info2, useLocal, pURL, pDlgProgress);
      else
      {
        CLog::Log(LOGERROR, "VideoInfoScanner: Unknown content type %d (%s)", info2->Content(), pItem->GetPath().c_str());
        FoundSomeInfo = false;
        break;
      }
      if (ret == INFO_CANCELLED || ret == INFO_ERROR)
      {
        FoundSomeInfo = false;
        break;
      }
      if (ret == INFO_ADDED || ret == INFO_HAVE_ALREADY)
        FoundSomeInfo = true;

      pURL = NULL;

      // Keep track of directories we've seen
      if (pItem->m_bIsFolder)
        seenPaths.push_back(m_database.GetPathId(pItem->GetPath()));
    }

    if (content == CONTENT_TVSHOWS && ! seenPaths.empty())
    {
      vector< pair<int,string> > libPaths;
      m_database.GetSubPaths(items.GetPath(), libPaths);
      for (vector< pair<int,string> >::iterator i = libPaths.begin(); i < libPaths.end(); ++i)
      {
        if (find(seenPaths.begin(), seenPaths.end(), i->first) == seenPaths.end())
          m_pathsToClean.insert(i->first);
      }
    }
    if(pDlgProgress)
      pDlgProgress->ShowProgressBar(false);

    g_infoManager.ResetLibraryBools();
    m_database.Close();
    return FoundSomeInfo;
  }

  INFO_RET CVideoInfoScanner::RetrieveInfoForTvShow(CFileItemPtr pItem, bool bDirNames, ScraperPtr &info2, bool useLocal, CScraperUrl* pURL, bool fetchEpisodes, CGUIDialogProgress* pDlgProgress)
  {
    long idTvShow = -1;
    if (pItem->m_bIsFolder)
      idTvShow = m_database.GetTvShowId(pItem->GetPath());
    else
    {
      CStdString strPath;
      URIUtils::GetDirectory(pItem->GetPath(),strPath);
      idTvShow = m_database.GetTvShowId(strPath);
    }
    if (idTvShow > -1 && (fetchEpisodes || !pItem->m_bIsFolder))
    {
      INFO_RET ret = RetrieveInfoForEpisodes(pItem, idTvShow, info2, useLocal, pDlgProgress);
      if (ret == INFO_ADDED)
        m_database.SetPathHash(pItem->GetPath(), pItem->GetProperty("hash").asString());
      return ret;
    }

    if (ProgressCancelled(pDlgProgress, pItem->m_bIsFolder ? 20353 : 20361, pItem->GetLabel()))
      return INFO_CANCELLED;

    CNfoFile::NFOResult result=CNfoFile::NO_NFO;
    CScraperUrl scrUrl;
    // handle .nfo files
    if (useLocal)
      result = CheckForNFOFile(pItem.get(), bDirNames, info2, scrUrl);
    if (result != CNfoFile::NO_NFO && result != CNfoFile::ERROR_NFO)
    { // check for preconfigured scraper; if found, overwrite with interpreted scraper (from Nfofile)
      // but keep current scan settings
      SScanSettings settings;
      if (m_database.GetScraperForPath(pItem->GetPath(), settings))
        m_database.SetScraperForPath(pItem->GetPath(), info2, settings);
    }
    if (result == CNfoFile::FULL_NFO)
    {
      pItem->GetVideoInfoTag()->Reset();
      m_nfoReader.GetDetails(*pItem->GetVideoInfoTag());

      long lResult = AddVideo(pItem.get(), info2->Content(), bDirNames, useLocal);
      if (lResult < 0)
        return INFO_ERROR;
      if (fetchEpisodes)
      {
        INFO_RET ret = RetrieveInfoForEpisodes(pItem, lResult, info2, useLocal, pDlgProgress);
        if (ret == INFO_ADDED)
          m_database.SetPathHash(pItem->GetPath(), pItem->GetProperty("hash").asString());
        return ret;
      }
      return INFO_ADDED;
    }
    if (result == CNfoFile::URL_NFO || result == CNfoFile::COMBINED_NFO)
      pURL = &scrUrl;

    CScraperUrl url;
    int retVal = 0;
    if (pURL)
      url = *pURL;
    else if ((retVal = FindVideo(pItem->GetMovieName(bDirNames), info2, url, pDlgProgress)) <= 0)
      return retVal < 0 ? INFO_CANCELLED : INFO_NOT_FOUND;

    long lResult=-1;
    if (GetDetails(pItem.get(), url, info2, result == CNfoFile::COMBINED_NFO ? &m_nfoReader : NULL, pDlgProgress))
    {
      if ((lResult = AddVideo(pItem.get(), info2->Content(), false, useLocal)) < 0)
        return INFO_ERROR;
    }
    if (fetchEpisodes)
    {
      INFO_RET ret = RetrieveInfoForEpisodes(pItem, lResult, info2, useLocal, pDlgProgress);
      if (ret == INFO_ADDED)
        m_database.SetPathHash(pItem->GetPath(), pItem->GetProperty("hash").asString());
    }
    return INFO_ADDED;
  }

  INFO_RET CVideoInfoScanner::RetrieveInfoForMovie(CFileItemPtr pItem, bool bDirNames, ScraperPtr &info2, bool useLocal, CScraperUrl* pURL, CGUIDialogProgress* pDlgProgress)
  {
    if (pItem->m_bIsFolder || !pItem->IsVideo() || pItem->IsNFO() ||
       (pItem->IsPlayList() && !URIUtils::GetExtension(pItem->GetPath()).Equals(".strm")))
      return INFO_NOT_NEEDED;

    if (ProgressCancelled(pDlgProgress, 198, pItem->GetLabel()))
      return INFO_CANCELLED;

    if (m_database.HasMovieInfo(pItem->GetPath()))
      return INFO_HAVE_ALREADY;

    CNfoFile::NFOResult result=CNfoFile::NO_NFO;
    CScraperUrl scrUrl;
    // handle .nfo files
    if (useLocal)
      result = CheckForNFOFile(pItem.get(), bDirNames, info2, scrUrl);
    if (result == CNfoFile::FULL_NFO)
    {
      pItem->GetVideoInfoTag()->Reset();
      m_nfoReader.GetDetails(*pItem->GetVideoInfoTag());

      if (AddVideo(pItem.get(), info2->Content(), bDirNames, true) < 0)
        return INFO_ERROR;
      return INFO_ADDED;
    }
    if (result == CNfoFile::URL_NFO || result == CNfoFile::COMBINED_NFO)
      pURL = &scrUrl;

    CScraperUrl url;
    int retVal = 0;
    if (pURL)
      url = *pURL;
    else if ((retVal = FindVideo(pItem->GetMovieName(bDirNames), info2, url, pDlgProgress)) <= 0)
      return retVal < 0 ? INFO_CANCELLED : INFO_NOT_FOUND;

    if (GetDetails(pItem.get(), url, info2, result == CNfoFile::COMBINED_NFO ? &m_nfoReader : NULL, pDlgProgress))
    {
      if (AddVideo(pItem.get(), info2->Content(), bDirNames, useLocal) < 0)
        return INFO_ERROR;
      return INFO_ADDED;
    }
    // TODO: This is not strictly correct as we could fail to download information here or error, or be cancelled
    return INFO_NOT_FOUND;
  }

  INFO_RET CVideoInfoScanner::RetrieveInfoForMusicVideo(CFileItemPtr pItem, bool bDirNames, ScraperPtr &info2, bool useLocal, CScraperUrl* pURL, CGUIDialogProgress* pDlgProgress)
  {
    if (pItem->m_bIsFolder || !pItem->IsVideo() || pItem->IsNFO() ||
       (pItem->IsPlayList() && !URIUtils::GetExtension(pItem->GetPath()).Equals(".strm")))
      return INFO_NOT_NEEDED;

    if (ProgressCancelled(pDlgProgress, 20394, pItem->GetLabel()))
      return INFO_CANCELLED;

    if (m_database.HasMusicVideoInfo(pItem->GetPath()))
      return INFO_HAVE_ALREADY;

    CNfoFile::NFOResult result=CNfoFile::NO_NFO;
    CScraperUrl scrUrl;
    // handle .nfo files
    if (useLocal)
      result = CheckForNFOFile(pItem.get(), bDirNames, info2, scrUrl);
    if (result == CNfoFile::FULL_NFO)
    {
      pItem->GetVideoInfoTag()->Reset();
      m_nfoReader.GetDetails(*pItem->GetVideoInfoTag());

      if (AddVideo(pItem.get(), info2->Content(), bDirNames, true) < 0)
        return INFO_ERROR;
      return INFO_ADDED;
    }
    if (result == CNfoFile::URL_NFO || result == CNfoFile::COMBINED_NFO)
      pURL = &scrUrl;

    CScraperUrl url;
    int retVal = 0;
    if (pURL)
      url = *pURL;
    else if ((retVal = FindVideo(pItem->GetMovieName(bDirNames), info2, url, pDlgProgress)) <= 0)
      return retVal < 0 ? INFO_CANCELLED : INFO_NOT_FOUND;

    if (GetDetails(pItem.get(), url, info2, result == CNfoFile::COMBINED_NFO ? &m_nfoReader : NULL, pDlgProgress))
    {
      if (AddVideo(pItem.get(), info2->Content(), bDirNames, useLocal) < 0)
        return INFO_ERROR;
      return INFO_ADDED;
    }
    // TODO: This is not strictly correct as we could fail to download information here or error, or be cancelled
    return INFO_NOT_FOUND;
  }

  INFO_RET CVideoInfoScanner::RetrieveInfoForEpisodes(CFileItemPtr item, long showID, const ADDON::ScraperPtr &scraper, bool useLocal, CGUIDialogProgress *progress)
  {
    // enumerate episodes
    EPISODES files;
    EnumerateSeriesFolder(item.get(), files);
    if (files.size() == 0) // no update or no files
      return INFO_NOT_NEEDED;

    if (m_bStop || (progress && progress->IsCanceled()))
      return INFO_CANCELLED;

    if (m_pObserver)
      m_pObserver->OnDirectoryChanged(item->GetPath());

    CStdString showTitle = m_database.GetTvShowTitleById(showID);
    return OnProcessSeriesFolder(files, scraper, useLocal, showID, showTitle, progress);
  }

  void CVideoInfoScanner::EnumerateSeriesFolder(CFileItem* item, EPISODES& episodeList)
  {
    CFileItemList items;

    if (item->m_bIsFolder)
    {
      CUtil::GetRecursiveListing(item->GetPath(), items, g_settings.m_videoExtensions, true);
      CStdString hash, dbHash;
      int numFilesInFolder = GetPathHash(items, hash);

      if (m_database.GetPathHash(item->GetPath(), dbHash) && dbHash == hash)
      {
        m_currentItem += numFilesInFolder;

        // notify our observer of our progress
        if (m_pObserver)
        {
          if (m_itemCount>0)
          {
            m_pObserver->OnSetProgress(m_currentItem, m_itemCount);
            m_pObserver->OnSetCurrentProgress(numFilesInFolder, numFilesInFolder);
          }
          m_pObserver->OnDirectoryScanned(item->GetPath());
        }
        return;
      }
      m_pathsToClean.insert(m_database.GetPathId(item->GetPath()));
      m_database.GetPathsForTvShow(m_database.GetTvShowId(item->GetPath()), m_pathsToClean);
      item->SetProperty("hash", hash);
    }
    else
    {
      CFileItemPtr newItem(new CFileItem(*item));
      items.Add(newItem);
    }

    /*
    stack down any dvd folders
    need to sort using the full path since this is a collapsed recursive listing of all subdirs
    video_ts.ifo files should sort at the top of a dvd folder in ascending order

    /foo/bar/video_ts.ifo
    /foo/bar/vts_x_y.ifo
    /foo/bar/vts_x_y.vob
    */

    // since we're doing this now anyway, should other items be stacked?
    items.Sort(SORT_METHOD_FULLPATH, SortOrderAscending);
    int x = 0;
    while (x < items.Size())
    {
      if (items[x]->m_bIsFolder)
        continue;


      CStdString strPathX, strFileX;
      URIUtils::Split(items[x]->GetPath(), strPathX, strFileX);
      //CLog::Log(LOGDEBUG,"%i:%s:%s", x, strPathX.c_str(), strFileX.c_str());

      int y = x + 1;
      if (strFileX.Equals("VIDEO_TS.IFO"))
      {
        while (y < items.Size())
        {
          CStdString strPathY, strFileY;
          URIUtils::Split(items[y]->GetPath(), strPathY, strFileY);
          //CLog::Log(LOGDEBUG," %i:%s:%s", y, strPathY.c_str(), strFileY.c_str());

          if (strPathY.Equals(strPathX))
            /*
            remove everything sorted below the video_ts.ifo file in the same path.
            understandbly this wont stack correctly if there are other files in the the dvd folder.
            this should be unlikely and thus is being ignored for now but we can monitor the
            where the path changes and potentially remove the items above the video_ts.ifo file.
            */
            items.Remove(y);
          else
            break;
        }
      }
      x = y;
    }

    // enumerate
    CStdStringArray regexps = g_advancedSettings.m_tvshowExcludeFromScanRegExps;

    for (int i=0;i<items.Size();++i)
    {
      if (items[i]->m_bIsFolder)
        continue;
      CStdString strPath;
      URIUtils::GetDirectory(items[i]->GetPath(), strPath);
      URIUtils::RemoveSlashAtEnd(strPath); // want no slash for the test that follows

      if (URIUtils::GetFileName(strPath).Equals("sample"))
        continue;

      // Discard all exclude files defined by regExExcludes
      if (CUtil::ExcludeFileOrFolder(items[i]->GetPath(), regexps))
        continue;

      /*
       * Check if the media source has already set the season and episode or original air date in
       * the VideoInfoTag. If it has, do not try to parse any of them from the file path to avoid
       * any false positive matches.
       */
      if (ProcessItemByVideoInfoTag(items[i], episodeList))
        continue;

      if (!EnumerateEpisodeItem(items[i], episodeList))
      {
        CStdString decode(items[i]->GetPath());
        CURL::Decode(decode);
        CLog::Log(LOGDEBUG, "VideoInfoScanner: Could not enumerate file %s", decode.c_str());
      }
    }
  }

  bool CVideoInfoScanner::ProcessItemByVideoInfoTag(const CFileItemPtr item, EPISODES &episodeList)
  {
    if (!item->HasVideoInfoTag())
      return false;

    CVideoInfoTag* tag = item->GetVideoInfoTag();
    /*
     * First check the season and episode number. This takes precedence over the original air
     * date and episode title. Must be a valid season and episode number combination.
     */
    if (tag->m_iSeason > -1 && tag->m_iEpisode > 0)
    {
      SEpisode episode;
      episode.strPath = item->GetPath();
      episode.iSeason = tag->m_iSeason;
      episode.iEpisode = tag->m_iEpisode;
      episode.isFolder = false;
      episodeList.push_back(episode);
      CLog::Log(LOGDEBUG, "%s - found match for: %s. Season %d, Episode %d", __FUNCTION__,
                episode.strPath.c_str(), episode.iSeason, episode.iEpisode);
      return true;
    }

    /*
     * Next preference is the first aired date. If it exists use that for matching the TV Show
     * information. Also set the title in case there are multiple matches for the first aired date.
     */
    if (tag->m_firstAired.IsValid())
    {
      SEpisode episode;
      episode.strPath = item->GetPath();
      episode.strTitle = tag->m_strTitle;
      episode.isFolder = false;
      /*
       * Set season and episode to -1 to indicate to use the aired date.
       */
      episode.iSeason = -1;
      episode.iEpisode = -1;
      /*
       * The first aired date string must be parseable.
       */
      episode.cDate = item->GetVideoInfoTag()->m_firstAired;
      episodeList.push_back(episode);
      CLog::Log(LOGDEBUG, "%s - found match for: '%s', firstAired: '%s' = '%s', title: '%s'",
        __FUNCTION__, episode.strPath.c_str(), tag->m_firstAired.GetAsDBDateTime().c_str(),
                episode.cDate.GetAsLocalizedDate().c_str(), episode.strTitle.c_str());
      return true;
    }

    /*
     * Next preference is the episode title. If it exists use that for matching the TV Show
     * information.
     */
    if (!tag->m_strTitle.IsEmpty())
    {
      SEpisode episode;
      episode.strPath = item->GetPath();
      episode.strTitle = tag->m_strTitle;
      episode.isFolder = false;
      /*
       * Set season and episode to -1 to indicate to use the title.
       */
      episode.iSeason = -1;
      episode.iEpisode = -1;
      episodeList.push_back(episode);
      CLog::Log(LOGDEBUG,"%s - found match for: '%s', title: '%s'", __FUNCTION__,
                episode.strPath.c_str(), episode.strTitle.c_str());
      return true;
    }

    /*
     * There is no further episode information available if both the season and episode number have
     * been set to 0. Return the match as true so no further matching is attempted, but don't add it
     * to the episode list.
     */
    if (tag->m_iSeason == 0 && tag->m_iEpisode == 0)
    {
      CLog::Log(LOGDEBUG,"%s - found exclusion match for: %s. Both Season and Episode are 0. Item will be ignored for scanning.",
                __FUNCTION__, item->GetPath().c_str());
      return true;
    }

    return false;
  }

  bool CVideoInfoScanner::EnumerateEpisodeItem(const CFileItemPtr item, EPISODES& episodeList)
  {
    SETTINGS_TVSHOWLIST expression = g_advancedSettings.m_tvshowEnumRegExps;

    CStdString strLabel=item->GetPath();
    // URLDecode in case an episode is on a http/https/dav/davs:// source and URL-encoded like foo%201x01%20bar.avi
    CURL::Decode(strLabel);
    strLabel.MakeLower();

    for (unsigned int i=0;i<expression.size();++i)
    {
      CRegExp reg;
      if (!reg.RegComp(expression[i].regexp))
        continue;

      int regexppos, regexp2pos;
      //CLog::Log(LOGDEBUG,"running expression %s on %s",expression[i].regexp.c_str(),strLabel.c_str());
      if ((regexppos = reg.RegFind(strLabel.c_str())) < 0)
        continue;

      SEpisode episode;
      episode.strPath = item->GetPath();
      episode.iSeason = -1;
      episode.iEpisode = -1;
      episode.cDate.SetValid(false);
      episode.isFolder = false;

      bool byDate = expression[i].byDate ? true : false;
      int defaultSeason = expression[i].defaultSeason;

      if (byDate)
      {
        if (!GetAirDateFromRegExp(reg, episode))
          continue;

        CLog::Log(LOGDEBUG, "VideoInfoScanner: Found date based match %s (%s) [%s]", strLabel.c_str(),
                  episode.cDate.GetAsLocalizedDate().c_str(), expression[i].regexp.c_str());
      }
      else
      {
        if (!GetEpisodeAndSeasonFromRegExp(reg, episode, defaultSeason))
          continue;

        CLog::Log(LOGDEBUG, "VideoInfoScanner: Found episode match %s (s%ie%i) [%s]", strLabel.c_str(),
                  episode.iSeason, episode.iEpisode, expression[i].regexp.c_str());
      }

      // Grab the remainder from first regexp run
      // as second run might modify or empty it.
      char *remainder = reg.GetReplaceString("\\3");

      /*
       * Check if the files base path is a dedicated folder that contains
       * only this single episode. If season and episode match with the
       * actual media file, we set episode.isFolder to true.
       */
      CStdString strBasePath = item->GetBaseMoviePath(true);
      URIUtils::RemoveSlashAtEnd(strBasePath);
      strBasePath = URIUtils::GetFileName(strBasePath);

      if (reg.RegFind(strBasePath.c_str()) > -1)
      {
        SEpisode parent;
        if (byDate)
        {
          GetAirDateFromRegExp(reg, parent);
          if (episode.cDate == parent.cDate)
            episode.isFolder = true;
        }
        else
        {
          GetEpisodeAndSeasonFromRegExp(reg, parent, defaultSeason);
          if (episode.iSeason == parent.iSeason && episode.iEpisode == parent.iEpisode)
            episode.isFolder = true;
        }
      }

      // add what we found by now
      episodeList.push_back(episode);

      CRegExp reg2;
      // check the remainder of the string for any further episodes.
      if (!byDate && reg2.RegComp(g_advancedSettings.m_tvshowMultiPartEnumRegExp))
      {
        int offset = 0;

        // we want "long circuit" OR below so that both offsets are evaluated
        while (((regexp2pos = reg2.RegFind(remainder + offset)) > -1) | ((regexppos = reg.RegFind(remainder + offset)) > -1))
        {
          if (((regexppos <= regexp2pos) && regexppos != -1) ||
             (regexppos >= 0 && regexp2pos == -1))
          {
            GetEpisodeAndSeasonFromRegExp(reg, episode, defaultSeason);

            CLog::Log(LOGDEBUG, "VideoInfoScanner: Adding new season %u, multipart episode %u [%s]",
                      episode.iSeason, episode.iEpisode,
                      g_advancedSettings.m_tvshowMultiPartEnumRegExp.c_str());

            episodeList.push_back(episode);
            free(remainder);
            remainder = reg.GetReplaceString("\\3");
            offset = 0;
          }
          else if (((regexp2pos < regexppos) && regexp2pos != -1) ||
                   (regexp2pos >= 0 && regexppos == -1))
          {
            char *ep = reg2.GetReplaceString("\\1");
            episode.iEpisode = atoi(ep);
            free(ep);
            CLog::Log(LOGDEBUG, "VideoInfoScanner: Adding multipart episode %u [%s]",
                      episode.iEpisode, g_advancedSettings.m_tvshowMultiPartEnumRegExp.c_str());
            episodeList.push_back(episode);
            offset += regexp2pos + reg2.GetFindLen();
          }
        }
      }
      free(remainder);
      return true;
    }
    return false;
  }

  bool CVideoInfoScanner::GetEpisodeAndSeasonFromRegExp(CRegExp &reg, SEpisode &episodeInfo, int defaultSeason)
  {
    char* season = reg.GetReplaceString("\\1");
    char* episode = reg.GetReplaceString("\\2");

    if (season && episode)
    {
      if (strlen(season) == 0 && strlen(episode) > 0)
      { // no season specified -> assume defaultSeason
        episodeInfo.iSeason = defaultSeason;
        if ((episodeInfo.iEpisode = CUtil::TranslateRomanNumeral(episode)) == -1)
          episodeInfo.iEpisode = atoi(episode);
      }
      else if (strlen(season) > 0 && strlen(episode) == 0)
      { // no episode specification -> assume defaultSeason
        episodeInfo.iSeason = defaultSeason;
        if ((episodeInfo.iEpisode = CUtil::TranslateRomanNumeral(season)) == -1)
          episodeInfo.iEpisode = atoi(season);
      }
      else
      { // season and episode specified
        episodeInfo.iSeason = atoi(season);
        episodeInfo.iEpisode = atoi(episode);
      }
    }
    free(season);
    free(episode);
    return (season && episode);
  }

  bool CVideoInfoScanner::GetAirDateFromRegExp(CRegExp &reg, SEpisode &episodeInfo)
  {
    char* param1 = reg.GetReplaceString("\\1");
    char* param2 = reg.GetReplaceString("\\2");
    char* param3 = reg.GetReplaceString("\\3");

    if (param1 && param2 && param3)
    {
      // regular expression by date
      int len1 = strlen( param1 );
      int len2 = strlen( param2 );
      int len3 = strlen( param3 );

      if (len1==4 && len2==2 && len3==2)
      {
        // yyyy mm dd format
        episodeInfo.cDate.SetDate(atoi(param1), atoi(param2), atoi(param3));
      }
      else if (len1==2 && len2==2 && len3==4)
      {
        // mm dd yyyy format
        episodeInfo.cDate.SetDate(atoi(param3), atoi(param1), atoi(param2));
      }
    }
    free(param1);
    free(param2);
    free(param3);
    return episodeInfo.cDate.IsValid();
  }

  long CVideoInfoScanner::AddVideo(CFileItem *pItem, const CONTENT_TYPE &content, bool videoFolder /* = false */, bool useLocal /* = true */, int idShow /* = -1 */, bool libraryImport /* = false */)
  {
    // ensure our database is open (this can get called via other classes)
    if (!m_database.Open())
      return -1;

    GetArtwork(pItem, content, videoFolder, useLocal);
    // ensure the art map isn't completely empty by specifying an empty thumb
    map<string, string> art = pItem->GetArt();
    if (art.empty())
      art["thumb"] = "";

    CVideoInfoTag &movieDetails = *pItem->GetVideoInfoTag();
    if (movieDetails.m_basePath.IsEmpty())
      movieDetails.m_basePath = pItem->GetBaseMoviePath(videoFolder);
    movieDetails.m_parentPathID = m_database.AddPath(URIUtils::GetParentPath(movieDetails.m_basePath));

    movieDetails.m_strFileNameAndPath = pItem->GetPath();

    if (pItem->m_bIsFolder)
      movieDetails.m_strPath = pItem->GetPath();

    CStdString strTitle(movieDetails.m_strTitle);

    if (idShow > -1 && content == CONTENT_TVSHOWS)
    {
      CStdString strShowTitle = m_database.GetTvShowTitleById(idShow);
      strTitle.Format("%s - %ix%i - %s", strShowTitle.c_str(), movieDetails.m_iSeason, movieDetails.m_iEpisode, strTitle.c_str());
    }

    if (m_pObserver)
      m_pObserver->OnSetTitle(strTitle);

    CLog::Log(LOGDEBUG, "VideoInfoScanner: Adding new item to %s:%s", TranslateContent(content).c_str(), pItem->GetPath().c_str());
    long lResult = -1;

    if (content == CONTENT_MOVIES)
    {
      // find local trailer first
      CStdString strTrailer = pItem->FindTrailer();
      if (!strTrailer.IsEmpty())
        movieDetails.m_strTrailer = strTrailer;

      lResult = m_database.SetDetailsForMovie(pItem->GetPath(), movieDetails, art);
      movieDetails.m_iDbId = lResult;

      // setup links to shows if the linked shows are in the db
      for (unsigned int i=0; i < movieDetails.m_showLink.size(); ++i)
      {
        CFileItemList items;
        m_database.GetTvShowsByName(movieDetails.m_showLink[i], items);
        if (items.Size())
          m_database.LinkMovieToTvshow(lResult, items[0]->GetVideoInfoTag()->m_iDbId, false);
        else
          CLog::Log(LOGDEBUG, "VideoInfoScanner: Failed to link movie %s to show %s", movieDetails.m_strTitle.c_str(), movieDetails.m_showLink[i].c_str());
      }
    }
    else if (content == CONTENT_TVSHOWS)
    {
      if (pItem->m_bIsFolder)
      {
        // get season thumbs
        map<int, string> seasonArt;
        GetSeasonThumbs(movieDetails, seasonArt);
        lResult = m_database.SetDetailsForTvShow(pItem->GetPath(), movieDetails, art, seasonArt);
        movieDetails.m_iDbId = lResult;
      }
      else
      {
        // we add episode then set details, as otherwise set details will delete the
        // episode then add, which breaks multi-episode files.
        int idEpisode = m_database.AddEpisode(idShow, pItem->GetPath());
        lResult = m_database.SetDetailsForEpisode(pItem->GetPath(), movieDetails, art, idShow, idEpisode);
        movieDetails.m_iDbId = lResult;
        if (movieDetails.m_fEpBookmark > 0)
        {
          movieDetails.m_strFileNameAndPath = pItem->GetPath();
          CBookmark bookmark;
          bookmark.timeInSeconds = movieDetails.m_fEpBookmark;
          bookmark.seasonNumber = movieDetails.m_iSeason;
          bookmark.episodeNumber = movieDetails.m_iEpisode;
          m_database.AddBookMarkForEpisode(movieDetails, bookmark);
        }
      }
    }
    else if (content == CONTENT_MUSICVIDEOS)
    {
      lResult = m_database.SetDetailsForMusicVideo(pItem->GetPath(), movieDetails, art);
      movieDetails.m_iDbId = lResult;
    }

    if (g_advancedSettings.m_bVideoLibraryImportWatchedState || libraryImport)
      m_database.SetPlayCount(*pItem, movieDetails.m_playCount, movieDetails.m_lastPlayed);

    if ((g_advancedSettings.m_bVideoLibraryImportResumePoint || libraryImport) &&
        movieDetails.m_resumePoint.IsSet())
      m_database.AddBookMarkToFile(pItem->GetPath(), movieDetails.m_resumePoint, CBookmark::RESUME);

    m_database.Close();

    CFileItemPtr itemCopy = CFileItemPtr(new CFileItem(*pItem));
    // Hack to make sure CVideoInfoTag::m_strShowTitle is set for tvshows
    // to make sure CAnnouncementManager provides the correct type for the item
    if (content == CONTENT_TVSHOWS && !pItem->m_bIsFolder && itemCopy->HasVideoInfoTag())
      itemCopy->GetVideoInfoTag()->m_strShowTitle = itemCopy->GetVideoInfoTag()->m_strTitle;
    ANNOUNCEMENT::CAnnouncementManager::Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "OnUpdate", itemCopy);
    return lResult;
  }

  void CVideoInfoScanner::GetArtwork(CFileItem *pItem, const CONTENT_TYPE &content, bool bApplyToDir, bool useLocal)
  {
    CVideoInfoTag &movieDetails = *pItem->GetVideoInfoTag();
    movieDetails.m_fanart.Unpack();
    movieDetails.m_strPictureURL.Parse();

    // get & save fanart image
    bool isEpisode = (content == CONTENT_TVSHOWS && !pItem->m_bIsFolder);
    if (!isEpisode)
    {
      CStdString fanart;
      if (pItem->HasProperty("fanart_image"))
        fanart = pItem->GetProperty("fanart_image").asString();
      else if (useLocal)
        fanart = pItem->GetLocalFanart();
      if (fanart.IsEmpty())
        fanart = movieDetails.m_fanart.GetImageURL();
      if (!fanart.IsEmpty())
      {
        CTextureCache::Get().BackgroundCacheImage(fanart);
        pItem->SetProperty("fanart_image", fanart);
      }
    }

    // get and cache thumb image
    CStdString thumb;
    if (pItem->HasThumbnail())
      thumb = pItem->GetThumbnailImage();
    else if (useLocal)
    {
      thumb = pItem->GetUserVideoThumb();
      if (bApplyToDir && thumb.IsEmpty())
      {
        CStdString strParent;
        URIUtils::GetParentPath(pItem->GetPath(), strParent);
        CFileItem folderItem(*pItem);
        folderItem.SetPath(strParent);
        folderItem.m_bIsFolder = true;
        thumb = folderItem.GetUserVideoThumb();
      }
    }

    if (thumb.IsEmpty())
    {
      thumb = CScraperUrl::GetThumbURL(movieDetails.m_strPictureURL.GetFirstThumb());
      if (!thumb.IsEmpty())
      {
        if (thumb.Find("http://") < 0 &&
            thumb.Find("/") < 0 &&
            thumb.Find("\\") < 0)
        {
          CStdString strPath;
          URIUtils::GetDirectory(pItem->GetPath(), strPath);
          thumb = URIUtils::AddFileToFolder(strPath, thumb);
        }
      }
    }
    if (!thumb.IsEmpty())
    {
      CTextureCache::Get().BackgroundCacheImage(thumb);
      pItem->SetThumbnailImage(thumb);
    }

    // parent folder to apply the thumb to and to search for local actor thumbs
    CStdString parentDir = GetParentDir(*pItem);
    if (g_guiSettings.GetBool("videolibrary.actorthumbs"))
      FetchActorThumbs(movieDetails.m_cast, parentDir);
    if (bApplyToDir)
      ApplyThumbToFolder(parentDir, thumb);
  }

  INFO_RET CVideoInfoScanner::OnProcessSeriesFolder(EPISODES& files, const ADDON::ScraperPtr &scraper, bool useLocal, int idShow, const CStdString& strShowTitle, CGUIDialogProgress* pDlgProgress /* = NULL */)
  {
    if (pDlgProgress)
    {
      pDlgProgress->SetLine(1, strShowTitle);
      pDlgProgress->SetLine(2, 20361);
      pDlgProgress->SetPercentage(0);
      pDlgProgress->ShowProgressBar(true);
      pDlgProgress->Progress();
    }

    EPISODELIST episodes;
    bool hasEpisodeGuide = false;

    int iMax = files.size();
    int iCurr = 1;
    for (EPISODES::iterator file = files.begin(); file != files.end(); ++file)
    {
      m_nfoReader.Close();
      if (pDlgProgress)
      {
        pDlgProgress->SetLine(2, 20361);
        pDlgProgress->SetPercentage((int)((float)(iCurr++)/iMax*100));
        pDlgProgress->Progress();
      }
      if (m_pObserver)
      {
        if (m_itemCount > 0)
          m_pObserver->OnSetProgress(m_currentItem++, m_itemCount);
        m_pObserver->OnSetCurrentProgress(iCurr++, iMax);
      }
      if ((pDlgProgress && pDlgProgress->IsCanceled()) || m_bStop)
        return INFO_CANCELLED;

      if (m_database.GetEpisodeId(file->strPath, file->iEpisode, file->iSeason) > -1)
      {
        if (m_pObserver)
          m_pObserver->OnSetTitle(g_localizeStrings.Get(20415));
        continue;
      }

      CFileItem item;
      item.SetPath(file->strPath);

      // handle .nfo files
      CNfoFile::NFOResult result=CNfoFile::NO_NFO;
      CScraperUrl scrUrl;
      ScraperPtr info(scraper);
      item.GetVideoInfoTag()->m_iEpisode = file->iEpisode;
      if (useLocal)
        result = CheckForNFOFile(&item, false, info,scrUrl);
      if (result == CNfoFile::FULL_NFO)
      {
        m_nfoReader.GetDetails(*item.GetVideoInfoTag());
        if (AddVideo(&item, CONTENT_TVSHOWS, file->isFolder, true, idShow) < 0)
          return INFO_ERROR;
        continue;
      }

      if (!hasEpisodeGuide)
      {
        // fetch episode guide
        CVideoInfoTag details;
        m_database.GetTvShowInfo(item.GetPath(), details, idShow);
        if (!details.m_strEpisodeGuide.IsEmpty())
        {
          CScraperUrl url;
          url.ParseEpisodeGuide(details.m_strEpisodeGuide);

          if (pDlgProgress)
          {
            pDlgProgress->SetLine(2, 20354);
            pDlgProgress->Progress();
          }

          CVideoInfoDownloader imdb(scraper);
          if (!imdb.GetEpisodeList(url, episodes))
            return INFO_NOT_FOUND;

          hasEpisodeGuide = true;
        }
      }

      if (episodes.empty())
      {
        CLog::Log(LOGERROR, "VideoInfoScanner: Asked to lookup episode %s"
                            " online, but we have no episode guide. Check your tvshow.nfo and make"
                            " sure the <episodeguide> tag is in place.", file->strPath.c_str());
        continue;
      }

      std::pair<int,int> key;
      key.first = file->iSeason;
      key.second = file->iEpisode;
      bool bFound = false;
      EPISODELIST::iterator guide = episodes.begin();;
      EPISODELIST matches;

      for (; guide != episodes.end(); ++guide )
      {
        if ((file->iEpisode!=-1) && (file->iSeason!=-1) && (key==guide->key))
        {
          bFound = true;
          break;
        }
        if (file->cDate.IsValid() && guide->cDate.IsValid() && file->cDate==guide->cDate)
        {
          matches.push_back(*guide);
          continue;
        }
        if (!guide->cScraperUrl.strTitle.IsEmpty() && guide->cScraperUrl.strTitle.CompareNoCase(file->strTitle) == 0)
        {
          bFound = true;
          break;
        }
      }

      if (!bFound)
      {
        /*
         * If there is only one match or there are matches but no title to compare with to help
         * identify the best match, then pick the first match as the best possible candidate.
         *
         * Otherwise, use the title to further refine the best match.
         */
        if (matches.size() == 1 || (file->strTitle.IsEmpty() && matches.size() > 1))
        {
          guide = matches.begin();
          bFound = true;
        }
        else if (!file->strTitle.IsEmpty())
        {
          double minscore = 0; // Default minimum score is 0 to find whatever is the best match.

          EPISODELIST *candidates;
          if (matches.empty()) // No matches found using earlier criteria. Use fuzzy match on titles across all episodes.
          {
            minscore = 0.8; // 80% should ensure a good match.
            candidates = &episodes;
          }
          else // Multiple matches found. Use fuzzy match on the title with already matched episodes to pick the best.
            candidates = &matches;

          CStdStringArray titles;
          for (guide = candidates->begin(); guide != candidates->end(); ++guide)
            titles.push_back(guide->cScraperUrl.strTitle.ToLower());

          double matchscore;
          int index = StringUtils::FindBestMatch(file->strTitle.ToLower(), titles, matchscore);
          if (matchscore >= minscore)
          {
            guide = candidates->begin() + index;
            bFound = true;
            CLog::Log(LOGDEBUG,"%s fuzzy title match for show: '%s', title: '%s', match: '%s', score: %f >= %f",
                      __FUNCTION__, strShowTitle.c_str(), file->strTitle.c_str(), titles[index].c_str(), matchscore, minscore);
          }
        }
      }

      if (bFound)
      {
        CVideoInfoDownloader imdb(scraper);
        CFileItem item;
        item.SetPath(file->strPath);
        if (!imdb.GetEpisodeDetails(guide->cScraperUrl, *item.GetVideoInfoTag(), pDlgProgress))
          return INFO_NOT_FOUND; // TODO: should we just skip to the next episode?
          
        // Only set season/epnum from filename when it is not already set by a scraper
        if (item.GetVideoInfoTag()->m_iSeason == -1)
          item.GetVideoInfoTag()->m_iSeason = guide->key.first;
        if (item.GetVideoInfoTag()->m_iEpisode == -1)
          item.GetVideoInfoTag()->m_iEpisode = guide->key.second;
          
        if (AddVideo(&item, CONTENT_TVSHOWS, file->isFolder, useLocal, idShow) < 0)
          return INFO_ERROR;
      }
      else
      {
        CLog::Log(LOGDEBUG,"%s - no match for show: '%s', season: %d, episode: %d, airdate: '%s', title: '%s'",
                  __FUNCTION__, strShowTitle.c_str(), file->iSeason, file->iEpisode,
                  file->cDate.GetAsLocalizedDate().c_str(), file->strTitle.c_str());
      }
    }
    return INFO_ADDED;
  }

  CStdString CVideoInfoScanner::GetnfoFile(CFileItem *item, bool bGrabAny) const
  {
    CStdString nfoFile;
    // Find a matching .nfo file
    if (!item->m_bIsFolder)
    {
      // file
      CStdString strExtension;
      URIUtils::GetExtension(item->GetPath(), strExtension);

      if (URIUtils::IsInRAR(item->GetPath())) // we have a rarred item - we want to check outside the rars
      {
        CFileItem item2(*item);
        CURL url(item->GetPath());
        CStdString strPath;
        URIUtils::GetDirectory(url.GetHostName(), strPath);
        item2.SetPath(URIUtils::AddFileToFolder(strPath, URIUtils::GetFileName(item->GetPath())));
        return GetnfoFile(&item2, bGrabAny);
      }

      // grab the folder path
      CStdString strPath;
      URIUtils::GetDirectory(item->GetPath(), strPath);

      if (bGrabAny && !item->IsStack())
      { // looking up by folder name - movie.nfo takes priority - but not for stacked items (handled below)
        nfoFile = URIUtils::AddFileToFolder(strPath, "movie.nfo");
        if (CFile::Exists(nfoFile))
          return nfoFile;
      }

      // try looking for .nfo file for a stacked item
      if (item->IsStack())
      {
        // first try .nfo file matching first file in stack
        CStackDirectory dir;
        CStdString firstFile = dir.GetFirstStackedFile(item->GetPath());
        CFileItem item2;
        item2.SetPath(firstFile);
        nfoFile = GetnfoFile(&item2, bGrabAny);
        // else try .nfo file matching stacked title
        if (nfoFile.IsEmpty())
        {
          CStdString stackedTitlePath = dir.GetStackedTitlePath(item->GetPath());
          item2.SetPath(stackedTitlePath);
          nfoFile = GetnfoFile(&item2, bGrabAny);
        }
      }
      else
      {
        // already an .nfo file?
        if ( strcmpi(strExtension.c_str(), ".nfo") == 0 )
          nfoFile = item->GetPath();
        // no, create .nfo file
        else
          nfoFile = URIUtils::ReplaceExtension(item->GetPath(), ".nfo");
      }

      // test file existence
      if (!nfoFile.IsEmpty() && !CFile::Exists(nfoFile))
        nfoFile.Empty();

      if (nfoFile.IsEmpty()) // final attempt - strip off any cd1 folders
      {
        URIUtils::RemoveSlashAtEnd(strPath); // need no slash for the check that follows
        CFileItem item2;
        if (strPath.Mid(strPath.size()-3).Equals("cd1"))
        {
          strPath = strPath.Mid(0,strPath.size()-3);
          item2.SetPath(URIUtils::AddFileToFolder(strPath, URIUtils::GetFileName(item->GetPath())));
          return GetnfoFile(&item2, bGrabAny);
        }
      }

      if (nfoFile.IsEmpty() && item->IsOpticalMediaFile())
      {
        CFileItem parentDirectory(item->GetLocalMetadataPath(), true);
        nfoFile = GetnfoFile(&parentDirectory, true);
      }
    }
    // folders (or stacked dvds) can take any nfo file if there's a unique one
    if (item->m_bIsFolder || item->IsOpticalMediaFile() || (bGrabAny && nfoFile.IsEmpty()))
    {
      // see if there is a unique nfo file in this folder, and if so, use that
      CFileItemList items;
      CDirectory dir;
      CStdString strPath = item->GetPath();
      if (!item->m_bIsFolder)
        URIUtils::GetDirectory(item->GetPath(), strPath);
      if (dir.GetDirectory(strPath, items, ".nfo") && items.Size())
      {
        int numNFO = -1;
        for (int i = 0; i < items.Size(); i++)
        {
          if (items[i]->IsNFO())
          {
            if (numNFO == -1)
              numNFO = i;
            else
            {
              numNFO = -1;
              break;
            }
          }
        }
        if (numNFO > -1)
          return items[numNFO]->GetPath();
      }
    }

    return nfoFile;
  }

  bool CVideoInfoScanner::GetDetails(CFileItem *pItem, CScraperUrl &url, const ScraperPtr& scraper, CNfoFile *nfoFile, CGUIDialogProgress* pDialog /* = NULL */)
  {
    CVideoInfoTag movieDetails;

    CVideoInfoDownloader imdb(scraper);
    bool ret = imdb.GetDetails(url, movieDetails, pDialog);

    if (ret)
    {
      if (nfoFile)
        nfoFile->GetDetails(movieDetails,NULL,true);

      if (m_pObserver && url.strTitle.IsEmpty())
        m_pObserver->OnSetTitle(movieDetails.m_strTitle);

      if (pDialog)
      {
        pDialog->SetLine(1, movieDetails.m_strTitle);
        pDialog->Progress();
      }

      *pItem->GetVideoInfoTag() = movieDetails;
      return true;
    }
    return false; // no info found, or cancelled
  }

  void CVideoInfoScanner::ApplyThumbToFolder(const CStdString &folder, const CStdString &imdbThumb)
  {
    // copy icon to folder also;
    if (!imdbThumb.IsEmpty())
    {
      CFileItem folderItem(folder, true);
      CThumbLoader::SetCachedImage(folderItem, "thumb", imdbThumb);
    }
  }

  int CVideoInfoScanner::GetPathHash(const CFileItemList &items, CStdString &hash)
  {
    // Create a hash based on the filenames, filesize and filedate.  Also count the number of files
    if (0 == items.Size()) return 0;
    XBMC::XBMC_MD5 md5state;
    int count = 0;
    for (int i = 0; i < items.Size(); ++i)
    {
      const CFileItemPtr pItem = items[i];
      md5state.append(pItem->GetPath());
      md5state.append((unsigned char *)&pItem->m_dwSize, sizeof(pItem->m_dwSize));
      FILETIME time = pItem->m_dateTime;
      md5state.append((unsigned char *)&time, sizeof(FILETIME));
      if (pItem->IsVideo() && !pItem->IsPlayList() && !pItem->IsNFO())
        count++;
    }
    md5state.getDigest(hash);
    return count;
  }

  bool CVideoInfoScanner::CanFastHash(const CFileItemList &items) const
  {
    // TODO: Probably should account for excluded folders here (eg samples), though that then
    //       introduces possible problems if the user then changes the exclude regexps and
    //       expects excluded folders that are inside a fast-hashed folder to then be picked
    //       up. The chances that the user has a folder which contains only excluded folders
    //       where some of those folders should be scanned recursively is pretty small.
    return items.GetFolderCount() == 0;
  }

  CStdString CVideoInfoScanner::GetFastHash(const CStdString &directory) const
  {
    struct __stat64 buffer;
    if (XFILE::CFile::Stat(directory, &buffer) == 0)
    {
      int64_t time = buffer.st_mtime;
      if (!time)
        time = buffer.st_ctime;
      if (time)
      {
        CStdString hash;
        hash.Format("fast%"PRId64, time);
        return hash;
      }
    }
    return "";
  }

  void CVideoInfoScanner::GetSeasonThumbs(const CVideoInfoTag &show, map<int, string> &art, bool useLocal)
  {
    show.m_strPictureURL.GetSeasonThumbs(art);
    // override with any local thumbs
    if (useLocal)
    {
      CFileItemList items;
      CDirectory::GetDirectory(show.m_strPath, items, ".tbn");
      // run through all these items and see which ones match
      CRegExp reg;
      if (items.Size() && reg.RegComp("season[ ._-]?([0-9]+)\\.tbn"))
      {
        for (int i = 0; i < items.Size(); i++)
        {
          CStdString name = URIUtils::GetFileName(items[i]->GetPath());
          name.ToLower();
          if (name == "season-all.tbn")
            art.insert(make_pair(-1, items[i]->GetPath()));
          else if (name == "season-specials.tbn")
            art.insert(make_pair(0, items[i]->GetPath()));
          else if (reg.RegFind(name) > -1)
          {
            char* seasonStr = reg.GetReplaceString("\\1");
            int season = atoi(seasonStr);
            free(seasonStr);

            art.insert(make_pair(season, items[i]->GetPath()));
          }
        }
      }
    }
    // cache them
    for (map<int, string>::iterator i = art.begin(); i != art.end(); ++i)
      CTextureCache::Get().BackgroundCacheImage(i->second);
  }

  void CVideoInfoScanner::FetchActorThumbs(vector<SActorInfo>& actors, const CStdString& strPath)
  {
    for (vector<SActorInfo>::iterator i = actors.begin(); i != actors.end(); ++i)
    {
      if (i->thumb.IsEmpty())
      {
        CStdString thumbFile = i->strName;
        thumbFile.Replace(" ","_");
        thumbFile += ".tbn";
        CStdString strLocal = URIUtils::AddFileToFolder(URIUtils::AddFileToFolder(strPath, ".actors"), thumbFile);
        if (CFile::Exists(strLocal))
          i->thumb = strLocal;
        else if (!i->thumbUrl.GetFirstThumb().m_url.IsEmpty())
          i->thumb = CScraperUrl::GetThumbURL(i->thumbUrl.GetFirstThumb());
        if (!i->thumb.IsEmpty())
          CTextureCache::Get().BackgroundCacheImage(i->thumb);
      }
    }
  }

  CNfoFile::NFOResult CVideoInfoScanner::CheckForNFOFile(CFileItem* pItem, bool bGrabAny, ScraperPtr& info, CScraperUrl& scrUrl)
  {
    CStdString strNfoFile;
    if (info->Content() == CONTENT_MOVIES || info->Content() == CONTENT_MUSICVIDEOS
        || (info->Content() == CONTENT_TVSHOWS && !pItem->m_bIsFolder))
      strNfoFile = GetnfoFile(pItem, bGrabAny);
    if (info->Content() == CONTENT_TVSHOWS && pItem->m_bIsFolder)
      URIUtils::AddFileToFolder(pItem->GetPath(), "tvshow.nfo", strNfoFile);

    CNfoFile::NFOResult result=CNfoFile::NO_NFO;
    if (!strNfoFile.IsEmpty() && CFile::Exists(strNfoFile))
    {
      result = m_nfoReader.Create(strNfoFile,info,pItem->GetVideoInfoTag()->m_iEpisode);

      CStdString type;
      switch(result)
      {
        case CNfoFile::COMBINED_NFO:
          type = "Mixed";
          break;
        case CNfoFile::FULL_NFO:
          type = "Full";
          break;
        case CNfoFile::URL_NFO:
          type = "URL";
          break;
        case CNfoFile::NO_NFO:
          type = "";
          break;
        default:
          type = "malformed";
      }
      if (result != CNfoFile::NO_NFO)
        CLog::Log(LOGDEBUG, "VideoInfoScanner: Found matching %s NFO file: %s", type.c_str(), strNfoFile.c_str());
      if (result == CNfoFile::FULL_NFO)
      {
        if (info->Content() == CONTENT_TVSHOWS)
          info = m_nfoReader.GetScraperInfo();
      }
      else if (result != CNfoFile::NO_NFO && result != CNfoFile::ERROR_NFO)
      {
        scrUrl = m_nfoReader.ScraperUrl();
        info = m_nfoReader.GetScraperInfo();

        CLog::Log(LOGDEBUG, "VideoInfoScanner: Fetching url '%s' using %s scraper (content: '%s')",
          scrUrl.m_url[0].m_url.c_str(), info->Name().c_str(), TranslateContent(info->Content()).c_str());

        if (result == CNfoFile::COMBINED_NFO)
          m_nfoReader.GetDetails(*pItem->GetVideoInfoTag());
      }
    }
    else
      CLog::Log(LOGDEBUG, "VideoInfoScanner: No NFO file found. Using title search for '%s'", pItem->GetPath().c_str());

    return result;
  }

  bool CVideoInfoScanner::DownloadFailed(CGUIDialogProgress* pDialog)
  {
    if (g_advancedSettings.m_bVideoScannerIgnoreErrors)
      return true;

    if (pDialog)
    {
      CGUIDialogOK::ShowAndGetInput(20448,20449,20022,20022);
      return false;
    }
    return CGUIDialogYesNo::ShowAndGetInput(20448,20449,20450,20022);
  }

  bool CVideoInfoScanner::ProgressCancelled(CGUIDialogProgress* progress, int heading, const CStdString &line1)
  {
    if (progress)
    {
      progress->SetHeading(heading);
      progress->SetLine(0, line1);
      progress->SetLine(2, "");
      progress->Progress();
      return progress->IsCanceled();
    }
    return m_bStop;
  }

  int CVideoInfoScanner::FindVideo(const CStdString &videoName, const ScraperPtr &scraper, CScraperUrl &url, CGUIDialogProgress *progress)
  {
    MOVIELIST movielist;
    CVideoInfoDownloader imdb(scraper);
    int returncode = imdb.FindMovie(videoName, movielist, progress);
    if (returncode < 0 || (returncode == 0 && !DownloadFailed(progress)))
    { // scraper reported an error, or we had an error and user wants to cancel the scan
      m_bStop = true;
      return -1; // cancelled
    }
    if (returncode > 0 && movielist.size())
    {
      url = movielist[0];
      return 1;  // found a movie
    }
    return 0;    // didn't find anything
  }

  CStdString CVideoInfoScanner::GetParentDir(const CFileItem &item) const
  {
    CStdString strCheck = item.GetPath();
    if (item.IsStack())
      strCheck = CStackDirectory::GetFirstStackedFile(item.GetPath());

    CStdString strDirectory;
    URIUtils::GetDirectory(strCheck, strDirectory);
    if (URIUtils::IsInRAR(strCheck))
    {
      CStdString strPath=strDirectory;
      URIUtils::GetParentPath(strPath, strDirectory);
    }
    if (item.IsStack())
    {
      strCheck = strDirectory;
      URIUtils::RemoveSlashAtEnd(strCheck);
      if (URIUtils::GetFileName(strCheck).size() == 3 && URIUtils::GetFileName(strCheck).Left(2).Equals("cd"))
        URIUtils::GetDirectory(strCheck, strDirectory);
    }
    return strDirectory;
  }

}
