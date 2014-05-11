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

#include "RSSDirectory.h"
#include "FileItem.h"
#include "CurlFile.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/HTMLUtil.h"
#include "utils/StringUtils.h"
#include "video/VideoInfoTag.h"
#include "music/tags/MusicInfoTag.h"
#include "utils/log.h"
#include "URL.h"
#include "climits"
#include "threads/SingleLock.h"

using namespace XFILE;
using namespace std;
using namespace MUSIC_INFO;

namespace {

  struct SResource
  {
    SResource()
      : width(0)
      , height(0)
      , bitrate(0)
      , duration(0)
      , size(0)
    {}

    CStdString tag;
    CStdString path;
    CStdString mime;
    CStdString lang;
    int        width;
    int        height;
    int        bitrate;
    int        duration;
    int64_t    size;
  };

  typedef std::vector<SResource> SResources;

}

std::map<CStdString,CDateTime> CRSSDirectory::m_cache;
CCriticalSection CRSSDirectory::m_section;

CRSSDirectory::CRSSDirectory()
{
}

CRSSDirectory::~CRSSDirectory()
{
}

bool CRSSDirectory::ContainsFiles(const CStdString& strPath)
{
  CFileItemList items;
  if(!GetDirectory(strPath, items))
    return false;

  return items.Size() > 0;
}

static bool IsPathToMedia(const CStdString& strPath )
{
  return URIUtils::HasExtension(strPath,
                              g_advancedSettings.m_videoExtensions + '|' +
                              g_advancedSettings.m_musicExtensions + '|' +
                              g_advancedSettings.m_pictureExtensions);
}

static bool IsPathToThumbnail(const CStdString& strPath )
{
  // Currently just check if this is an image, maybe we will add some
  // other checks later
  return URIUtils::HasExtension(strPath,
                                    g_advancedSettings.m_pictureExtensions);
}

static time_t ParseDate(const CStdString & strDate)
{
  struct tm pubDate = {0};
  // TODO: Handle time zone
  strptime(strDate.c_str(), "%a, %d %b %Y %H:%M:%S", &pubDate);
  // Check the difference between the time of last check and time of the item
  return mktime(&pubDate);
}
static void ParseItem(CFileItem* item, SResources& resources, TiXmlElement* root, const CStdString& path);

