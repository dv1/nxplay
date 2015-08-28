/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_UTILITY_HPP
#define NXPLAY_UTILITY_HPP

#include <gst/gst.h>


/** nxplay */
namespace nxplay
{


/// Utility function to unref a GStreamer object only if the pointer isn't nullptr.
/**
 * This also sets the pointer to nullptr after unref'ing.
 */
template < typename T >
void checked_unref(T* &p_object)
{
	if (p_object != nullptr)
	{
		gst_object_unref(GST_OBJECT(p_object));
		p_object = nullptr;
	}
}


} // namespace nxplay end


#endif
