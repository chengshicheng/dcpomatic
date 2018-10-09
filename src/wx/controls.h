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

#include "lib/dcpomatic_time.h"
#include "lib/types.h"
#include "lib/film.h"
#include "lib/spl_entry.h"
#include <wx/wx.h>
#include <boost/shared_ptr.hpp>
#include <boost/signals2.hpp>

class FilmViewer;
class Film;
class ClosedCaptionsDialog;
class Content;
class PlayerVideo;
class wxToggleButton;
class wxListCtrl;

namespace dcp {
	class CPL;
}

class Controls : public wxPanel
{
public:
	Controls (
		wxWindow* parent,
		boost::shared_ptr<FilmViewer>,
		bool editor_controls = true
		);

	boost::shared_ptr<Film> film () const;
	void back_frame ();
	void forward_frame ();

	void show_extended_player_controls (bool s);
	void log (wxString s);

	boost::signals2::signal<void (std::list<SPLEntry>)> SPLChanged;

private:
	void update_position_label ();
	void update_position_slider ();
	void rewind_clicked (wxMouseEvent &);
	void back_clicked (wxKeyboardState& s);
	void forward_clicked (wxKeyboardState &);
	void slider_moved (bool page);
	void slider_released ();
	void play_clicked ();
	void frame_number_clicked ();
	void jump_to_selected_clicked ();
	void setup_sensitivity ();
	void timecode_clicked ();
#ifndef DCPOMATIC_PLAYER_SWAROOP
	void check_play_state ();
#endif
	void active_jobs_changed (boost::optional<std::string>);
	DCPTime nudge_amount (wxKeyboardState& ev);
	void image_changed (boost::weak_ptr<PlayerVideo>);
	void film_change (ChangeType type, Film::Property p);
	void outline_content_changed ();
	void eye_changed ();
	void position_changed ();
	void started ();
	void stopped ();
	void film_changed ();
	void update_dcp_directory ();
	void dcp_directory_changed ();
	void config_changed (int property);

	typedef std::pair<boost::shared_ptr<dcp::CPL>, boost::filesystem::path> CPL;

	boost::optional<CPL> selected_cpl () const;
#ifdef DCPOMATIC_VARIANT_SWAROOP
	void pause_clicked ();
	void stop_clicked ();
#endif
	void add_clicked ();
	void add_cpl_to_list (boost::shared_ptr<dcp::CPL> cpl, wxListCtrl* list);

	boost::shared_ptr<Film> _film;
	boost::shared_ptr<FilmViewer> _viewer;

	wxSizer* _v_sizer;
	bool _slider_being_moved;
	bool _was_running_before_slider;

	wxCheckBox* _outline_content;
	wxChoice* _eye;
	wxCheckBox* _jump_to_selected;
	wxListCtrl* _cpl;
	wxListCtrl* _spl_view;
	wxTextCtrl* _log;
	wxButton* _add_button;
	std::vector<CPL> _cpls;
	wxSlider* _slider;
	wxButton* _rewind_button;
	wxButton* _back_button;
	wxButton* _forward_button;
	wxStaticText* _frame_number;
	wxStaticText* _timecode;
#ifdef DCPOMATIC_VARIANT_SWAROOP
	wxButton* _play_button;
	wxButton* _pause_button;
	wxButton* _stop_button;
#else
	wxToggleButton* _play_button;
#endif
	boost::optional<std::string> _active_job;
	std::list<SPLEntry> _spl;

	ClosedCaptionsDialog* _closed_captions_dialog;

#ifdef DCPOMATIC_VARIANT_SWAROOP
	boost::optional<dcp::ContentKind> _current_kind;
#endif

	boost::signals2::scoped_connection _config_changed_connection;
};