/*
    Copyright (C) 2018 Carl Hetherington <cth@carlh.net>

    This file is part of DCP-o-matic.

    DCP-o-matic is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DCP-o-matic is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DCP-o-matic.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "spl.h"
#include "spl_entry.h"
#include <dcp/cpl.h>
#include <dcp/dcp.h>
#include <libxml++/libxml++.h>
#include <boost/foreach.hpp>

using boost::shared_ptr;

SPL::SPL (boost::filesystem::path file)
{
	cxml::Document f ("DCPPlaylist");
	f.read_file (file);

	name = f.string_attribute ("Name");
	BOOST_FOREACH (cxml::ConstNodePtr i, f.node_children("DCP")) {
		boost::filesystem::path dir(i->content());
		dcp::DCP dcp (dir);
		dcp.read ();
		BOOST_FOREACH (shared_ptr<dcp::CPL> j, dcp.cpls()) {
			if (j->id() == i->string_attribute("CPL")) {
				playlist.push_back (SPLEntry(j, dir));
			}
		}
	}
}

void
SPL::as_xml (boost::filesystem::path file) const
{
	xmlpp::Document doc;
	xmlpp::Element* root = doc.create_root_node ("DCPPlaylist");
	root->set_attribute ("Name", name);

	BOOST_FOREACH (SPLEntry i, playlist) {
		xmlpp::Element* d = root->add_child ("DCP");
		d->set_attribute ("CPL", i.cpl->id());
		d->add_child_text (i.directory.string());
	}

	doc.write_to_file_formatted(file.string());
}