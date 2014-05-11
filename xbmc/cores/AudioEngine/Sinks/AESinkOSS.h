#pragma once
/*
 *      Copyright (C) 2010-2013 Team XBMC
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

#include "cores/AudioEngine/Interfaces/AESink.h"
#include "cores/AudioEngine/Utils/AEDeviceInfo.h"
#include <stdint.h>

#include "threads/CriticalSection.h"

class CAESinkOSS : public IAESink
{
public:
  virtual const char *GetName() { return "OSS"; }

  CAESinkOSS();
  virtual ~CAESinkOSS();

  virtual bool Initialize  (AEAudioFormat &format, std::string &device);
  virtual void Deinitialize();

  virtual void         Stop            ();
  virtual double       GetDelay        ();
  virtual double       GetCacheTotal   () { return 0.0; } /* FIXME */
  virtual unsigned int AddPackets      (uint8_t *data, unsigned int frames, bool hasAudio, bool blocking = false);
  virtual void         Drain           ();
  static  void         EnumerateDevicesEx(AEDeviceInfoList &list, bool force = false);
private:
  int m_fd;
  bool m_blockingNeedsUpdate;
  std::string      m_device;
  AEAudioFormat   m_initFormat;
  AEAudioFormat   m_format;

  CAEChannelInfo  GetChannelLayout(AEAudioFormat format);
  std::string      GetDeviceUse(const AEAudioFormat format, const std::string &device);
};

