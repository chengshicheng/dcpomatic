/*
    Copyright (C) 2012-2018 Carl Hetherington <cth@carlh.net>

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

#include "update_dialog.h"
#include "static_text.h"
#include "wx_util.h"
#include <wx/hyperlink.h>

using std::string;
using boost::optional;

UpdateDialog::UpdateDialog (wxWindow* parent, optional<string> stable, optional<string> test)
	: wxDialog (parent, wxID_ANY, _("Update"))
{
	wxBoxSizer* overall_sizer = new wxBoxSizer (wxVERTICAL);

	wxStaticText* message;

	if ((stable || test) && !(stable && test)) {
		message = new StaticText (this, _("A new version of DCP-o-matic is available."));
	} else {
		message = new StaticText (this, _("New versions of DCP-o-matic are available."));
	}

	overall_sizer->Add (message, 1, wxTOP | wxLEFT | wxRIGHT, DCPOMATIC_DIALOG_BORDER);

	wxFlexGridSizer* table = new wxFlexGridSizer (2, DCPOMATIC_SIZER_X_GAP, DCPOMATIC_SIZER_Y_GAP);

	if (stable) {
		add_label_to_sizer (table, this, _("Stable version ") + std_to_wx (stable.get ()), true);
		wxHyperlinkCtrl* h = new wxHyperlinkCtrl (this, wxID_ANY, "dcpomatic.com/download", "https://dcpomatic.com/download");
		table->Add (h);
	}

	if (test) {
		add_label_to_sizer (table, this, _("Test version ") + std_to_wx (test.get ()), true);
		wxHyperlinkCtrl* h = new wxHyperlinkCtrl (this, wxID_ANY, "dcpomatic.com/test-download", "https://dcpomatic.com/test-download");
		table->Add (h);
	}

	overall_sizer->Add (table, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, DCPOMATIC_DIALOG_BORDER);

	wxSizer* buttons = CreateButtonSizer (wxOK);
	if (buttons) {
		overall_sizer->Add (buttons, 1, wxEXPAND | wxALL);
	}

	SetSizerAndFit (overall_sizer);
}
