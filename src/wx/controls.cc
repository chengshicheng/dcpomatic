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

#include "controls.h"
#include "film_viewer.h"
#include "wx_util.h"
#include "playhead_to_timecode_dialog.h"
#include "playhead_to_frame_dialog.h"
#include "lib/job_manager.h"
#include "lib/player_video.h"
#include "lib/dcp_content.h"
#include <dcp/dcp.h>
#include <dcp/cpl.h>
#include <dcp/reel.h>
#include <dcp/reel_picture_asset.h>
#include <wx/wx.h>
#include <wx/tglbtn.h>
#include <wx/listctrl.h>

using std::string;
using std::list;
using std::make_pair;
using boost::optional;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::dynamic_pointer_cast;

Controls::Controls (wxWindow* parent, shared_ptr<FilmViewer> viewer, bool editor_controls)
	: wxPanel (parent)
	, _viewer (viewer)
	, _slider_being_moved (false)
	, _was_running_before_slider (false)
	, _outline_content (0)
	, _eye (0)
	, _jump_to_selected (0)
	, _slider (new wxSlider (this, wxID_ANY, 0, 0, 4096))
	, _rewind_button (new wxButton (this, wxID_ANY, wxT("|<")))
	, _back_button (new wxButton (this, wxID_ANY, wxT("<")))
	, _forward_button (new wxButton (this, wxID_ANY, wxT(">")))
	, _frame_number (new wxStaticText (this, wxID_ANY, wxT("")))
	, _timecode (new wxStaticText (this, wxID_ANY, wxT("")))
#ifdef DCPOMATIC_VARIANT_SWAROOP
	, _play_button (new wxButton(this, wxID_ANY, _("Play")))
	, _pause_button (new wxButton(this, wxID_ANY, _("Pause")))
	, _stop_button (new wxButton(this, wxID_ANY, _("Stop")))
#else
	, _play_button (new wxToggleButton(this, wxID_ANY, _("Play")))
