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

/** @file  src/ffmpeg_decoder.cc
 *  @brief A decoder using FFmpeg to decode content.
 */

#include "filter.h"
#include "exceptions.h"
#include "image.h"
#include "util.h"
#include "log.h"
#include "ffmpeg_decoder.h"
#include "ffmpeg_audio_stream.h"
#include "ffmpeg_subtitle_stream.h"
#include "filter_graph.h"
#include "audio_buffers.h"
#include "ffmpeg_content.h"
#include "raw_image_proxy.h"
#include "film.h"
#include "compose.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <boost/foreach.hpp>
#include <vector>
#include <iomanip>
#include <iostream>
#include <stdint.h>

#include "i18n.h"

#define LOG_GENERAL(...) _video_content->film()->log()->log (String::compose (__VA_ARGS__), Log::TYPE_GENERAL);
#define LOG_ERROR(...) _video_content->film()->log()->log (String::compose (__VA_ARGS__), Log::TYPE_ERROR);
#define LOG_WARNING_NC(...) _video_content->film()->log()->log (__VA_ARGS__, Log::TYPE_WARNING);
#define LOG_WARNING(...) _video_content->film()->log()->log (String::compose (__VA_ARGS__), Log::TYPE_WARNING);

using std::cout;
using std::vector;
using std::list;
using std::min;
using std::pair;
using std::max;
using boost::shared_ptr;
using dcp::Size;

FFmpegDecoder::FFmpegDecoder (shared_ptr<const FFmpegContent> c, shared_ptr<Log> log)
	: VideoDecoder (c)
	, AudioDecoder (c)
	, SubtitleDecoder (c)
	, FFmpeg (c)
	, _log (log)
{
	/* Audio and video frame PTS values may not start with 0.  We want
	   to fiddle them so that:

	   1.  One of them starts at time 0.
	   2.  The first video PTS value ends up on a frame boundary.

	   Then we remove big initial gaps in PTS and we allow our
	   insertion of black frames to work.

	   We will do:
	     audio_pts_to_use = audio_pts_from_ffmpeg + pts_offset;
	     video_pts_to_use = video_pts_from_ffmpeg + pts_offset;
	*/

	/* First, make one of them start at 0 */

	vector<shared_ptr<FFmpegAudioStream> > streams = c->ffmpeg_audio_streams ();

	_pts_offset = ContentTime::min ();

	if (c->first_video ()) {
		_pts_offset = - c->first_video().get ();
	}

	BOOST_FOREACH (shared_ptr<FFmpegAudioStream> i, streams) {
		if (i->first_audio) {
			_pts_offset = max (_pts_offset, - i->first_audio.get ());
		}
	}

	/* If _pts_offset is positive we would be pushing things from a -ve PTS to be played.
	   I don't think we ever want to do that, as it seems things at -ve PTS are not meant
	   to be seen (use for alignment bars etc.); see mantis #418.
	*/
	if (_pts_offset > ContentTime ()) {
		_pts_offset = ContentTime ();
	}

	/* Now adjust so that the video pts starts on a frame */
	if (c->first_video ()) {
		ContentTime first_video = c->first_video().get() + _pts_offset;
		ContentTime const old_first_video = first_video;
		_pts_offset += first_video.round_up (c->video_frame_rate ()) - old_first_video;
	}
}

void
FFmpegDecoder::flush ()
{
	/* Get any remaining frames */

	_packet.data = 0;
	_packet.size = 0;

	/* XXX: should we reset _packet.data and size after each *_decode_* call? */

	while (decode_video_packet ()) {}

	decode_audio_packet ();
	AudioDecoder::flush ();
}

bool
FFmpegDecoder::pass ()
{
	int r = av_read_frame (_format_context, &_packet);

	/* AVERROR_INVALIDDATA can apparently be returned sometimes even when av_read_frame
	   has pretty-much succeeded (and hence generated data which should be processed).
	   Hence it makes sense to continue here in that case.
	*/
	if (r < 0 && r != AVERROR_INVALIDDATA) {
		if (r != AVERROR_EOF) {
			/* Maybe we should fail here, but for now we'll just finish off instead */
			char buf[256];
			av_strerror (r, buf, sizeof(buf));
			LOG_ERROR (N_("error on av_read_frame (%1) (%2)"), buf, r);
		}

		flush ();
		return true;
	}

	int const si = _packet.stream_index;
	shared_ptr<const FFmpegContent> fc = _ffmpeg_content;

	if (si == _video_stream && !_ignore_video) {
		decode_video_packet ();
	} else if (fc->subtitle_stream() && fc->subtitle_stream()->uses_index (_format_context, si)) {
		decode_subtitle_packet ();
	} else {
		decode_audio_packet ();
	}

	av_free_packet (&_packet);
	return false;
}

