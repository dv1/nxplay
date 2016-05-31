/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include "pipeline.hpp"


namespace nxplay
{


std::string get_state_name(states const p_state)
{
	switch (p_state)
	{
		case state_idle: return "idle";
		case state_starting: return "starting";
		case state_stopping: return "stopping";
		case state_seeking: return "seeking";
		case state_buffering: return "buffering";
		case state_playing: return "playing";
		case state_paused: return "paused";
		default: return "<invalid>";
	}
}


playback_properties::playback_properties()
	: m_start_paused(false)
	, m_start_at_position(0)
	, m_start_at_position_unit(position_unit_nanoseconds)
	, m_buffer_estimation_duration(boost::none)
	, m_buffer_timeout(boost::none)
	, m_buffer_size(boost::none)
	, m_low_buffer_threshold(boost::none)
	, m_high_buffer_threshold(boost::none)
	, m_jitter_buffer_length(boost::none)
	, m_do_retransmissions(boost::none)
	, m_allowed_transports(boost::none)
{
}


playback_properties::playback_properties(
	bool const p_start_paused,
	gint64 const p_start_at_position,
	position_units const p_start_at_position_unit,
	boost::optional < guint64 > const &p_buffer_estimation_duration,
	boost::optional < guint64 > const &p_buffer_timeout,
	boost::optional < guint > const &p_buffer_size,
	boost::optional < guint > const &p_low_buffer_threshold,
	boost::optional < guint > const &p_high_buffer_threshold,
	boost::optional < guint64 > const &p_jitter_buffer_length,
	boost::optional < bool > const &p_do_retransmissions,
	boost::optional < guint32 > const p_allowed_transports
)
	: m_start_paused(p_start_paused)
	, m_start_at_position(p_start_at_position)
	, m_start_at_position_unit(p_start_at_position_unit)
	, m_buffer_estimation_duration(p_buffer_estimation_duration)
	, m_buffer_timeout(p_buffer_timeout)
	, m_buffer_size(p_buffer_size)
	, m_low_buffer_threshold(p_low_buffer_threshold)
	, m_high_buffer_threshold(p_high_buffer_threshold)
	, m_jitter_buffer_length(p_jitter_buffer_length)
	, m_do_retransmissions(p_do_retransmissions)
	, m_allowed_transports(p_allowed_transports)
{
}


pipeline::~pipeline()
{
}


bool pipeline::play_media(guint64 const p_token, media const &p_media, bool const p_play_now, playback_properties const &p_properties)
{
	media temp_media(p_media); // create temporary copy which will be moved by the derived class
	return play_media_impl(p_token, std::move(temp_media), p_play_now, p_properties);
}


bool pipeline::play_media(guint64 const p_token, media &&p_media, bool const p_play_now, playback_properties const &p_properties)
{
	return play_media_impl(p_token, std::move(p_media), p_play_now, p_properties);
}


} // namespace nxplay end
