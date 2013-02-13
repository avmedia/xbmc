#pragma once

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

#include "DVDAudioCodec.h"

class CDVDAudioCodecPcm : public CDVDAudioCodec
{
public:
  CDVDAudioCodecPcm();
  virtual ~CDVDAudioCodecPcm();
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  virtual int Decode(BYTE* pData, int iSize);
  virtual int GetData(BYTE** dst);
  virtual void Reset();
  virtual int GetChannels();
  virtual CAEChannelInfo GetChannelMap();
  virtual int GetSampleRate();
  virtual enum AEDataFormat GetDataFormat();
  virtual const char* GetName()  { return "pcm"; }

protected:
  virtual void SetDefault();

  short m_decodedData[131072]; // could be a bit to big
  int m_decodedDataSize;

  CodecID m_codecID;
  int m_iSourceSampleRate;
  int m_iSourceChannels;
  int m_iSourceBitrate;

  int m_iOutputChannels;

  short table[256];
};
