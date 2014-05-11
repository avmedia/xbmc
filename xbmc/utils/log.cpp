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

#include "system.h"
#include "log.h"
#include "stdio_utf8.h"
#include "stat_utf8.h"
#include "threads/CriticalSection.h"
#include "threads/SingleLock.h"
#include "threads/Thread.h"
#include "utils/StdString.h"
#include "utils/StringUtils.h"
#if defined(TARGET_ANDROID)
#include "android/activity/XBMCApp.h"
#elif defined(TARGET_WINDOWS)
#include "win32/WIN32Util.h"
#endif

#define critSec XBMC_GLOBAL_USE(CLog::CLogGlobals).critSec
#define m_file XBMC_GLOBAL_USE(CLog::CLogGlobals).m_file
#define m_repeatCount XBMC_GLOBAL_USE(CLog::CLogGlobals).m_repeatCount
#define m_repeatLogLevel XBMC_GLOBAL_USE(CLog::CLogGlobals).m_repeatLogLevel
#define m_repeatLine XBMC_GLOBAL_USE(CLog::CLogGlobals).m_repeatLine
#define m_logLevel XBMC_GLOBAL_USE(CLog::CLogGlobals).m_logLevel
#define m_extraLogLevels XBMC_GLOBAL_USE(CLog::CLogGlobals).m_extraLogLevels

