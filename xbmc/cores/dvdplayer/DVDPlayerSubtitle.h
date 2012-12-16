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

#include "DVDOverlayContainer.h"
#include "DVDSubtitles/DVDFactorySubtitle.h"
#include "DVDStreamInfo.h"
#include "DVDMessageQueue.h"
#include "DVDDemuxSPU.h"

class CDVDInputStream;
class CDVDSubtitleStream;
class CDVDSubtitleParser;
class CDVDInputStreamNavigator;
class CDVDOverlayCodec;

class CDVDPlayerSubtitle
{
public:
  CDVDPlayerSubtitle(CDVDOverlayContainer* pOverlayContainer);
  ~CDVDPlayerSubtitle();

  void Process(double pts);
  void Flush();
  void FindSubtitles(const char* strFilename);
  void GetCurrentSubtitle(CStdString& strSubtitle, double pts);
  int GetSubtitleCount();

  void UpdateOverlayInfo(CDVDInputStreamNavigator* pStream, int iAction) { m_pOverlayContainer->UpdateOverlayInfo(pStream, &m_dvdspus, iAction); }

  bool AcceptsData();
  void SendMessage(CDVDMsg* pMsg);
  bool OpenStream(CDVDStreamInfo &hints, std::string& filename);
  void CloseStream(bool flush);

  bool IsStalled() { return m_pOverlayContainer->GetSize() == 0; }
private:
  CDVDOverlayContainer* m_pOverlayContainer;

  CDVDSubtitleStream* m_pSubtitleStream;
  CDVDSubtitleParser* m_pSubtitleFileParser;
  CDVDOverlayCodec*   m_pOverlayCodec;
  CDVDDemuxSPU        m_dvdspus;

  CDVDStreamInfo      m_streaminfo;
  double              m_lastPts;


  CCriticalSection    m_section;
};


//typedef struct SubtitleInfo
//{

//
//} SubtitleInfo;

