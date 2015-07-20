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


pipeline::~pipeline()
{
}


bool pipeline::play_media(guint64 const p_token, media const &p_media, bool const p_play_now)
{
	media temp_media(p_media); // create temporary copy which will be moved by the derived class
	return play_media_impl(p_token, std::move(temp_media), p_play_now);
}


bool pipeline::play_media(guint64 const p_token, media &&p_media, bool const p_play_now)
{
	return play_media_impl(p_token, std::move(p_media), p_play_now);
}


} // namespace nxplay end