static void ParseItemMRSS(CFileItem* item, SResources& resources, TiXmlElement* item_child, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CVideoInfoTag* vtag = item->GetVideoInfoTag();
  CStdString text = item_child->GetText();

  if(name == "content")
  {
    SResource res;
    res.tag = "media:content";
    res.mime    = item_child->Attribute("type");
    res.path    = item_child->Attribute("url");
    if(item_child->Attribute("width"))
      res.width    = atoi(item_child->Attribute("width"));
    if(item_child->Attribute("height"))
      res.height   = atoi(item_child->Attribute("height"));
    if(item_child->Attribute("bitrate"))
      res.bitrate  = atoi(item_child->Attribute("bitrate"));
    if(item_child->Attribute("duration"))
      res.duration = atoi(item_child->Attribute("duration"));
    if(item_child->Attribute("fileSize"))
      res.size     = _atoi64(item_child->Attribute("fileSize"));

    resources.push_back(res);
    ParseItem(item, resources, item_child, path);
  }
  else if(name == "group")
  {
    ParseItem(item, resources, item_child, path);
  }
  else if(name == "thumbnail")
  {
    if(item_child->GetText() && IsPathToThumbnail(item_child->GetText()))
      item->SetArt("thumb", item_child->GetText());
    else
    {
      const char * url = item_child->Attribute("url");
      if(url && IsPathToThumbnail(url))
        item->SetArt("thumb", url);
    }
  }
  else if (name == "title")
  {
    if(text.empty())
      return;

    if(text.length() > item->m_strTitle.length())
      item->m_strTitle = text;
  }
  else if(name == "description")
  {
    if(text.empty())
      return;

    CStdString description = text;
    if(CStdString(item_child->Attribute("type")) == "html")
      HTML::CHTMLUtil::RemoveTags(description);
    item->SetProperty("description", description);
  }
  else if(name == "category")
  {
    if(text.empty())
      return;

    CStdString scheme = item_child->Attribute("scheme");

    /* okey this is silly, boxee what did you think?? */
    if     (scheme == "urn:boxee:genre")
      vtag->m_genre.push_back(text);
    else if(scheme == "urn:boxee:title-type")
    {
      if     (text == "tv")
        item->SetProperty("boxee:istvshow", true);
      else if(text == "movie")
        item->SetProperty("boxee:ismovie", true);
    }
    else if(scheme == "urn:boxee:episode")
      vtag->m_iEpisode = atoi(text.c_str());
    else if(scheme == "urn:boxee:season")
      vtag->m_iSeason  = atoi(text.c_str());
    else if(scheme == "urn:boxee:show-title")
      vtag->m_strShowTitle = text.c_str();
    else if(scheme == "urn:boxee:view-count")
      vtag->m_playCount = atoi(text.c_str());
    else if(scheme == "urn:boxee:source")
      item->SetProperty("boxee:provider_source", text);
    else
      vtag->m_genre = StringUtils::Split(text, g_advancedSettings.m_videoItemSeparator);
  }
  else if(name == "rating")
  {
    CStdString scheme = item_child->Attribute("scheme");
    if(scheme == "urn:user")
      vtag->m_fRating = (float)atof(text.c_str());
    else
      vtag->m_strMPAARating = text;
  }
  else if(name == "credit")
  {
    CStdString role = item_child->Attribute("role");
    if     (role == "director")
      vtag->m_director.push_back(text);
    else if(role == "author"
         || role == "writer")
      vtag->m_writingCredits.push_back(text);
    else if(role == "actor")
    {
      SActorInfo actor;
      actor.strName = text;
      vtag->m_cast.push_back(actor);
    }
  }
  else if(name == "copyright")
    vtag->m_studio = StringUtils::Split(text, g_advancedSettings.m_videoItemSeparator);
  else if(name == "keywords")
    item->SetProperty("keywords", text);

}

static void ParseItemItunes(CFileItem* item, SResources& resources, TiXmlElement* item_child, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CVideoInfoTag* vtag = item->GetVideoInfoTag();
  CStdString text = item_child->GetText();

  if(name == "image")
  {
    const char * url = item_child->Attribute("href");
    if(url)
      item->SetArt("thumb", url);
    else
      item->SetArt("thumb", text);
  }
  else if(name == "summary")
    vtag->m_strPlot = text;
  else if(name == "subtitle")
    vtag->m_strPlotOutline = text;
  else if(name == "author")
    vtag->m_writingCredits.push_back(text);
  else if(name == "duration")
    vtag->m_duration = StringUtils::TimeStringToSeconds(text);
  else if(name == "keywords")
    item->SetProperty("keywords", text);
}

static void ParseItemRSS(CFileItem* item, SResources& resources, TiXmlElement* item_child, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CStdString text = item_child->GetText();
  if (name == "title")
  {
    if(text.length() > item->m_strTitle.length())
      item->m_strTitle = text;
  }
  else if (name == "pubDate")
  {
    CDateTime pubDate(ParseDate(text));
    item->m_dateTime = pubDate;
  }
  else if (name == "link")
  {
    SResource res;
    res.tag  = "rss:link";
    res.path = text;
    resources.push_back(res);
  }
  else if(name == "enclosure")
  {
    const char * url  = item_child->Attribute("url");
    const char * type = item_child->Attribute("type");
    const char * len  = item_child->Attribute("length");

    SResource res;
    res.tag = "rss:enclosure";
    if(url)
      res.path = url;
    if(type)
      res.mime = type;
    if(len)
      res.size = _atoi64(len);

    resources.push_back(res);
  }
  else if(name == "description")
  {
    CStdString description = text;
    HTML::CHTMLUtil::RemoveTags(description);
    item->SetProperty("description", description);
  }
  else if(name == "guid")
  {
    if(IsPathToMedia(text))
    {
      SResource res;
      res.tag  = "rss:guid";
      res.path = text;
      resources.push_back(res);
    }
  }
}

