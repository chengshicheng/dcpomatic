/*
    Copyright (C) 2013-2016 Carl Hetherington <cth@carlh.net>

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

#include "ffmpeg_content.h"
#include "video_content.h"
#include "audio_content.h"
#include "ffmpeg_examiner.h"
#include "ffmpeg_subtitle_stream.h"
#include "ffmpeg_audio_stream.h"
#include "compose.hpp"
#include "job.h"
#include "util.h"
#include "filter.h"
#include "film.h"
#include "log.h"
#include "exceptions.h"
#include "frame_rate_change.h"
#include "text_content.h"
#include <dcp/raw_convert.h>
#include <libcxml/cxml.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}
#include <libxml++/libxml++.h>
#include <boost/foreach.hpp>
#include <iostream>

#include "i18n.h"

using std::string;
using std::vector;
using std::list;
using std::cout;
using std::pair;
using std::make_pair;
using std::max;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using boost::optional;
using dcp::raw_convert;

int const FFmpegContentProperty::SUBTITLE_STREAMS = 100;
int const FFmpegContentProperty::SUBTITLE_STREAM = 101;
int const FFmpegContentProperty::FILTERS = 102;

FFmpegContent::FFmpegContent (boost::filesystem::path p)
	: Content (p)
	, _encrypted (false)
{

}

template <class T>
optional<T>
get_optional_enum (cxml::ConstNodePtr node, string name)
{
	optional<int> const v = node->optional_number_child<int>(name);
	if (!v) {
		return optional<T>();
	}
	return static_cast<T>(*v);
}

FFmpegContent::FFmpegContent (cxml::ConstNodePtr node, int version, list<string>& notes)
	: Content (node)
{
	video = VideoContent::from_xml (this, node, version);
	audio = AudioContent::from_xml (this, node, version);
	text = TextContent::from_xml (this, node, version);

	list<cxml::NodePtr> c = node->node_children ("SubtitleStream");
	for (list<cxml::NodePtr>::const_iterator i = c.begin(); i != c.end(); ++i) {
		_subtitle_streams.push_back (shared_ptr<FFmpegSubtitleStream> (new FFmpegSubtitleStream (*i, version)));
		if ((*i)->optional_number_child<int> ("Selected")) {
			_subtitle_stream = _subtitle_streams.back ();
		}
	}

	c = node->node_children ("AudioStream");
	for (list<cxml::NodePtr>::const_iterator i = c.begin(); i != c.end(); ++i) {
		shared_ptr<FFmpegAudioStream> as (new FFmpegAudioStream (*i, version));
		audio->add_stream (as);
		if (version < 11 && !(*i)->optional_node_child ("Selected")) {
			/* This is an old file and this stream is not selected, so un-map it */
			as->set_mapping (AudioMapping (as->channels (), MAX_DCP_AUDIO_CHANNELS));
		}
	}

	c = node->node_children ("Filter");
	for (list<cxml::NodePtr>::iterator i = c.begin(); i != c.end(); ++i) {
		Filter const * f = Filter::from_id ((*i)->content ());
		if (f) {
			_filters.push_back (f);
		} else {
			notes.push_back (String::compose (_("DCP-o-matic no longer supports the `%1' filter, so it has been turned off."), (*i)->content()));
		}
	}

	optional<ContentTime::Type> const f = node->optional_number_child<ContentTime::Type> ("FirstVideo");
	if (f) {
		_first_video = ContentTime (f.get ());
	}

	_color_range = get_optional_enum<AVColorRange>(node, "ColorRange");
	_color_primaries = get_optional_enum<AVColorPrimaries>(node, "ColorPrimaries");
	_color_trc = get_optional_enum<AVColorTransferCharacteristic>(node, "ColorTransferCharacteristic");
	_colorspace = get_optional_enum<AVColorSpace>(node, "Colorspace");
	_bits_per_pixel = node->optional_number_child<int> ("BitsPerPixel");
	_decryption_key = node->optional_string_child ("DecryptionKey");
	_encrypted = node->optional_bool_child("Encrypted").get_value_or(false);
}