#endif
{
	_v_sizer = new wxBoxSizer (wxVERTICAL);
	SetSizer (_v_sizer);

	wxBoxSizer* view_options = new wxBoxSizer (wxHORIZONTAL);
	if (editor_controls) {
		_outline_content = new wxCheckBox (this, wxID_ANY, _("Outline content"));
		view_options->Add (_outline_content, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, DCPOMATIC_SIZER_GAP);
		_eye = new wxChoice (this, wxID_ANY);
		_eye->Append (_("Left"));
		_eye->Append (_("Right"));
		_eye->SetSelection (0);
		view_options->Add (_eye, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, DCPOMATIC_SIZER_GAP);
		_jump_to_selected = new wxCheckBox (this, wxID_ANY, _("Jump to selected content"));
		view_options->Add (_jump_to_selected, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, DCPOMATIC_SIZER_GAP);
	}

	_v_sizer->Add (view_options, 0, wxALL, DCPOMATIC_SIZER_GAP);

	wxBoxSizer* e_sizer = new wxBoxSizer (wxHORIZONTAL);

	_cpl = new wxListCtrl (this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_NO_HEADER);
	/* time */
	_cpl->AppendColumn (wxT(""), wxLIST_FORMAT_LEFT, 80);
	/* type */
	_cpl->AppendColumn (wxT(""), wxLIST_FORMAT_LEFT, 80);
	/* annotation text */
	_cpl->AppendColumn (wxT(""), wxLIST_FORMAT_LEFT, 580);
	e_sizer->Add (_cpl, 1, wxALL | wxEXPAND, DCPOMATIC_SIZER_GAP);

	_spl_view = new wxListCtrl (this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_NO_HEADER);
	_spl_view->AppendColumn (wxT(""), wxLIST_FORMAT_LEFT, 80);
	_spl_view->AppendColumn (wxT(""), wxLIST_FORMAT_LEFT, 80);
	_spl_view->AppendColumn (wxT(""), wxLIST_FORMAT_LEFT, 580);
	e_sizer->Add (_spl_view, 1, wxALL | wxEXPAND, DCPOMATIC_SIZER_GAP);

	wxBoxSizer* buttons_sizer = new wxBoxSizer (wxVERTICAL);
	_add_button = new wxButton(this, wxID_ANY, _("Add"));
	buttons_sizer->Add (_add_button);
	e_sizer->Add (buttons_sizer, 0, wxALL | wxEXPAND, DCPOMATIC_SIZER_GAP);

	_v_sizer->Add (e_sizer, 1, wxEXPAND);

	_log = new wxTextCtrl (this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(-1, 200), wxTE_READONLY | wxTE_MULTILINE);
	_v_sizer->Add (_log, 0, wxALL | wxEXPAND, DCPOMATIC_SIZER_GAP);

	_cpl->Show (false);
	_spl_view->Show (false);
	_add_button->Show (false);
	_log->Show (false);

	wxBoxSizer* h_sizer = new wxBoxSizer (wxHORIZONTAL);

	wxBoxSizer* time_sizer = new wxBoxSizer (wxVERTICAL);
	time_sizer->Add (_frame_number, 0, wxEXPAND);
	time_sizer->Add (_timecode, 0, wxEXPAND);

	h_sizer->Add (_rewind_button, 0, wxALL, 2);
	h_sizer->Add (_back_button, 0, wxALL, 2);
	h_sizer->Add (time_sizer, 0, wxEXPAND);
	h_sizer->Add (_forward_button, 0, wxALL, 2);
	h_sizer->Add (_play_button, 0, wxEXPAND);
#ifdef DCPOMATIC_VARIANT_SWAROOP
	h_sizer->Add (_pause_button, 0, wxEXPAND);
	h_sizer->Add (_stop_button, 0, wxEXPAND);
#endif
	h_sizer->Add (_slider, 1, wxEXPAND);

	_v_sizer->Add (h_sizer, 0, wxEXPAND | wxALL, 6);

	_frame_number->SetMinSize (wxSize (84, -1));
	_rewind_button->SetMinSize (wxSize (32, -1));
	_back_button->SetMinSize (wxSize (32, -1));
	_forward_button->SetMinSize (wxSize (32, -1));

	if (_eye) {
		_eye->Bind (wxEVT_CHOICE, boost::bind (&Controls::eye_changed, this));
	}
	if (_outline_content) {
		_outline_content->Bind (wxEVT_CHECKBOX, boost::bind (&Controls::outline_content_changed, this));
	}

	_slider->Bind           (wxEVT_SCROLL_THUMBTRACK,    boost::bind(&Controls::slider_moved,    this, false));
	_slider->Bind           (wxEVT_SCROLL_PAGEUP,        boost::bind(&Controls::slider_moved,    this, true));
	_slider->Bind           (wxEVT_SCROLL_PAGEDOWN,      boost::bind(&Controls::slider_moved,    this, true));
	_slider->Bind           (wxEVT_SCROLL_CHANGED,       boost::bind(&Controls::slider_released, this));
#ifdef DCPOMATIC_VARIANT_SWAROOP
	_play_button->Bind      (wxEVT_BUTTON,               boost::bind(&Controls::play_clicked,    this));
	_pause_button->Bind     (wxEVT_BUTTON,               boost::bind(&Controls::pause_clicked,   this));
	_stop_button->Bind      (wxEVT_BUTTON,               boost::bind(&Controls::stop_clicked,    this));
#else
	_play_button->Bind      (wxEVT_TOGGLEBUTTON,         boost::bind(&Controls::play_clicked,    this));
#endif
	_rewind_button->Bind    (wxEVT_LEFT_DOWN,            boost::bind(&Controls::rewind_clicked,  this, _1));
	_back_button->Bind      (wxEVT_LEFT_DOWN,            boost::bind(&Controls::back_clicked,    this, _1));
	_forward_button->Bind   (wxEVT_LEFT_DOWN,            boost::bind(&Controls::forward_clicked, this, _1));
	_frame_number->Bind     (wxEVT_LEFT_DOWN,            boost::bind(&Controls::frame_number_clicked, this));
	_timecode->Bind         (wxEVT_LEFT_DOWN,            boost::bind(&Controls::timecode_clicked, this));
	_cpl->Bind              (wxEVT_LIST_ITEM_SELECTED,   boost::bind(&Controls::setup_sensitivity, this));
	_cpl->Bind              (wxEVT_LIST_ITEM_DESELECTED, boost::bind(&Controls::setup_sensitivity, this));
	if (_jump_to_selected) {
		_jump_to_selected->Bind (wxEVT_CHECKBOX, boost::bind (&Controls::jump_to_selected_clicked, this));
		_jump_to_selected->SetValue (Config::instance()->jump_to_selected ());
	}
	_add_button->Bind       (wxEVT_BUTTON,              boost::bind(&Controls::add_clicked, this));

	_viewer->PositionChanged.connect (boost::bind(&Controls::position_changed, this));
	_viewer->Started.connect (boost::bind(&Controls::started, this));
	_viewer->Stopped.connect (boost::bind(&Controls::stopped, this));
	_viewer->FilmChanged.connect (boost::bind(&Controls::film_changed, this));
	_viewer->ImageChanged.connect (boost::bind(&Controls::image_changed, this, _1));

	film_changed ();

	setup_sensitivity ();
	update_dcp_directory ();

	JobManager::instance()->ActiveJobsChanged.connect (
		bind (&Controls::active_jobs_changed, this, _2)
		);

	_config_changed_connection = Config::instance()->Changed.connect (bind(&Controls::config_changed, this, _1));
}

