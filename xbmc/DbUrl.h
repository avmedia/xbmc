#pragma once
/*
 *      Copyright (C) 2012 Team XBMC
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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <map>
#include <string>

#include "URL.h"
#include "utils/UrlOptions.h"

class CDbUrl : public CUrlOptions
{
public:
  CDbUrl();
  virtual ~CDbUrl();

  bool IsValid() const { return m_valid; }
  void Reset();

  std::string ToString() const;
  bool FromString(const std::string &dbUrl);

  const std::string& GetType() const { return m_type; }
  void AppendPath(const std::string &subPath);

  virtual void AddOption(const std::string &key, const char *value) { CUrlOptions::AddOption(key, value); updateOptions(); }
  virtual void AddOption(const std::string &key, const std::string &value) { CUrlOptions::AddOption(key, value); updateOptions(); }
  virtual void AddOption(const std::string &key, int value) { CUrlOptions::AddOption(key, value); updateOptions(); }
  virtual void AddOption(const std::string &key, float value) { CUrlOptions::AddOption(key, value); updateOptions(); }
  virtual void AddOption(const std::string &key, double value) { CUrlOptions::AddOption(key, value); updateOptions(); }
  virtual void AddOption(const std::string &key, bool value) { CUrlOptions::AddOption(key, value); updateOptions(); }
  virtual void AddOptions(const std::string &options) { CUrlOptions::AddOptions(options); updateOptions(); }

protected:
  virtual bool parse() = 0;
  
  CURL m_url;
  std::string m_type;

private:
  void updateOptions();

  bool m_valid;
};
