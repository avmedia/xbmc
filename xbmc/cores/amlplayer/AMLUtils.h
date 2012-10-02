#pragma once
/*
 *      Copyright (C) 2011-2012 Team XBMC
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

int aml_set_sysfs_str(const char *path, const char *val);
int aml_get_sysfs_str(const char *path, char *valstr, const int size);
int aml_set_sysfs_int(const char *path, const int val);
int aml_get_sysfs_int(const char *path);

bool aml_present();
void aml_cpufreq_limit(bool limit);
void aml_set_audio_passthrough(bool passthrough);