void
Controls::add_clicked ()
{
	optional<CPL> sel = selected_cpl ();
	DCPOMATIC_ASSERT (sel);
	_spl.push_back (SPLEntry(sel->first, sel->second));
	add_cpl_to_list (sel->first, _spl_view);
	SPLChanged (_spl);
	setup_sensitivity ();
}

void
Controls::config_changed (int property)
{
	if (property == Config::PLAYER_DCP_DIRECTORY) {
		update_dcp_directory ();
	}
}

void
Controls::started ()
{
#ifdef DCPOMATIC_VARIANT_SWAROOP
	_play_button->Enable (false);
	_pause_button->Enable (true);
#else
	_play_button->SetValue (true);
#endif
	setup_sensitivity ();
}

void
Controls::stopped ()
{
#ifdef DCPOMATIC_VARIANT_SWAROOP
	_play_button->Enable (true);
	_pause_button->Enable (false);
#else
	_play_button->SetValue (false);
#endif
	setup_sensitivity ();
}

void
Controls::position_changed ()
{
	if (!_slider_being_moved) {
		update_position_label ();
		update_position_slider ();
	}
}

void
Controls::eye_changed ()
{
	_viewer->set_eyes (_eye->GetSelection() == 0 ? EYES_LEFT : EYES_RIGHT);
}

void
Controls::outline_content_changed ()
{
	_viewer->set_outline_content (_outline_content->GetValue());
}

void
Controls::film_change (ChangeType type, Film::Property p)
{
	if (type != CHANGE_TYPE_DONE) {
		return;
	}

	if (p == Film::CONTENT || p == Film::THREE_D) {
		setup_sensitivity ();
	}
}

/** @param page true if this was a PAGEUP/PAGEDOWN event for which we won't receive a THUMBRELEASE */
void
Controls::slider_moved (bool page)
{
	if (!_film) {
		return;
	}

	if (!page && !_slider_being_moved) {
		/* This is the first event of a drag; stop playback for the duration of the drag */
		_was_running_before_slider = _viewer->stop ();
		_slider_being_moved = true;
	}

	DCPTime t (_slider->GetValue() * _film->length().get() / 4096);
	t = t.round (_film->video_frame_rate());
	/* Ensure that we hit the end of the film at the end of the slider */
	if (t >= _film->length ()) {
		t = _film->length() - _viewer->one_video_frame();
	}
	_viewer->seek (t, false);
	update_position_label ();
}

void
Controls::slider_released ()
{
	if (_was_running_before_slider) {
		/* Restart after a drag */
		_viewer->start ();
	}
	_slider_being_moved = false;
}

void
Controls::play_clicked ()
{
#ifdef DCPOMATIC_VARIANT_SWAROOP
	_viewer->start ();
#else
	check_play_state ();
#endif
}


#ifndef DCPOMATIC_VARIANT_SWAROOP
void
Controls::check_play_state ()
{
	if (!_film || _film->video_frame_rate() == 0) {
		return;
	}

	if (_play_button->GetValue()) {
		_viewer->start ();
	} else {
		_viewer->stop ();
	}
}
#endif

