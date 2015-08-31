/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_PROCESSING_OBJECT_HPP
#define NXPLAY_PROCESSING_OBJECT_HPP

#include <gst/gst.h>


/** nxplay */
namespace nxplay
{


/// Class for processing media data; placed right before a suitable output sink.
/**
 * This is useful for processing data right before it is presented. Examples
 * include: soft volume controls, equalizers, room correction, resamplers,
 * pitch correction etc.
 *
 * Some subclasses might want to create the GstElement only once. Others might
 * opt to create it in every get_gst_element() call instead.
 * In the former case, the element is created in the setup() and unref'd in the
 * teardown() function. In setup(), it is also sink-ref'd (see gst_object_ref_sink()
 * and gst_object_unref() for details). In the latter case, simply creating the
 * element in get_gst_element() and returning it *without* sink-ref'ing it is enough.
 *
 * The reason for this is that this element is inserted in the pipeline's bin with
 * the gst_bin_add() call, and this call sink-refs the element (also, the inserted
 * elements are later unref'd when the pipeline element is destroyed).
 */
class processing_object
{
public:
	virtual ~processing_object();

	/// Sets up the object's states (called during pipeline initialization).
	/**
	 * If a GStreamer element is created here, it must be sink-ref'd by
	 * calling gst_object_ref_sink().
	 *
	 * Default implementation does nothing.
	 *
	 * @return true if setup succeeded, false otherwise
	 */
	virtual bool setup();
	/// Tears down the object's states (called during pipeline shutdown).
	/**
	 * If a GStreamer element was created in setup(), it is unref'd here.
	 *
	 * Default implementation does nothing.
	 */
	virtual void teardown();

	/// Returns the element associated with this object.
	/**
	 * The element returned here can either be created in setup(), or in here.
	 * See the class documentation for details.
	 *
	 * @return A GStreamer element, or nullptr is something went wrong
	 */
	virtual GstElement* get_gst_element() = 0;
};


} // namespace nxplay end


#endif
