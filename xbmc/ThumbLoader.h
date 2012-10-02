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

#ifndef THUMBLOADER_H
#define THUMBLOADER_H
#include "BackgroundInfoLoader.h"
#include "utils/JobManager.h"
#include "FileItem.h"

#define kJobTypeMediaFlags "mediaflags"

class CStreamDetails;
class IStreamDetailsObserver;
class CVideoDatabase;
class CMusicDatabase;

/*!
 \ingroup thumbs,jobs
 \brief Thumb extractor job class

 Used by the CVideoThumbLoader to perform asynchronous generation of thumbs

 \sa CVideoThumbLoader and CJob
 */
class CThumbExtractor : public CJob
{
public:
  CThumbExtractor(const CFileItem& item, const CStdString& listpath, bool thumb, const CStdString& strTarget="");
  virtual ~CThumbExtractor();

  /*!
   \brief Work function that extracts thumb.
   */
  virtual bool DoWork();

  virtual const char* GetType() const
  {
    return kJobTypeMediaFlags;
  }

  virtual bool operator==(const CJob* job) const;

  CStdString m_path; ///< path of video to extract thumb from
  CStdString m_target; ///< thumbpath
  CStdString m_listpath; ///< path used in fileitem list
  CFileItem  m_item;
  bool       m_thumb; ///< extract thumb?
};

class CThumbLoader : public CBackgroundInfoLoader
{
public:
  CThumbLoader(int nThreads=-1);
  virtual ~CThumbLoader();

  virtual void Initialize() { };

  /*! \brief helper function to fill the art for a library item
   \param item a CFileItem
   \return true if we fill art, false otherwise
   */
  virtual bool FillLibraryArt(CFileItem &item) { return false; }

  /*! \brief Checks whether the given item has an image listed in the texture database
   \param item CFileItem to check
   \param type the type of image to retrieve
   \return the image associated with this item
   */
  static CStdString GetCachedImage(const CFileItem &item, const CStdString &type);

  /*! \brief Associate an image with the given item in the texture database
   \param item CFileItem to associate the image with
   \param type the type of image
   \param image the URL of the image
   */
  static void SetCachedImage(const CFileItem &item, const CStdString &type, const CStdString &image);
};

class CVideoThumbLoader : public CThumbLoader, public CJobQueue
{
public:
  CVideoThumbLoader();
  virtual ~CVideoThumbLoader();

  virtual void Initialize();
  virtual bool LoadItem(CFileItem* pItem);
  void SetStreamDetailsObserver(IStreamDetailsObserver *pObs) { m_pStreamDetailsObs = pObs; }

  /*! \brief Fill the thumb of a video item
   First uses a cached thumb from a previous run, then checks for a local thumb
   and caches it for the next run
   \param item the CFileItem object to fill
   \return true if we fill the thumb, false otherwise
   */
  static bool FillThumb(CFileItem &item);

  /*! \brief helper function to retrieve a thumb URL for embedded video thumbs
   \param item a video CFileItem.
   \return a URL for the embedded thumb.
   */
  static CStdString GetEmbeddedThumbURL(const CFileItem &item);

  /*! \brief helper function to fill the art for a video library item
   \param item a video CFileItem
   \return true if we fill art, false otherwise
   */
 virtual bool FillLibraryArt(CFileItem &item);

  /*!
   \brief Callback from CThumbExtractor on completion of a generated image

   Performs the callbacks and updates the GUI.

   \sa CImageLoader, IJobCallback
   */
  virtual void OnJobComplete(unsigned int jobID, bool success, CJob *job);

protected:
  virtual void OnLoaderStart();
  virtual void OnLoaderFinish();

  IStreamDetailsObserver *m_pStreamDetailsObs;
  CVideoDatabase *m_database;
};

class CProgramThumbLoader : public CThumbLoader
{
public:
  CProgramThumbLoader();
  virtual ~CProgramThumbLoader();
  virtual bool LoadItem(CFileItem* pItem);

  /*! \brief Fill the thumb of a programs item
   First uses a cached thumb from a previous run, then checks for a local thumb
   and caches it for the next run
   \param item the CFileItem object to fill
   \return true if we fill the thumb, false otherwise
   \sa GetLocalThumb
   */
  static bool FillThumb(CFileItem &item);

  /*! \brief Get a local thumb for a programs item
   Shortcuts are checked, then we check for a file or folder thumb
   \param item the CFileItem object to check
   \return the local thumb (if it exists)
   \sa FillThumb
   */
  static CStdString GetLocalThumb(const CFileItem &item);
};

namespace MUSIC_INFO
{
  class EmbeddedArt;
};

class CMusicThumbLoader : public CThumbLoader
{
public:
  CMusicThumbLoader();
  virtual ~CMusicThumbLoader();

  virtual void Initialize();
  virtual bool LoadItem(CFileItem* pItem);

  /*! \brief helper function to fill the art for a video library item
   \param item a video CFileItem
   \return true if we fill art, false otherwise
   */
  virtual bool FillLibraryArt(CFileItem &item);

  /*! \brief Fill the thumb of a music file/folder item
   First uses a cached thumb from a previous run, then checks for a local thumb
   and caches it for the next run
   \param item the CFileItem object to fill
   \return true if we fill the thumb, false otherwise
   */
  static bool FillThumb(CFileItem &item);

  static bool GetEmbeddedThumb(const std::string &path, MUSIC_INFO::EmbeddedArt &art);

protected:
  virtual void OnLoaderStart();
  virtual void OnLoaderFinish();

  CMusicDatabase *m_database;
};
#endif