void
Controls::update_position_slider ()
{
	if (!_film) {
		_slider->SetValue (0);
		return;
	}

	DCPTime const len = _film->length ();

	if (len.get ()) {
		int const new_slider_position = 4096 * _viewer->position().get() / len.get();
		if (new_slider_position != _slider->GetValue()) {
			_slider->SetValue (new_slider_position);
		}
	}
}

void
Controls::update_position_label ()
{
	if (!_film) {
		_frame_number->SetLabel ("0");
		_timecode->SetLabel ("0:0:0.0");
		return;
	}

	double const fps = _film->video_frame_rate ();
	/* Count frame number from 1 ... not sure if this is the best idea */
	_frame_number->SetLabel (wxString::Format (wxT("%ld"), lrint (_viewer->position().seconds() * fps) + 1));
	_timecode->SetLabel (time_to_timecode (_viewer->position(), fps));
}

void
Controls::active_jobs_changed (optional<string> j)
{
	_active_job = j;
	setup_sensitivity ();
}

DCPTime
Controls::nudge_amount (wxKeyboardState& ev)
{
	DCPTime amount = _viewer->one_video_frame ();

	if (ev.ShiftDown() && !ev.ControlDown()) {
		amount = DCPTime::from_seconds (1);
	} else if (!ev.ShiftDown() && ev.ControlDown()) {
		amount = DCPTime::from_seconds (10);
	} else if (ev.ShiftDown() && ev.ControlDown()) {
		amount = DCPTime::from_seconds (60);
	}

	return amount;
}

void
Controls::rewind_clicked (wxMouseEvent& ev)
{
	_viewer->seek (DCPTime(), true);
	ev.Skip();
}

void
Controls::back_frame ()
{
	_viewer->seek_by (-_viewer->one_video_frame(), true);
}

void
Controls::forward_frame ()
{
	_viewer->seek_by (_viewer->one_video_frame(), true);
}

void
Controls::back_clicked (wxKeyboardState& ev)
{
	_viewer->seek_by (-nudge_amount(ev), true);
}

void
Controls::forward_clicked (wxKeyboardState& ev)
{
	_viewer->seek_by (nudge_amount(ev), true);
}

void
Controls::setup_sensitivity ()
{
	/* examine content is the only job which stops the viewer working */
	bool const active_job = _active_job && *_active_job != "examine_content";
	bool const c = ((_film && !_film->content().empty()) || !_spl.empty()) && !active_job;

	_slider->Enable (c);
	_rewind_button->Enable (c);
	_back_button->Enable (c);
	_forward_button->Enable (c);
#ifdef DCPOMATIC_VARIANT_SWAROOP
	_play_button->Enable (c && !_viewer->playing());
	_pause_button->Enable (c && (!_current_kind || _current_kind != dcp::ADVERTISEMENT) && _viewer->playing());
	_stop_button->Enable (c && (!_current_kind || _current_kind != dcp::ADVERTISEMENT));
	_slider->Enable (c && (!_current_kind || _current_kind != dcp::ADVERTISEMENT));
#else
	_play_button->Enable (c);
#endif
	if (_outline_content) {
		_outline_content->Enable (c);
	}
	_frame_number->Enable (c);
	_timecode->Enable (c);
	if (_jump_to_selected) {
		_jump_to_selected->Enable (c);
	}

	if (_eye) {
		_eye->Enable (c && _film->three_d ());
	}

	_add_button->Enable (static_cast<bool>(selected_cpl()));
}

