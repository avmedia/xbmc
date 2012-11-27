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

#include "DVDInputStream.h"
#include "URL.h"

CDVDInputStream::CDVDInputStream(DVDStreamType streamType)
{
  m_streamType = streamType;
}

CDVDInputStream::~CDVDInputStream()
{
}

bool CDVDInputStream::Open(const char* strFile, const std::string &content)
{
  CURL url = CURL(strFile);

  // get rid of any |option parameters which might have sneaked in here
  // those are only handled by our curl impl.
  url.SetProtocolOptions("");
  m_strFileName = url.Get();

  m_content = content;
  return true;
}

void CDVDInputStream::Close()
{
  m_strFileName = "";
  m_item.Reset();
}

void CDVDInputStream::SetFileItem(const CFileItem& item)
{
  m_item = item;
}
