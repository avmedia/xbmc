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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "music/tags/MusicInfoTag.h"
#include "AddonClass.h"

#pragma once

namespace XBMCAddon
{
  namespace xbmc
  {
    class InfoTagMusic : public AddonClass
    {
    private:
      MUSIC_INFO::CMusicInfoTag* infoTag;

    public:
#ifndef SWIG
      InfoTagMusic(const MUSIC_INFO::CMusicInfoTag& tag);
#endif
      InfoTagMusic();
      virtual ~InfoTagMusic();

      String getURL();
      String getTitle();
      String getArtist();
      String getAlbum();
      String getAlbumArtist();
      String getGenre();
      int getDuration();
      int getTrack();
      int getDisc();
      String getReleaseDate();

      int getListeners();
      int getPlayCount();
      String getLastPlayed();
      String getComment();
      String getLyrics();

    };
  }
}
  
