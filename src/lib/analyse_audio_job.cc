/*
    Copyright (C) 2012-2015 Carl Hetherington <cth@carlh.net>

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

#include "audio_analysis.h"
#include "audio_buffers.h"
#include "analyse_audio_job.h"
#include "audio_content.h"
#include "compose.hpp"
#include "film.h"
#include "player.h"
#include "playlist.h"
#include <boost/foreach.hpp>
#include <iostream>

#include "i18n.h"

using std::string;
using std::max;
using std::min;
using std::cout;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;

int const AnalyseAudioJob::_num_points = 1024;

AnalyseAudioJob::AnalyseAudioJob (shared_ptr<const Film> film, shared_ptr<const Playlist> playlist)
	: Job (film)
	, _playlist (playlist)
	, _done (0)
	, _samples_per_point (1)
	, _current (0)
	, _overall_peak (0)
	, _overall_peak_frame (0)
{

}

AnalyseAudioJob::~AnalyseAudioJob ()
{
	delete[] _current;
}

string
AnalyseAudioJob::name () const
{
	return _("Analyse audio");
}

string
AnalyseAudioJob::json_name () const
{
	return N_("analyse_audio");
}

void
AnalyseAudioJob::run ()
{
	shared_ptr<Player> player (new Player (_film, _playlist));
	player->set_ignore_video ();
	player->set_fast ();
	player->set_play_referenced ();

	DCPTime const start = _playlist->start().get_value_or (DCPTime ());
	DCPTime const length = _playlist->length ();

	Frame const len = DCPTime (length - start).frames_round (_film->audio_frame_rate());
	_samples_per_point = max (int64_t (1), len / _num_points);

	delete[] _current;
	_current = new AudioPoint[_film->audio_channels ()];
	_analysis.reset (new AudioAnalysis (_film->audio_channels ()));

	bool has_any_audio = false;
	BOOST_FOREACH (shared_ptr<Content> c, _playlist->content ()) {
		if (dynamic_pointer_cast<AudioContent> (c)) {
			has_any_audio = true;
		}
	}

	if (has_any_audio) {
		_done = 0;
		DCPTime const block = DCPTime::from_seconds (1.0 / 8);
		for (DCPTime t = start; t < length; t += block) {
			analyse (player->get_audio (t, block, false));
			set_progress ((t.seconds() - start.seconds()) / (length.seconds() - start.seconds()));
		}
	}

	_analysis->set_peak (_overall_peak, DCPTime::from_frames (_overall_peak_frame, _film->audio_frame_rate ()));

	if (_playlist->content().size() == 1) {
		/* If there was only one piece of content in this analysis we may later need to know what its
		   gain was when we analysed it.
		*/
		shared_ptr<const AudioContent> ac = dynamic_pointer_cast<const AudioContent> (_playlist->content().front ());
		DCPOMATIC_ASSERT (ac);
		_analysis->set_analysis_gain (ac->audio_gain ());
	}

	_analysis->write (_film->audio_analysis_path (_playlist));

	set_progress (1);
	set_state (FINISHED_OK);
}

void
AnalyseAudioJob::analyse (shared_ptr<const AudioBuffers> b)
{
	int const frames = b->frames ();
	int const channels = b->channels ();

	for (int j = 0; j < channels; ++j) {
		float* data = b->data(j);
		for (int i = 0; i < frames; ++i) {
			float s = data[i];
			float as = fabsf (s);
			if (as < 10e-7) {
				/* SafeStringStream can't serialise and recover inf or -inf, so prevent such
				   values by replacing with this (140dB down) */
				s = as = 10e-7;
			}
			_current[j][AudioPoint::RMS] += pow (s, 2);
			_current[j][AudioPoint::PEAK] = max (_current[j][AudioPoint::PEAK], as);

			if (as > _overall_peak) {
				_overall_peak = as;
				_overall_peak_frame = _done + i;
			}

			if (((_done + i) % _samples_per_point) == 0) {
				_current[j][AudioPoint::RMS] = sqrt (_current[j][AudioPoint::RMS] / _samples_per_point);
				_analysis->add_point (j, _current[j]);
				_current[j] = AudioPoint ();
			}
		}
	}

	_done += frames;
}