FFmpegContent::FFmpegContent (vector<shared_ptr<Content> > c)
	: Content (c)
{
	vector<shared_ptr<Content> >::const_iterator i = c.begin ();

	bool need_video = false;
	bool need_audio = false;
	bool need_text = false;

	if (i != c.end ()) {
		need_video = static_cast<bool> ((*i)->video);
		need_audio = static_cast<bool> ((*i)->audio);
		need_text = !(*i)->text.empty();
	}

	while (i != c.end ()) {
		if (need_video != static_cast<bool> ((*i)->video)) {
			throw JoinError (_("Content to be joined must all have or not have video"));
		}
		if (need_audio != static_cast<bool> ((*i)->audio)) {
			throw JoinError (_("Content to be joined must all have or not have audio"));
		}
		if (need_text != !(*i)->text.empty()) {
			throw JoinError (_("Content to be joined must all have or not have subtitles or captions"));
		}
		++i;
	}

	if (need_video) {
		video.reset (new VideoContent (this, c));
	}
	if (need_audio) {
		audio.reset (new AudioContent (this, c));
	}
	if (need_text) {
		text.push_back (shared_ptr<TextContent> (new TextContent (this, c)));
	}

	shared_ptr<FFmpegContent> ref = dynamic_pointer_cast<FFmpegContent> (c[0]);
	DCPOMATIC_ASSERT (ref);

	for (size_t i = 0; i < c.size(); ++i) {
		shared_ptr<FFmpegContent> fc = dynamic_pointer_cast<FFmpegContent> (c[i]);
		if (fc->only_text() && fc->only_text()->use() && *(fc->_subtitle_stream.get()) != *(ref->_subtitle_stream.get())) {
			throw JoinError (_("Content to be joined must use the same subtitle stream."));
		}
	}

	/* XXX: should probably check that more of the stuff below is the same in *this and ref */

	_subtitle_streams = ref->subtitle_streams ();
	_subtitle_stream = ref->subtitle_stream ();
	_first_video = ref->_first_video;
	_filters = ref->_filters;
	_color_range = ref->_color_range;
	_color_primaries = ref->_color_primaries;
	_color_trc = ref->_color_trc;
	_colorspace = ref->_colorspace;
	_bits_per_pixel = ref->_bits_per_pixel;
	_encrypted = ref->_encrypted;
}

void
FFmpegContent::as_xml (xmlpp::Node* node, bool with_paths) const
{
	node->add_child("Type")->add_child_text ("FFmpeg");
	Content::as_xml (node, with_paths);

	if (video) {
		video->as_xml (node);
	}

	if (audio) {
		audio->as_xml (node);

		BOOST_FOREACH (AudioStreamPtr i, audio->streams ()) {
			shared_ptr<FFmpegAudioStream> f = dynamic_pointer_cast<FFmpegAudioStream> (i);
			DCPOMATIC_ASSERT (f);
			f->as_xml (node->add_child("AudioStream"));
		}
	}

	if (only_text()) {
		only_text()->as_xml (node);
	}

	boost::mutex::scoped_lock lm (_mutex);

	for (vector<shared_ptr<FFmpegSubtitleStream> >::const_iterator i = _subtitle_streams.begin(); i != _subtitle_streams.end(); ++i) {
		xmlpp::Node* t = node->add_child("SubtitleStream");
		if (_subtitle_stream && *i == _subtitle_stream) {
			t->add_child("Selected")->add_child_text("1");
		}
		(*i)->as_xml (t);
	}

	for (vector<Filter const *>::const_iterator i = _filters.begin(); i != _filters.end(); ++i) {
		node->add_child("Filter")->add_child_text ((*i)->id ());
	}

	if (_first_video) {
		node->add_child("FirstVideo")->add_child_text (raw_convert<string> (_first_video.get().get()));
	}

	if (_color_range) {
		node->add_child("ColorRange")->add_child_text (raw_convert<string> (static_cast<int> (*_color_range)));
	}
	if (_color_primaries) {
		node->add_child("ColorPrimaries")->add_child_text (raw_convert<string> (static_cast<int> (*_color_primaries)));
	}
	if (_color_trc) {
		node->add_child("ColorTransferCharacteristic")->add_child_text (raw_convert<string> (static_cast<int> (*_color_trc)));
	}
	if (_colorspace) {
		node->add_child("Colorspace")->add_child_text (raw_convert<string> (static_cast<int> (*_colorspace)));
	}
	if (_bits_per_pixel) {
		node->add_child("BitsPerPixel")->add_child_text (raw_convert<string> (*_bits_per_pixel));
	}
	if (_decryption_key) {
		node->add_child("DecryptionKey")->add_child_text (_decryption_key.get());
	}
	if (_encrypted) {
		node->add_child("Encypted")->add_child_text ("1");
	}
}