static void ParseItemVoddler(CFileItem* item, SResources& resources, TiXmlElement* element, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CVideoInfoTag* vtag = item->GetVideoInfoTag();
  CStdString text = element->GetText();

  if(name == "trailer")
  {
    vtag->m_strTrailer = text;

    SResource res;
    res.tag  = "voddler:trailer";
    res.mime = element->Attribute("type");
    res.path = text;
    resources.push_back(res);
  }
  else if(name == "year")
    vtag->m_iYear = atoi(text);
  else if(name == "rating")
    vtag->m_fRating = (float)atof(text);
  else if(name == "tagline")
    vtag->m_strTagLine = text;
  else if(name == "posterwall")
  {
    const char* url = element->Attribute("url");
    if(url)
      item->SetArt("fanart", url);
    else if(IsPathToThumbnail(text))
      item->SetArt("fanart", text);
  }
}

static void ParseItemBoxee(CFileItem* item, SResources& resources, TiXmlElement* element, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CVideoInfoTag* vtag = item->GetVideoInfoTag();
  CStdString text = element->GetText();

  if     (name == "image")
    item->SetArt("thumb", text);
  else if(name == "user_agent")
    item->SetProperty("boxee:user_agent", text);
  else if(name == "content_type")
    item->SetMimeType(text);
  else if(name == "runtime")
    vtag->m_duration = StringUtils::TimeStringToSeconds(text);
  else if(name == "episode")
    vtag->m_iEpisode = atoi(text);
  else if(name == "season")
    vtag->m_iSeason = atoi(text);
  else if(name == "view-count")
    vtag->m_playCount = atoi(text);
  else if(name == "tv-show-title")
    vtag->m_strShowTitle = text;
  else if(name == "release-date")
    item->SetProperty("boxee:releasedate", text);
}

static void ParseItemZink(CFileItem* item, SResources& resources, TiXmlElement* element, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CVideoInfoTag* vtag = item->GetVideoInfoTag();
  CStdString text = element->GetText();
  if     (name == "episode")
    vtag->m_iEpisode = atoi(text);
  else if(name == "season")
    vtag->m_iSeason = atoi(text);
  else if(name == "views")
    vtag->m_playCount = atoi(text);
  else if(name == "airdate")
    vtag->m_firstAired.SetFromDateString(text);
  else if(name == "userrating")
    vtag->m_fRating = (float)atof(text.c_str());
  else if(name == "duration")
    vtag->m_duration = atoi(text);
  else if(name == "durationstr")
    vtag->m_duration = StringUtils::TimeStringToSeconds(text);
}

static void ParseItemSVT(CFileItem* item, SResources& resources, TiXmlElement* element, const CStdString& name, const CStdString& xmlns, const CStdString& path)
{
  CStdString text = element->GetText();
  if     (name == "xmllink")
  {
    SResource res;
    res.tag  = "svtplay:xmllink";
    res.path = text;
    res.mime = "application/rss+xml";
    resources.push_back(res);
  }
  else if (name == "broadcasts")
  {
    CURL url(path);
    if(StringUtils::StartsWith(url.GetFileName(), "v1/"))
    {
      SResource res;
      res.tag  = "svtplay:broadcasts";
      res.path = url.GetWithoutFilename() + "v1/video/list/" + text;
      res.mime = "application/rss+xml";
      resources.push_back(res);
    }
  }
}

