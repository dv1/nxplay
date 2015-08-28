/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_SOFT_VOLUME_CONTROL_HPP
#define NXPLAY_SOFT_VOLUME_CONTROL_HPP

#include <gst/gst.h>
#include "processing_object.hpp"


/** nxplay */
namespace nxplay
{


/// Software volume control processing object.
/**
 * Implements software volume control by using the GStreamer volume object.
 * The volume object is created in setup() and unref'd in teardown().
 */
class soft_volume_control
	: public processing_object
{
public:
	soft_volume_control();
	~soft_volume_control();

	virtual bool setup() override;
	virtual void teardown() override;

	virtual GstElement* get_gst_element() override;

	/// Sets the current volume, with the given format.
	/**
	 * @param p_new_volume New volume to use, valid range: 0.0 (silence) - 1.0 (full volume)
	 * @param p_format Format of the new volume to use
	 */
	void set_volume(double const p_new_volume);
	/// Retrieves the current volume in the given format.
	/**
	 * @return Current volume, or 1.0 if volume is not supported by the pipeline
	 */
	double get_volume() const;
	/// Mutes/unmutes the audio playback
	/**
	 * If nothing inside the pipeline supports muting, this call is ignored.
	 *
	 * @param p_mute true if audio shall be muted
	 */
	void set_muted(bool const p_mute);
	/// Determines if audio playback is currently muted or not.
	/**
	 * If nothing inside the pipeline supports muting, this call return false.
	 *
	 * @return true if audio playback is currently muted, false otherwise
	 */
	bool is_muted() const;

private:
	GstElement *m_bin, *m_volume_elem;

	double m_volume;
	bool m_mute;
};


} // namespace nxplay end


#endif
