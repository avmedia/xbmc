#ifndef SID_CODEC_H_
#define SID_CODEC_H_

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

#include "ICodec.h"
#include "DllSidplay2.h"

class SIDCodec : public ICodec
{
public:
  SIDCodec();
  virtual ~SIDCodec();

  virtual bool Init(const CStdString &strFile, unsigned int filecache);
  virtual void DeInit();
  virtual int64_t Seek(int64_t iSeekTime);
  virtual int ReadPCM(BYTE *pBuffer, int size, int *actualsize);
  virtual bool CanInit();
  virtual CAEChannelInfo GetChannelInfo();

  virtual void SetTotalTime(int64_t totaltime)
  {
    m_TotalTime = totaltime*1000;
  }
private:
  DllSidplay2 m_dll;
  void* m_sid;
  int m_iTrack;
  int64_t m_iDataPos;
};

#endif