static void ParseItem(CFileItem* item, SResources& resources, TiXmlElement* root, const CStdString& path)
{
  for (TiXmlElement* child = root->FirstChildElement(); child; child = child->NextSiblingElement())
  {
    CStdString name = child->Value();
    CStdString xmlns;
    size_t pos = name.find(':');
    if(pos != std::string::npos)
    {
      xmlns = name.substr(0, pos);
      name.erase(0, pos+1);
    }

    if      (xmlns == "media")
      ParseItemMRSS   (item, resources, child, name, xmlns, path);
    else if (xmlns == "itunes")
      ParseItemItunes (item, resources, child, name, xmlns, path);
    else if (xmlns == "voddler")
      ParseItemVoddler(item, resources, child, name, xmlns, path);
    else if (xmlns == "boxee")
      ParseItemBoxee  (item, resources, child, name, xmlns, path);
    else if (xmlns == "zn")
      ParseItemZink   (item, resources, child, name, xmlns, path);
    else if (xmlns == "svtplay")
      ParseItemSVT    (item, resources, child, name, xmlns, path);
    else
      ParseItemRSS    (item, resources, child, name, xmlns, path);
  }
}

static bool FindMime(SResources resources, CStdString mime)
{
  for(SResources::iterator it = resources.begin(); it != resources.end(); it++)
  {
    if(StringUtils::StartsWithNoCase(it->mime, mime))
      return true;
  }
  return false;
}

static void ParseItem(CFileItem* item, TiXmlElement* root, const CStdString& path)
{
  SResources resources;
  ParseItem(item, resources, root, path);

  const char* prio[] = { "media:content", "voddler:trailer", "rss:enclosure", "svtplay:broadcasts", "svtplay:xmllink", "rss:link", "rss:guid", NULL };

  CStdString mime;
  if     (FindMime(resources, "video/"))
    mime = "video/";
  else if(FindMime(resources, "audio/"))
    mime = "audio/";
  else if(FindMime(resources, "application/rss"))
    mime = "application/rss";
  else if(FindMime(resources, "image/"))
    mime = "image/";

  int maxrate = CSettings::Get().GetInt("network.bandwidth");
  if(maxrate == 0)
    maxrate = INT_MAX;

  SResources::iterator best = resources.end();
  for(const char** type = prio; *type && best == resources.end(); type++)
  {
    for(SResources::iterator it = resources.begin(); it != resources.end(); it++)
    {
      if(!StringUtils::StartsWith(it->mime, mime))
        continue;

      if(it->tag == *type)
      {
        if(best == resources.end())
        {
          best = it;
          continue;
        }

        if(it->bitrate == best->bitrate)
        {
          if(it->width*it->height > best->width*best->height)
            best = it;
          continue;
        }

        if(it->bitrate > maxrate)
        {
          if(it->bitrate < best->bitrate)
            best = it;
          continue;
        }

        if(it->bitrate > best->bitrate)
        {
          best = it;
          continue;
        }
      }
    }
  }

  if(best != resources.end())
  {
    item->SetMimeType(best->mime);
    item->SetPath(best->path);
    item->m_dwSize  = best->size;

    if(best->duration)
      item->SetProperty("duration", StringUtils::SecondsToTimeString(best->duration));    

    /* handling of mimetypes fo directories are sub optimal at best */
    if(best->mime == "application/rss+xml" && StringUtils::StartsWithNoCase(item->GetPath(), "http://"))
      item->SetPath("rss://" + item->GetPath().substr(7));

    if(StringUtils::StartsWithNoCase(item->GetPath(), "rss://"))
      item->m_bIsFolder = true;
    else
      item->m_bIsFolder = false;
  }

  if(!item->m_strTitle.empty())
    item->SetLabel(item->m_strTitle);

  if(item->HasVideoInfoTag())
  {
    CVideoInfoTag* vtag = item->GetVideoInfoTag();

    if(item->HasProperty("duration")    && !vtag->GetDuration())
      vtag->m_duration = StringUtils::TimeStringToSeconds(item->GetProperty("duration").asString());

    if(item->HasProperty("description") && vtag->m_strPlot.empty())
      vtag->m_strPlot = item->GetProperty("description").asString();

    if(vtag->m_strPlotOutline.empty() && !vtag->m_strPlot.empty())
    {
      size_t pos = vtag->m_strPlot.find('\n');
      if(pos != std::string::npos)
        vtag->m_strPlotOutline = vtag->m_strPlot.substr(0, pos);
      else
        vtag->m_strPlotOutline = vtag->m_strPlot;
    }

    if(!vtag->GetDuration())
      item->SetLabel2(StringUtils::SecondsToTimeString(vtag->GetDuration()));
  }
}