static char levelNames[][8] =
{"DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "SEVERE", "FATAL", "NONE"};

CLog::CLog()
{}

CLog::~CLog()
{}

void CLog::Close()
{
  
  CSingleLock waitLock(critSec);
  if (m_file)
  {
    fclose(m_file);
    m_file = NULL;
  }
  m_repeatLine.clear();
}

void CLog::Log(int loglevel, const char *format, ... )
{
  static const char* prefixFormat = "%02.2d:%02.2d:%02.2d T:%"PRIu64" %7s: ";
  CSingleLock waitLock(critSec);
  int extras = (loglevel >> LOGMASKBIT) << LOGMASKBIT;
  loglevel = loglevel & LOGMASK;
#if !(defined(_DEBUG) || defined(PROFILE))
  if (m_logLevel > LOG_LEVEL_NORMAL ||
     (m_logLevel > LOG_LEVEL_NONE && loglevel >= LOGNOTICE))
#endif
  {
    if (!m_file)
      return;

    if (extras != 0 && (m_extraLogLevels & extras) == 0)
      return;

    SYSTEMTIME time;
    GetLocalTime(&time);

    CStdString strPrefix, strData;

    strData.reserve(16384);
    va_list va;
    va_start(va, format);
    strData = StringUtils::FormatV(format,va);
    va_end(va);

    if (m_repeatLogLevel == loglevel && m_repeatLine == strData)
    {
      m_repeatCount++;
      return;
    }
    else if (m_repeatCount)
    {
      strPrefix = StringUtils::Format(prefixFormat,
                                      time.wHour,
                                      time.wMinute,
                                      time.wSecond,
                                      (uint64_t)CThread::GetCurrentThreadId(),
                                      levelNames[m_repeatLogLevel]);

      CStdString strData2 = StringUtils::Format("Previous line repeats %d times."
                                                LINE_ENDING,
                                                m_repeatCount);
      fputs(strPrefix.c_str(), m_file);
      fputs(strData2.c_str(), m_file);
      OutputDebugString(strData2);
      m_repeatCount = 0;
    }
    
    m_repeatLine      = strData;
    m_repeatLogLevel  = loglevel;

    StringUtils::TrimRight(strData);
    if (strData.empty())
      return;
    
    OutputDebugString(strData);

    /* fixup newline alignment, number of spaces should equal prefix length */
    StringUtils::Replace(strData, "\n", LINE_ENDING"                                            ");
    strData += LINE_ENDING;

    strPrefix = StringUtils::Format(prefixFormat,
                                    time.wHour,
                                    time.wMinute,
                                    time.wSecond,
                                    (uint64_t)CThread::GetCurrentThreadId(),
                                    levelNames[loglevel]);

//print to adb
#if defined(TARGET_ANDROID) && defined(_DEBUG)
  CXBMCApp::android_printf("%s%s",strPrefix.c_str(), strData.c_str());
#endif

    fputs(strPrefix.c_str(), m_file);
    fputs(strData.c_str(), m_file);
    fflush(m_file);
  }
}

bool CLog::Init(const char* path)
{
  CSingleLock waitLock(critSec);
  if (!m_file)
  {
    // the log folder location is initialized in the CAdvancedSettings
    // constructor and changed in CApplication::Create()
    CStdString strLogFile = StringUtils::Format("%sxbmc.log", path);
    CStdString strLogFileOld = StringUtils::Format("%sxbmc.old.log", path);

#if defined(TARGET_WINDOWS)
    // the appdata folder might be redirected to an unc share
    // convert smb to unc path that stat and fopen can handle it
    strLogFile = CWIN32Util::SmbToUnc(strLogFile);
    strLogFileOld = CWIN32Util::SmbToUnc(strLogFileOld);
#endif

    struct stat64 info;
    if (stat64_utf8(strLogFileOld.c_str(),&info) == 0 &&
        remove_utf8(strLogFileOld.c_str()) != 0)
      return false;
    if (stat64_utf8(strLogFile.c_str(),&info) == 0 &&
        rename_utf8(strLogFile.c_str(),strLogFileOld.c_str()) != 0)
      return false;

    m_file = fopen64_utf8(strLogFile.c_str(),"wb");
  }

  if (m_file)
  {
    unsigned char BOM[3] = {0xEF, 0xBB, 0xBF};
    fwrite(BOM, sizeof(BOM), 1, m_file);
  }

  return m_file != NULL;
}

void CLog::MemDump(char *pData, int length)
{
  Log(LOGDEBUG, "MEM_DUMP: Dumping from %p", pData);
  for (int i = 0; i < length; i+=16)
  {
    CStdString strLine = StringUtils::Format("MEM_DUMP: %04x ", i);
    char *alpha = pData;
    for (int k=0; k < 4 && i + 4*k < length; k++)
    {
      for (int j=0; j < 4 && i + 4*k + j < length; j++)
      {
        CStdString strFormat = StringUtils::Format(" %02x", (unsigned char)*pData++);
        strLine += strFormat;
      }
      strLine += " ";
    }
    // pad with spaces
    while (strLine.size() < 13*4 + 16)
      strLine += " ";
    for (int j=0; j < 16 && i + j < length; j++)
    {
      if (*alpha > 31)
        strLine += *alpha;
      else
        strLine += '.';
      alpha++;
    }
    Log(LOGDEBUG, "%s", strLine.c_str());
  }
}

void CLog::SetLogLevel(int level)
{
  CSingleLock waitLock(critSec);
  m_logLevel = level;
  CLog::Log(LOGNOTICE, "Log level changed to %d", m_logLevel);
}

int CLog::GetLogLevel()
{
  return m_logLevel;
}

void CLog::SetExtraLogLevels(int level)
{
  CSingleLock waitLock(critSec);
  m_extraLogLevels = level;
}

void CLog::OutputDebugString(const std::string& line)
{
#if defined(_DEBUG) || defined(PROFILE)
#if defined(TARGET_WINDOWS)
  // we can't use charsetconverter here as it's initialized later than CLog and deinitialized early
  int bufSize = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, NULL, 0);
  CStdStringW wstr (L"", bufSize);
  if ( MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wstr.GetBuf(bufSize), bufSize) == bufSize )
  {
    wstr.RelBuf();
    ::OutputDebugStringW(wstr.c_str());
  }
  else
#endif // TARGET_WINDOWS
    ::OutputDebugString(line.c_str());
  ::OutputDebugString("\n");
#endif
}