/** @param data pointer to array of pointers to buffers.
 *  Only the first buffer will be used for non-planar data, otherwise there will be one per channel.
 */
shared_ptr<AudioBuffers>
FFmpegDecoder::deinterleave_audio (shared_ptr<FFmpegAudioStream> stream, uint8_t** data, int size)
{
	DCPOMATIC_ASSERT (bytes_per_audio_sample (stream));

	/* Deinterleave and convert to float */

	/* total_samples and frames will be rounded down here, so if there are stray samples at the end
	   of the block that do not form a complete sample or frame they will be dropped.
	*/
	int const total_samples = size / bytes_per_audio_sample (stream);
	int const frames = total_samples / stream->channels();
	shared_ptr<AudioBuffers> audio (new AudioBuffers (stream->channels(), frames));

	switch (audio_sample_format (stream)) {
	case AV_SAMPLE_FMT_U8:
	{
		uint8_t* p = reinterpret_cast<uint8_t *> (data[0]);
		int sample = 0;
		int channel = 0;
		for (int i = 0; i < total_samples; ++i) {
			audio->data(channel)[sample] = float(*p++) / (1 << 23);

			++channel;
			if (channel == stream->channels()) {
				channel = 0;
				++sample;
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_S16:
	{
		int16_t* p = reinterpret_cast<int16_t *> (data[0]);
		int sample = 0;
		int channel = 0;
		for (int i = 0; i < total_samples; ++i) {
			audio->data(channel)[sample] = float(*p++) / (1 << 15);

			++channel;
			if (channel == stream->channels()) {
				channel = 0;
				++sample;
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_S16P:
	{
		int16_t** p = reinterpret_cast<int16_t **> (data);
		for (int i = 0; i < stream->channels(); ++i) {
			for (int j = 0; j < frames; ++j) {
				audio->data(i)[j] = static_cast<float>(p[i][j]) / (1 << 15);
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_S32:
	{
		int32_t* p = reinterpret_cast<int32_t *> (data[0]);
		int sample = 0;
		int channel = 0;
		for (int i = 0; i < total_samples; ++i) {
			audio->data(channel)[sample] = static_cast<float>(*p++) / (1 << 31);

			++channel;
			if (channel == stream->channels()) {
				channel = 0;
				++sample;
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_FLT:
	{
		float* p = reinterpret_cast<float*> (data[0]);
		int sample = 0;
		int channel = 0;
		for (int i = 0; i < total_samples; ++i) {
			audio->data(channel)[sample] = *p++;

			++channel;
			if (channel == stream->channels()) {
				channel = 0;
				++sample;
			}
		}
	}
	break;

	case AV_SAMPLE_FMT_FLTP:
	{
		float** p = reinterpret_cast<float**> (data);
		for (int i = 0; i < stream->channels(); ++i) {
			memcpy (audio->data(i), p[i], frames * sizeof(float));
		}
	}
	break;

	default:
		throw DecodeError (String::compose (_("Unrecognised audio sample format (%1)"), static_cast<int> (audio_sample_format (stream))));
	}

	return audio;
}

AVSampleFormat
FFmpegDecoder::audio_sample_format (shared_ptr<FFmpegAudioStream> stream) const
{
	return stream->stream (_format_context)->codec->sample_fmt;
}

int
FFmpegDecoder::bytes_per_audio_sample (shared_ptr<FFmpegAudioStream> stream) const
{
	return av_get_bytes_per_sample (audio_sample_format (stream));
}

void
FFmpegDecoder::seek (ContentTime time, bool accurate)
{
	VideoDecoder::seek (time, accurate);
	AudioDecoder::seek (time, accurate);
	SubtitleDecoder::seek (time, accurate);

	/* If we are doing an `accurate' seek, we need to use pre-roll, as
	   we don't really know what the seek will give us.
	*/

	ContentTime pre_roll = accurate ? ContentTime::from_seconds (2) : ContentTime (0);
	time -= pre_roll;

	/* XXX: it seems debatable whether PTS should be used here...
	   http://www.mjbshaw.com/2012/04/seeking-in-ffmpeg-know-your-timestamp.html
	*/

	ContentTime u = time - _pts_offset;
	if (u < ContentTime ()) {
		u = ContentTime ();
	}
	av_seek_frame (_format_context, _video_stream, u.seconds() / av_q2d (_format_context->streams[_video_stream]->time_base), AVSEEK_FLAG_BACKWARD);

	avcodec_flush_buffers (video_codec_context());

	/* XXX: should be flushing audio buffers? */

	if (subtitle_codec_context ()) {
		avcodec_flush_buffers (subtitle_codec_context ());
	}
}

void
FFmpegDecoder::decode_audio_packet ()
{
	/* Audio packets can contain multiple frames, so we may have to call avcodec_decode_audio4
	   several times.
	*/

	AVPacket copy_packet = _packet;

	/* XXX: inefficient */
	vector<shared_ptr<FFmpegAudioStream> > streams = ffmpeg_content()->ffmpeg_audio_streams ();
	vector<shared_ptr<FFmpegAudioStream> >::const_iterator stream = streams.begin ();
	while (stream != streams.end () && !(*stream)->uses_index (_format_context, copy_packet.stream_index)) {
		++stream;
	}

	if (stream == streams.end ()) {
		/* The packet's stream may not be an audio one; just ignore it in this method if so */
		return;
	}

	while (copy_packet.size > 0) {

		int frame_finished;
		int decode_result = avcodec_decode_audio4 ((*stream)->stream (_format_context)->codec, _frame, &frame_finished, &copy_packet);
		if (decode_result < 0) {
			/* avcodec_decode_audio4 can sometimes return an error even though it has decoded
			   some valid data; for example dca_subframe_footer can return AVERROR_INVALIDDATA
			   if it overreads the auxiliary data.  ffplay carries on if frame_finished is true,
			   even in the face of such an error, so I think we should too.

			   Returning from the method here caused mantis #352.
			*/
			LOG_WARNING ("avcodec_decode_audio4 failed (%1)", decode_result);

			/* Fudge decode_result so that we come out of the while loop when
			   we've processed this data.
			*/
			decode_result = copy_packet.size;
		}

		if (frame_finished) {
			ContentTime const ct = ContentTime::from_seconds (
				av_frame_get_best_effort_timestamp (_frame) *
				av_q2d ((*stream)->stream (_format_context)->time_base))
				+ _pts_offset;

			int const data_size = av_samples_get_buffer_size (
				0, (*stream)->stream(_format_context)->codec->channels, _frame->nb_samples, audio_sample_format (*stream), 1
				);

			audio (*stream, deinterleave_audio (*stream, _frame->data, data_size), ct);
		}

		copy_packet.data += decode_result;
		copy_packet.size -= decode_result;
	}
}

bool
FFmpegDecoder::decode_video_packet ()
{
	int frame_finished;
	if (avcodec_decode_video2 (video_codec_context(), _frame, &frame_finished, &_packet) < 0 || !frame_finished) {
		return false;
	}

	boost::mutex::scoped_lock lm (_filter_graphs_mutex);

	shared_ptr<FilterGraph> graph;

	list<shared_ptr<FilterGraph> >::iterator i = _filter_graphs.begin();
	while (i != _filter_graphs.end() && !(*i)->can_process (dcp::Size (_frame->width, _frame->height), (AVPixelFormat) _frame->format)) {
		++i;
	}

	if (i == _filter_graphs.end ()) {
		graph.reset (new FilterGraph (_ffmpeg_content, dcp::Size (_frame->width, _frame->height), (AVPixelFormat) _frame->format));
		_filter_graphs.push_back (graph);
		LOG_GENERAL (N_("New graph for %1x%2, pixel format %3"), _frame->width, _frame->height, _frame->format);
	} else {
		graph = *i;
	}

	list<pair<shared_ptr<Image>, int64_t> > images = graph->process (_frame);

	for (list<pair<shared_ptr<Image>, int64_t> >::iterator i = images.begin(); i != images.end(); ++i) {

		shared_ptr<Image> image = i->first;

		if (i->second != AV_NOPTS_VALUE) {
			double const pts = i->second * av_q2d (_format_context->streams[_video_stream]->time_base) + _pts_offset.seconds ();
			video (
				shared_ptr<ImageProxy> (new RawImageProxy (image)),
				llrint (pts * _ffmpeg_content->video_frame_rate ())
				);
		} else {
			LOG_WARNING_NC ("Dropping frame without PTS");
		}
	}

	return true;
}

void
FFmpegDecoder::decode_subtitle_packet ()
{
	int got_subtitle;
	AVSubtitle sub;
	if (avcodec_decode_subtitle2 (subtitle_codec_context(), &sub, &got_subtitle, &_packet) < 0 || !got_subtitle) {
		return;
	}

	if (sub.num_rects <= 0) {
		/* Sometimes we get an empty AVSubtitle, which is used by some codecs to
		   indicate that the previous subtitle should stop.  We can ignore it here.
		*/
		return;
	} else if (sub.num_rects > 1) {
		throw DecodeError (_("multi-part subtitles not yet supported"));
	}

	/* Subtitle PTS (within the source, not taking into account any of the
	   source that we may have chopped off for the DCP).
	*/
	FFmpegSubtitlePeriod sub_period = subtitle_period (sub);
	ContentTimePeriod period;
	period.from = sub_period.from + _pts_offset;
	if (sub_period.to) {
		/* We already know the subtitle period `to' time */
		period.to = sub_period.to.get() + _pts_offset;
	} else {
		/* We have to look up the `to' time in the stream's records */
		period.to = ffmpeg_content()->subtitle_stream()->find_subtitle_to (sub_period.from);
	}

	AVSubtitleRect const * rect = sub.rects[0];

	switch (rect->type) {
	case SUBTITLE_NONE:
		break;
	case SUBTITLE_BITMAP:
		decode_bitmap_subtitle (rect, period);
		break;
	case SUBTITLE_TEXT:
		cout << "XXX: SUBTITLE_TEXT " << rect->text << "\n";
		break;
	case SUBTITLE_ASS:
		cout << "XXX: SUBTITLE_ASS " << rect->ass << "\n";
		break;
	}

	avsubtitle_free (&sub);
}

list<ContentTimePeriod>
FFmpegDecoder::image_subtitles_during (ContentTimePeriod p, bool starting) const
{
	return _ffmpeg_content->subtitles_during (p, starting);
}

list<ContentTimePeriod>
FFmpegDecoder::text_subtitles_during (ContentTimePeriod, bool) const
{
	return list<ContentTimePeriod> ();
}

void
FFmpegDecoder::decode_bitmap_subtitle (AVSubtitleRect const * rect, ContentTimePeriod period)
{
	/* Note RGBA is expressed little-endian, so the first byte in the word is R, second
	   G, third B, fourth A.
	*/
	shared_ptr<Image> image (new Image (PIX_FMT_RGBA, dcp::Size (rect->w, rect->h), true));

	/* Start of the first line in the subtitle */
	uint8_t* sub_p = rect->pict.data[0];
	/* sub_p looks up into a BGRA palette which is here
	   (i.e. first byte B, second G, third R, fourth A)
	*/
	uint32_t const * palette = (uint32_t *) rect->pict.data[1];
	/* Start of the output data */
	uint32_t* out_p = (uint32_t *) image->data()[0];

	for (int y = 0; y < rect->h; ++y) {
		uint8_t* sub_line_p = sub_p;
		uint32_t* out_line_p = out_p;
		for (int x = 0; x < rect->w; ++x) {
			uint32_t const p = palette[*sub_line_p++];
			*out_line_p++ = ((p & 0xff) << 16) | (p & 0xff00) | ((p & 0xff0000) >> 16) | (p & 0xff000000);
		}
		sub_p += rect->pict.linesize[0];
		out_p += image->stride()[0] / sizeof (uint32_t);
	}

	dcp::Size const vs = _ffmpeg_content->video_size ();
	dcpomatic::Rect<double> const scaled_rect (
		static_cast<double> (rect->x) / vs.width,
		static_cast<double> (rect->y) / vs.height,
		static_cast<double> (rect->w) / vs.width,
		static_cast<double> (rect->h) / vs.height
		);

	image_subtitle (period, image, scaled_rect);
}