bool CRSSDirectory::GetDirectory(const CStdString& path, CFileItemList &items)
{
  CStdString strPath(path);
  URIUtils::RemoveSlashAtEnd(strPath);
  std::map<CStdString,CDateTime>::iterator it;
  items.SetPath(strPath);
  CSingleLock lock(m_section);
  if ((it=m_cache.find(strPath)) != m_cache.end())
  {
    if (it->second > CDateTime::GetCurrentDateTime() && 
        items.Load())
      return true;
    m_cache.erase(it);
  }
  lock.Leave();

  CXBMCTinyXML xmlDoc;
  if (!xmlDoc.LoadFile(strPath))
  {
    CLog::Log(LOGERROR,"failed to load xml from <%s>. error: <%d>", strPath.c_str(), xmlDoc.ErrorId());
    return false;
  }
  if (xmlDoc.Error())
  {
    CLog::Log(LOGERROR,"error parsing xml doc from <%s>. error: <%d>", strPath.c_str(), xmlDoc.ErrorId());
    return false;
  }

  TiXmlElement* rssXmlNode = xmlDoc.RootElement();

  if (!rssXmlNode)
    return false;

  TiXmlHandle docHandle( &xmlDoc );
  TiXmlElement* channelXmlNode = docHandle.FirstChild( "rss" ).FirstChild( "channel" ).Element();
  if (channelXmlNode)
    ParseItem(&items, channelXmlNode, path);
  else
    return false;

  TiXmlElement* child = NULL;
  for (child = channelXmlNode->FirstChildElement("item"); child; child = child->NextSiblingElement())
  {
    // Create new item,
    CFileItemPtr item(new CFileItem());
    ParseItem(item.get(), child, path);

    item->SetProperty("isrss", "1");
    // Use channel image if item doesn't have one
    if (!item->HasArt("thumb") && items.HasArt("thumb"))
      item->SetArt("thumb", items.GetArt("thumb"));

    if (!item->GetPath().empty())
      items.Add(item);
  }

  items.AddSortMethod(SortByNone   , 231, LABEL_MASKS("%L", "%D", "%L", ""));    // FileName, Duration | Foldername, empty
  items.AddSortMethod(SortByLabel  , 551, LABEL_MASKS("%L", "%D", "%L", ""));    // FileName, Duration | Foldername, empty
  items.AddSortMethod(SortBySize   , 553, LABEL_MASKS("%L", "%I", "%L", "%I"));  // FileName, Size | Foldername, Size
  items.AddSortMethod(SortByDate   , 552, LABEL_MASKS("%L", "%J", "%L", "%J"));  // FileName, Date | Foldername, Date

  CDateTime time = CDateTime::GetCurrentDateTime();
  int mins = 60;
  TiXmlElement* ttl = docHandle.FirstChild("rss").FirstChild("ttl").Element();
  if (ttl)
    mins = strtol(ttl->FirstChild()->Value(),NULL,10);
  time += CDateTimeSpan(0,0,mins,0);
  items.SetPath(strPath);
  items.Save();
  CSingleLock lock2(m_section);
  m_cache.insert(make_pair(strPath,time));

  return true;
}

bool CRSSDirectory::Exists(const char* strPath)
{
  CCurlFile rss;
  CURL url(strPath);
  return rss.Exists(url);
}
