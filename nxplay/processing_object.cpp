/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include "processing_object.hpp"


namespace nxplay
{


processing_object::~processing_object()
{
}


bool processing_object::setup()
{
	return true;
}


void processing_object::teardown()
{
}


} // namespace nxplay end
