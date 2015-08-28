/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include <assert.h>
#include "log.hpp"
#include "soft_volume_control.hpp"
#include "scope_guard.hpp"
#include "utility.hpp"


namespace nxplay
{


soft_volume_control::soft_volume_control()
	: m_bin(nullptr)
	, m_volume_elem(nullptr)
	, m_volume(1.0)
	, m_mute(false)
{
}


soft_volume_control::~soft_volume_control()
{
	teardown();
}


bool soft_volume_control::setup()
{
	GstElement *audioconvert = nullptr;

	assert(m_bin == nullptr);
	assert(m_volume_elem == nullptr);

	auto elems_guard = make_scope_guard([&]()
	{
		checked_unref(m_bin);
		checked_unref(audioconvert);
		checked_unref(m_volume_elem);
	});

	if ((m_bin = gst_bin_new("processing_obj_volume_bin")) == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create volume bin");
		return false;
	}

	if ((audioconvert = gst_element_factory_make("audioconvert", "processing_obj_audioconvert_elem")) == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audioconvert element");
		return false;
	}

	if ((m_volume_elem = gst_element_factory_make("volume", "processing_obj_volume_elem")) == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create volume element");
		return false;
	}

	gst_bin_add_many(GST_BIN(m_bin), audioconvert, m_volume_elem, nullptr);
	gst_element_link(audioconvert, m_volume_elem);

	elems_guard.unguard();

	g_object_set(
		G_OBJECT(m_volume_elem),
		"volume", gdouble(m_volume),
		"mute", gboolean(m_mute),
		nullptr
	);

	GstPad *sinkpad = gst_element_get_static_pad(audioconvert, "sink");
	GstPad *srcpad = gst_element_get_static_pad(m_volume_elem, "src");
	gst_element_add_pad(m_bin, gst_ghost_pad_new("sink", sinkpad));
	gst_element_add_pad(m_bin, gst_ghost_pad_new("src", srcpad));
	gst_object_unref(GST_OBJECT(sinkpad));
	gst_object_unref(GST_OBJECT(srcpad));

	gst_object_ref_sink(GST_OBJECT(m_bin));

	return true;
}


void soft_volume_control::teardown()
{
	checked_unref(m_bin);
	m_volume_elem = nullptr;
}


GstElement* soft_volume_control::get_gst_element()
{
	return m_bin;
}


void soft_volume_control::set_volume(double const p_new_volume)
{
	m_volume = p_new_volume;

	if (m_volume_elem != nullptr)
		g_object_set(G_OBJECT(m_volume_elem), "volume", gdouble(p_new_volume), nullptr);
}


double soft_volume_control::get_volume() const
{
	return m_volume;
}


void soft_volume_control::set_muted(bool const p_mute)
{
	m_mute = p_mute;

	if (m_volume_elem != nullptr)
		g_object_set(G_OBJECT(m_volume_elem), "mute", gboolean(p_mute), nullptr);
}


bool soft_volume_control::is_muted() const
{
	return m_mute;
}


} // namespace nxplay end
