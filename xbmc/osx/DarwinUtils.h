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
#ifndef _DARWIN_UTILS_H_
#define _DARWIN_UTILS_H_

#include <string>

// We forward declare CFStringRef in order to avoid
// pulling in tons of Objective-C headers.
struct __CFString;
typedef const struct __CFString * CFStringRef;

#ifdef __cplusplus
extern "C"
{
#endif
  bool        DarwinIsAppleTV2(void);
  bool        DarwinIsMavericks(void);
  bool        DarwinHasRetina(void);
  const char *GetDarwinOSReleaseString(void);
  const char *GetDarwinVersionString(void);
  float       GetIOSVersion(void);
  int         GetDarwinFrameworkPath(bool forPython, char* path, uint32_t *pathsize);
  int         GetDarwinExecutablePath(char* path, uint32_t *pathsize);
  const char *DarwinGetXbmcRootFolder(void);
  bool        DarwinIsIosSandboxed(void);
  bool        DarwinHasVideoToolboxDecoder(void);
  int         DarwinBatteryLevel(void);
  void        DarwinSetScheduling(int message);
  bool        DarwinCFStringRefToString(CFStringRef source, std::string& destination);
  bool        DarwinCFStringRefToUTF8String(CFStringRef source, std::string& destination);
#ifdef __cplusplus
}
#endif

#endif
