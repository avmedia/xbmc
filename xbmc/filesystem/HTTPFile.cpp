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

#include "HTTPFile.h"

using namespace XFILE;

CHTTPFile::CHTTPFile(void)
{
  m_openedforwrite = false;
}


CHTTPFile::~CHTTPFile(void)
{
}

bool CHTTPFile::OpenForWrite(const CURL& url, bool bOverWrite)
{
  // real Open is delayed until we receive the POST data
  m_urlforwrite = url;
  m_openedforwrite = true;
  return true;
}

int CHTTPFile::Write(const void* lpBuf, int64_t uiBufSize)
{
  // Although we can not verify much, try to catch errors where we can
  if (!m_openedforwrite)
    return -1;

  CStdString myPostData((char*) lpBuf);
  if ((int64_t)myPostData.length() != uiBufSize)
    return -1;

  // If we get here, we (most likely) satisfied the pre-conditions that we used OpenForWrite and passed a string as postdata
  // we mimic 'post(..)' but do not read any data
  m_postdata = myPostData;
  m_postdataset = true;
  m_openedforwrite = false;
  SetMimeType("application/json");
  if (!Open(m_urlforwrite))
    return -1;

  // Finally (and this is a clumsy hack) return the http response code
  return (int) m_httpresponse;
}

