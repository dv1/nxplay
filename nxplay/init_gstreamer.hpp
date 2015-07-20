/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_INIT_GSTREAMER_HPP
#define NXPLAY_INIT_GSTREAMER_HPP


/** nxplay */
namespace nxplay
{


/// Convenience wrapper for gst_init_check() with reference counting.
/*
 * This can be called several times, and only the first time it actually
 * initializes GStreamer.
 *
 * NOTE: This function is not thread-safe.
 *
 * @param argc argc parameter from the main() function
 * @param argv argv parameter from the main() function
 * @return true if initialization succeeded, false otherwise
 */
bool init_gstreamer(int *argc, char ***argv);
/// Convenience wrapper for gst_deinit() with reference counting.
/*
 * Only after this gets called as many times as init_gstreamer() was
 * called, this will actually call gst_deinit() internally.
 *
 * NOTE: This function is not thread-safe.
 */
void deinit_gstreamer();


} // namespace nxplay end


#endif