void
FFmpegContent::examine (shared_ptr<const Film> film, shared_ptr<Job> job)
{
	ChangeSignaller<Content> cc1 (this, FFmpegContentProperty::SUBTITLE_STREAMS);
	ChangeSignaller<Content> cc2 (this, FFmpegContentProperty::SUBTITLE_STREAM);

	job->set_progress_unknown ();

	Content::examine (film, job);

	shared_ptr<FFmpegExaminer> examiner (new FFmpegExaminer (shared_from_this (), job));

	if (examiner->has_video ()) {
		video.reset (new VideoContent (this));
		video->take_from_examiner (examiner);
	}

	boost::filesystem::path first_path = path (0);

	{
		boost::mutex::scoped_lock lm (_mutex);

		if (examiner->has_video ()) {
			_first_video = examiner->first_video ();
			_color_range = examiner->color_range ();
			_color_primaries = examiner->color_primaries ();
			_color_trc = examiner->color_trc ();
			_colorspace = examiner->colorspace ();
			_bits_per_pixel = examiner->bits_per_pixel ();

			if (examiner->rotation()) {
				double rot = *examiner->rotation ();
				if (fabs (rot - 180) < 1.0) {
					_filters.push_back (Filter::from_id ("vflip"));
					_filters.push_back (Filter::from_id ("hflip"));
				} else if (fabs (rot - 90) < 1.0) {
					_filters.push_back (Filter::from_id ("90clock"));
				} else if (fabs (rot - 270) < 1.0) {
					_filters.push_back (Filter::from_id ("90anticlock"));
				}
			}
		}

		if (!examiner->audio_streams().empty ()) {
			audio.reset (new AudioContent (this));

			BOOST_FOREACH (shared_ptr<FFmpegAudioStream> i, examiner->audio_streams ()) {
				audio->add_stream (i);
			}

			AudioStreamPtr as = audio->streams().front();
			AudioMapping m = as->mapping ();
			m.make_default (film ? film->audio_processor() : 0, first_path);
			as->set_mapping (m);
		}

		_subtitle_streams = examiner->subtitle_streams ();
		if (!_subtitle_streams.empty ()) {
			text.clear ();
			text.push_back (shared_ptr<TextContent> (new TextContent (this, TEXT_OPEN_SUBTITLE, TEXT_UNKNOWN)));
			_subtitle_stream = _subtitle_streams.front ();
		}

		_encrypted = first_path.extension() == ".ecinema";
	}

	if (examiner->has_video ()) {
		set_default_colour_conversion ();
	}
}

string
FFmpegContent::summary () const
{
	if (video && audio) {
		return String::compose (_("%1 [movie]"), path_summary ());
	} else if (video) {
		return String::compose (_("%1 [video]"), path_summary ());
	} else if (audio) {
		return String::compose (_("%1 [audio]"), path_summary ());
	}

	return path_summary ();
}

string
FFmpegContent::technical_summary () const
{
	string as = "";
	BOOST_FOREACH (shared_ptr<FFmpegAudioStream> i, ffmpeg_audio_streams ()) {
		as += i->technical_summary () + " " ;
	}

	if (as.empty ()) {
		as = "none";
	}

	string ss = "none";
	if (_subtitle_stream) {
		ss = _subtitle_stream->technical_summary ();
	}

	string filt = Filter::ffmpeg_string (_filters);

	string s = Content::technical_summary ();

	if (video) {
		s += " - " + video->technical_summary ();
	}

	if (audio) {
		s += " - " + audio->technical_summary ();
	}

	return s + String::compose (
		"ffmpeg: audio %1 subtitle %2 filters %3", as, ss, filt
		);
}

void
FFmpegContent::set_subtitle_stream (shared_ptr<FFmpegSubtitleStream> s)
{
	ChangeSignaller<Content> cc (this, FFmpegContentProperty::SUBTITLE_STREAM);

	{
		boost::mutex::scoped_lock lm (_mutex);
		_subtitle_stream = s;
	}
}

bool
operator== (FFmpegStream const & a, FFmpegStream const & b)
{
	return a._id == b._id;
}

bool
operator!= (FFmpegStream const & a, FFmpegStream const & b)
{
	return a._id != b._id;
}

DCPTime
FFmpegContent::full_length (shared_ptr<const Film> film) const
{
	FrameRateChange const frc (film, shared_from_this());
	if (video) {
		return DCPTime::from_frames (llrint (video->length_after_3d_combine() * frc.factor()), film->video_frame_rate());
	}

	if (audio) {
		DCPTime longest;
		BOOST_FOREACH (AudioStreamPtr i, audio->streams()) {
			longest = max (longest, DCPTime::from_frames(llrint(i->length() / frc.speed_up), i->frame_rate()));
		}
		return longest;
	}

	/* XXX: subtitle content? */

	return DCPTime();
}

