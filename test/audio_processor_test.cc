/*
    Copyright (C) 2015 Carl Hetherington <cth@carlh.net>

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

#include "lib/audio_processor.h"
#include "lib/analyse_audio_job.h"
#include "lib/dcp_content_type.h"
#include "lib/job_manager.h"
#include "lib/sndfile_content.h"
#include "lib/film.h"
#include "test.h"
#include <boost/test/unit_test.hpp>

using boost::shared_ptr;

/** Test the mid-side decoder for analysis and DCP-making */
BOOST_AUTO_TEST_CASE (audio_processor_test)
{
	shared_ptr<Film> film = new_test_film ("audio_processor_test");
	film->set_name ("audio_processor_test");
	shared_ptr<SndfileContent> c (new SndfileContent (film, "test/data/white.wav"));
	film->examine_and_add_content (c);
	wait_for_jobs ();

	film->set_audio_channels (6);
	film->set_dcp_content_type (DCPContentType::from_pretty_name ("Test"));
	film->set_audio_processor (AudioProcessor::from_id ("mid-side-decoder"));

	/* Analyse the audio and check it doesn't crash */
	shared_ptr<AnalyseAudioJob> job (new AnalyseAudioJob (film, film->playlist ()));
	JobManager::instance()->add (job);
	wait_for_jobs ();

	/* Make a DCP and check it */
	film->make_dcp ();
	wait_for_jobs ();
	check_dcp ("test/data/audio_processor_test", film->dir (film->dcp_name ()));
}