optional<Controls::CPL>
Controls::selected_cpl () const
{
	long int s = _cpl->GetNextItem (-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (s == -1) {
		return optional<CPL>();
	}

	DCPOMATIC_ASSERT (s < int(_cpls.size()));
	return _cpls[s];
}

void
Controls::timecode_clicked ()
{
	PlayheadToTimecodeDialog* dialog = new PlayheadToTimecodeDialog (this, _film->video_frame_rate ());
	if (dialog->ShowModal() == wxID_OK) {
		_viewer->seek (dialog->get(), true);
	}
	dialog->Destroy ();
}

void
Controls::frame_number_clicked ()
{
	PlayheadToFrameDialog* dialog = new PlayheadToFrameDialog (this, _film->video_frame_rate ());
	if (dialog->ShowModal() == wxID_OK) {
		_viewer->seek (dialog->get(), true);
	}
	dialog->Destroy ();
}

void
Controls::jump_to_selected_clicked ()
{
	Config::instance()->set_jump_to_selected (_jump_to_selected->GetValue ());
}

void
Controls::film_changed ()
{
	shared_ptr<Film> film = _viewer->film ();

	if (_film == film) {
		return;
	}

	_film = film;

	setup_sensitivity ();

	update_position_slider ();
	update_position_label ();

	if (_film) {
		_film->Change.connect (boost::bind (&Controls::film_change, this, _1, _2));
	}
}

shared_ptr<Film>
Controls::film () const
{
	return _film;
}

void
Controls::show_extended_player_controls (bool s)
{
	_cpl->Show (s);
	if (s) {
		update_dcp_directory ();
	}
	_spl_view->Show (s);
	_log->Show (s);
	_add_button->Show (s);
	_v_sizer->Layout ();
}

void
Controls::add_cpl_to_list (shared_ptr<dcp::CPL> cpl, wxListCtrl* ctrl)
{
	list<shared_ptr<dcp::Reel> > reels = cpl->reels ();

	int const N = ctrl->GetItemCount();

	wxListItem it;
	if (!reels.empty() && reels.front()->main_picture()) {
		it.SetId(N);
		it.SetColumn(0);
		int seconds = rint(double(cpl->duration()) / reels.front()->main_picture()->frame_rate().as_float());
		int minutes = seconds / 60;
		seconds -= minutes * 60;
		int hours = minutes / 60;
		minutes -= hours * 60;
		it.SetText(wxString::Format("%02d:%02d:%02d", hours, minutes, seconds));
		ctrl->InsertItem(it);
	}

	it.SetId(N);
	it.SetColumn(1);
	it.SetText(std_to_wx(dcp::content_kind_to_string(cpl->content_kind())));
	ctrl->SetItem(it);

	it.SetId(N);
	it.SetColumn(2);
	it.SetText(std_to_wx(cpl->annotation_text()));
	ctrl->SetItem(it);
}

void
Controls::update_dcp_directory ()
{
	if (!_cpl->IsShown()) {
		return;
	}

	using namespace boost::filesystem;

	_cpl->DeleteAllItems ();
	_cpls.clear ();
	optional<path> dir = Config::instance()->player_dcp_directory();
	if (!dir) {
		return;
	}

	for (directory_iterator i = directory_iterator(*dir); i != directory_iterator(); ++i) {
		try {
			if (is_directory(*i) && (is_regular_file(*i / "ASSETMAP") || is_regular_file(*i / "ASSETMAP.xml"))) {
				string const x = i->path().string().substr(dir->string().length() + 1);
				dcp::DCP dcp (*i);
				dcp.read ();
				BOOST_FOREACH (shared_ptr<dcp::CPL> j, dcp.cpls()) {
					add_cpl_to_list (j, _cpl);
					_cpls.push_back (make_pair(j, *i));
				}
			}
		} catch (boost::filesystem::filesystem_error& e) {
			/* Never mind */
		}
	}
}

#ifdef DCPOMATIC_VARIANT_SWAROOP
void
Controls::pause_clicked ()
{
	_viewer->stop ();
}

void
Controls::stop_clicked ()
{
	_viewer->stop ();
	_viewer->seek (DCPTime(), true);
}
#endif

void
Controls::log (wxString s)
{
	struct timeval time;
	gettimeofday (&time, 0);
	char buffer[64];
	time_t const sec = time.tv_sec;
	struct tm* t = localtime (&sec);
	strftime (buffer, 64, "%c", t);
	wxString ts = std_to_wx(string(buffer)) + N_(": ");
	_log->SetValue(_log->GetValue() + ts + s + "\n");
}

void
Controls::image_changed (boost::weak_ptr<PlayerVideo> weak_pv)
{
#ifdef DCPOMATIC_VARIANT_SWAROOP
	shared_ptr<PlayerVideo> pv = weak_pv.lock ();
	if (!pv) {
		return;
	}

	shared_ptr<Content> c = pv->content().lock();
	if (!c) {
		return;
	}

	shared_ptr<DCPContent> dc = dynamic_pointer_cast<DCPContent> (c);
	if (!dc) {
		return;
	}

	if (!_current_kind || *_current_kind != dc->content_kind()) {
		_current_kind = dc->content_kind ();
		setup_sensitivity ();
	}
#endif
}