DCPTime
FFmpegContent::approximate_length () const
{
	if (video) {
		return DCPTime::from_frames (video->length_after_3d_combine(), 24);
	}

	DCPOMATIC_ASSERT (audio);

	Frame longest = 0;
	BOOST_FOREACH (AudioStreamPtr i, audio->streams ()) {
		longest = max (longest, Frame(llrint(i->length())));
	}

	return DCPTime::from_frames (longest, 24);
}

void
FFmpegContent::set_filters (vector<Filter const *> const & filters)
{
	ChangeSignaller<Content> cc (this, FFmpegContentProperty::FILTERS);

	{
		boost::mutex::scoped_lock lm (_mutex);
		_filters = filters;
	}
}

string
FFmpegContent::identifier () const
{
	string s = Content::identifier();

	if (video) {
		s += "_" + video->identifier();
	}

	if (only_text() && only_text()->use() && only_text()->burn()) {
		s += "_" + only_text()->identifier();
	}

	boost::mutex::scoped_lock lm (_mutex);

	if (_subtitle_stream) {
		s += "_" + _subtitle_stream->identifier ();
	}

	for (vector<Filter const *>::const_iterator i = _filters.begin(); i != _filters.end(); ++i) {
		s += "_" + (*i)->id ();
	}

	return s;
}

void
FFmpegContent::set_default_colour_conversion ()
{
	DCPOMATIC_ASSERT (video);

	dcp::Size const s = video->size ();

	boost::mutex::scoped_lock lm (_mutex);

	switch (_colorspace.get_value_or(AVCOL_SPC_UNSPECIFIED)) {
	case AVCOL_SPC_RGB:
		video->set_colour_conversion (PresetColourConversion::from_id ("srgb").conversion);
		break;
	case AVCOL_SPC_BT709:
		video->set_colour_conversion (PresetColourConversion::from_id ("rec709").conversion);
		break;
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M:
		video->set_colour_conversion (PresetColourConversion::from_id ("rec601").conversion);
		break;
	case AVCOL_SPC_BT2020_CL:
	case AVCOL_SPC_BT2020_NCL:
		video->set_colour_conversion (PresetColourConversion::from_id ("rec2020").conversion);
		break;
	default:
		if (s.width < 1080) {
			video->set_colour_conversion (PresetColourConversion::from_id ("rec601").conversion);
		} else {
			video->set_colour_conversion (PresetColourConversion::from_id ("rec709").conversion);
		}
		break;
	}
}

