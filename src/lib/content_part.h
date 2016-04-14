/*
    Copyright (C) 2016 Carl Hetherington <cth@carlh.net>

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

#ifndef DCPOMATIC_CONTENT_PART_H
#define DCPOMATIC_CONTENT_PART_H

#include "content.h"
#include <boost/weak_ptr.hpp>
#include <boost/thread/mutex.hpp>

class Content;
class Film;

class ContentPart
{
public:
	ContentPart (Content* parent, boost::shared_ptr<const Film> film)
		: _parent (parent)
		, _film (film)
	{}

protected:
	template <class T>
	void
	maybe_set (T& member, T new_value, int property) const
	{
		{
			boost::mutex::scoped_lock lm (_mutex);
			if (member == new_value) {
				return;
			}
			member = new_value;
		}
		_parent->signal_changed (property);
	}

	template <class T>
	void
	maybe_set (boost::optional<T>& member, T new_value, int property) const
	{
		{
			boost::mutex::scoped_lock lm (_mutex);
			if (member && member.get() == new_value) {
				return;
			}
			member = new_value;
		}
		_parent->signal_changed (property);
	}

	Content* _parent;
	boost::weak_ptr<const Film> _film;
	mutable boost::mutex _mutex;
};

#endif