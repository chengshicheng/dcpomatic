/*
    Copyright (C) 2012 Carl Hetherington <cth@carlh.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <boost/filesystem.hpp>

#ifdef DCPOMATIC_WINDOWS
#define WEXITSTATUS(w) (w)
#endif

class Log;

void dcpomatic_sleep (int);
extern std::string cpu_info ();
extern void run_ffprobe (boost::filesystem::path, boost::filesystem::path, boost::shared_ptr<Log>);
extern std::list<std::pair<std::string, std::string> > mount_info ();
extern boost::filesystem::path openssl_path ();
#ifdef DCPOMATIC_OSX
extern boost::filesystem::path app_contents ();
#endif
extern FILE * fopen_boost (boost::filesystem::path, std::string);