void
FFmpegContent::add_properties (shared_ptr<const Film> film, list<UserProperty>& p) const
{
	Content::add_properties (film, p);

	if (video) {
		video->add_properties (p);

		if (_bits_per_pixel) {
			int const sub = 219 * pow (2, _bits_per_pixel.get() - 8);
			int const total = pow (2, _bits_per_pixel.get());

			switch (_color_range.get_value_or(AVCOL_RANGE_UNSPECIFIED)) {
			case AVCOL_RANGE_UNSPECIFIED:
				/// TRANSLATORS: this means that the range of pixel values used in this
				/// file is unknown (not specified in the file).
				p.push_back (UserProperty (UserProperty::VIDEO, _("Colour range"), _("Unspecified")));
				break;
			case AVCOL_RANGE_MPEG:
				/// TRANSLATORS: this means that the range of pixel values used in this
				/// file is limited, so that not all possible values are valid.
				p.push_back (
					UserProperty (
						UserProperty::VIDEO, _("Colour range"), String::compose (_("Limited (%1-%2)"), (total - sub) / 2, (total + sub) / 2)
						)
					);
				break;
			case AVCOL_RANGE_JPEG:
				/// TRANSLATORS: this means that the range of pixel values used in this
				/// file is full, so that all possible pixel values are valid.
				p.push_back (UserProperty (UserProperty::VIDEO, _("Colour range"), String::compose (_("Full (0-%1)"), total)));
				break;
			default:
				DCPOMATIC_ASSERT (false);
			}
		} else {
			switch (_color_range.get_value_or(AVCOL_RANGE_UNSPECIFIED)) {
			case AVCOL_RANGE_UNSPECIFIED:
				/// TRANSLATORS: this means that the range of pixel values used in this
				/// file is unknown (not specified in the file).
				p.push_back (UserProperty (UserProperty::VIDEO, _("Colour range"), _("Unspecified")));
				break;
			case AVCOL_RANGE_MPEG:
				/// TRANSLATORS: this means that the range of pixel values used in this
				/// file is limited, so that not all possible values are valid.
				p.push_back (UserProperty (UserProperty::VIDEO, _("Colour range"), _("Limited")));
				break;
			case AVCOL_RANGE_JPEG:
				/// TRANSLATORS: this means that the range of pixel values used in this
				/// file is full, so that all possible pixel values are valid.
				p.push_back (UserProperty (UserProperty::VIDEO, _("Colour range"), _("Full")));
				break;
			default:
				DCPOMATIC_ASSERT (false);
			}
		}

		char const * primaries[] = {
			_("Unspecified"),
			_("BT709"),
			_("Unspecified"),
			_("Unspecified"),
			_("BT470M"),
			_("BT470BG"),
			_("SMPTE 170M (BT601)"),
			_("SMPTE 240M"),
			_("Film"),
			_("BT2020"),
			_("SMPTE ST 428-1 (CIE 1931 XYZ)"),
			_("SMPTE ST 431-2 (2011)"),
			_("SMPTE ST 432-1 D65 (2010)"), // 12
			"", // 13
			"", // 14
			"", // 15
			"", // 16
			"", // 17
			"", // 18
			"", // 19
			"", // 20
			"", // 21
			_("JEDEC P22")
		};

		DCPOMATIC_ASSERT (AVCOL_PRI_NB <= 23);
		p.push_back (UserProperty (UserProperty::VIDEO, _("Colour primaries"), primaries[_color_primaries.get_value_or(AVCOL_PRI_UNSPECIFIED)]));

		char const * transfers[] = {
			_("Unspecified"),
			_("BT709"),
			_("Unspecified"),
			_("Unspecified"),
			_("Gamma 22 (BT470M)"),
			_("Gamma 28 (BT470BG)"),
			_("SMPTE 170M (BT601)"),
			_("SMPTE 240M"),
			_("Linear"),
			_("Logarithmic (100:1 range)"),
			_("Logarithmic (316:1 range)"),
			_("IEC61966-2-4"),
			_("BT1361 extended colour gamut"),
			_("IEC61966-2-1 (sRGB or sYCC)"),
			_("BT2020 for a 10-bit system"),
			_("BT2020 for a 12-bit system"),
			_("SMPTE ST 2084 for 10, 12, 14 and 16 bit systems"),
			_("SMPTE ST 428-1"),
			_("ARIB STD-B67 ('Hybrid log-gamma')")
		};

		DCPOMATIC_ASSERT (AVCOL_TRC_NB <= 19);
		p.push_back (UserProperty (UserProperty::VIDEO, _("Colour transfer characteristic"), transfers[_color_trc.get_value_or(AVCOL_TRC_UNSPECIFIED)]));

		char const * spaces[] = {
			_("RGB / sRGB (IEC61966-2-1)"),
			_("BT709"),
			_("Unspecified"),
			_("Unspecified"),
			_("FCC"),
			_("BT470BG (BT601-6)"),
			_("SMPTE 170M (BT601-6)"),
			_("SMPTE 240M"),
			_("YCOCG"),
			_("BT2020 non-constant luminance"),
			_("BT2020 constant luminance"),
			_("SMPTE 2085, Y'D'zD'x"),
			_("Chroma-derived non-constant luminance"),
			_("Chroma-derived constant luminance"),
			_("BT2100")
		};

		DCPOMATIC_ASSERT (AVCOL_SPC_NB == 15);
		p.push_back (UserProperty (UserProperty::VIDEO, _("Colourspace"), spaces[_colorspace.get_value_or(AVCOL_SPC_UNSPECIFIED)]));

		if (_bits_per_pixel) {
			p.push_back (UserProperty (UserProperty::VIDEO, _("Bits per pixel"), *_bits_per_pixel));
		}
	}

	if (audio) {
		audio->add_properties (film, p);
	}
}

/** Our subtitle streams have colour maps, which can be changed, but
 *  they have no way of signalling that change.  As a hack, we have this
 *  method which callers can use when they've modified one of our subtitle
 *  streams.
 */
void
FFmpegContent::signal_subtitle_stream_changed ()
{
	/* XXX: this is too late; really it should be before the change */
	ChangeSignaller<Content> cc (this, FFmpegContentProperty::SUBTITLE_STREAM);
}

vector<shared_ptr<FFmpegAudioStream> >
FFmpegContent::ffmpeg_audio_streams () const
{
	vector<shared_ptr<FFmpegAudioStream> > fa;

	if (audio) {
		BOOST_FOREACH (AudioStreamPtr i, audio->streams()) {
			fa.push_back (dynamic_pointer_cast<FFmpegAudioStream> (i));
		}
	}

	return fa;
}

void
FFmpegContent::take_settings_from (shared_ptr<const Content> c)
{
	shared_ptr<const FFmpegContent> fc = dynamic_pointer_cast<const FFmpegContent> (c);
	if (!fc) {
		return;
		}

	Content::take_settings_from (c);
	_filters = fc->_filters;
}
