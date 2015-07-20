/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include <gst/gst.h>
#include "log.hpp"
#include "init_gstreamer.hpp"


namespace nxplay
{


namespace
{


long refcount = 0;


}


bool init_gstreamer(int *argc, char ***argv)
{
	if (refcount == 0)
	{
		GError *error = nullptr;
		if (!gst_init_check(argc, argv, &error))
		{
			NXPLAY_LOG_MSG(error, "initializing GStreamer failed: " << error->message);
			return false;
		}
	}

	++refcount;

	return true;
}


void deinit_gstreamer()
{
	if (refcount > 0)
	{
		--refcount;

		if (refcount == 0)
		{
			gst_deinit();
			NXPLAY_LOG_MSG(debug, "GStreamer deinitialized");
		}
	}
}


} // namespace nxplay end
