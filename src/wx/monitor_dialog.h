/*
    Copyright (C) 2012 Carl Hetherington <cth@carlh.net>

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

#include "table_dialog.h"
#include "lib/edid.h"
#include <boost/optional.hpp>

class MonitorDialog : public TableDialog
{
public:
	explicit MonitorDialog (wxWindow *);

	void set (Monitor);
	boost::optional<Monitor> get () const;

private:
	wxTextCtrl* _manufacturer_id;
	wxTextCtrl* _manufacturer_product_code;
	wxTextCtrl* _serial_number;
	wxTextCtrl* _week_of_manufacture;
	wxTextCtrl* _year_of_manufacture;
};