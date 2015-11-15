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
#include "main_pipeline.hpp"
#include "scope_guard.hpp"
#include "utility.hpp"


namespace nxplay
{


namespace
{


char const *stream_eos_msg_name = "nxplay-stream-eos";
char const *element_shutdown_marker = "nxplay-element-shutdown";
guint64 const buffer_estimation_duration_default = GST_SECOND * 2;
guint64 const buffer_timeout_default = GST_SECOND * 2;
guint const buffer_size_limit_default = 1024 * 1024 * 2;
guint const buffer_low_threshold_default = 10;
guint const buffer_high_threshold_default = 99;


GstFormat pos_unit_to_format(position_units const p_unit)
{
	switch (p_unit)
	{
		case position_unit_nanoseconds: return GST_FORMAT_TIME;
		case position_unit_bytes:     return GST_FORMAT_BYTES;
		default: assert(0);
	}

	// not supposed to be reached due to the assert above; just shuts up the compiler
	return GST_FORMAT_UNDEFINED;
}


char const * yesno(bool const p_value)
{
	return p_value ? "yes" : "no";
}


char const * pos_unit_description(position_units const p_unit)
{
	switch (p_unit)
	{
		case position_unit_nanoseconds: return "nanoseconds";
		case position_unit_bytes:     return "bytes";
		default: assert(0);
	}

	// not supposed to be reached due to the assert above; just shuts up the compiler
	return "<invalid>";
}


std::string message_to_str(GstMessage *p_msg, GstDebugLevel const p_level)
{
	// NOTE: even though this might be used for info and warnings as well,
	// it is still using GError, which can sound confusing
	GError *error = nullptr;
	gchar *debug_info = nullptr;

	auto elems_guard = make_scope_guard([&]()
	{
		if (error != nullptr)
			g_error_free(error);
		g_free(debug_info);
	});

	auto dbgstr = [&]() { return (debug_info == nullptr) ? "<none>" : debug_info; };

	switch (p_level)
	{
		case GST_LEVEL_INFO:
			gst_message_parse_info(p_msg, &error, &debug_info);
			NXPLAY_LOG_MSG(info, error->message << " (reported by: " << GST_MESSAGE_SRC_NAME(p_msg) << " debug info: " << dbgstr() << ")");
			break;
		case GST_LEVEL_WARNING:
			gst_message_parse_warning(p_msg, &error, &debug_info);
			NXPLAY_LOG_MSG(warning, error->message << " (reported by: " << GST_MESSAGE_SRC_NAME(p_msg) << " (debug info: " << dbgstr() << ")");
			break;
		case GST_LEVEL_ERROR:
			gst_message_parse_error(p_msg, &error, &debug_info);
			NXPLAY_LOG_MSG(error, error->message << " (reported by: " << GST_MESSAGE_SRC_NAME(p_msg) << " (debug info: " << dbgstr() << ")");
			break;
		default:
		{
			std::string text = std::string(error->message) + "<invalid level " + gst_debug_level_get_name(p_level) + "> reported by: " + GST_MESSAGE_SRC_NAME(p_msg);
			NXPLAY_LOG_MSG(error, text);
			return text;
		}
	}

	return error->message;
}


// Functions to set a marker in a GstObject.
// This marker is then used to check if a given object is marked
// for shutdown, meaning it will be gone soon. When a stream enters
// its destructor, it marks all of the uridecodebin's child objects.
// This is useful for example to filter out error messages that
// originated from one of the uridecodebin children during shutdown.
// Such error messages are pointless (since the object is anyway
// going away) and only cause erroneous pipeline reinitializations.


void mark_object_as_shutting_down(GObject *p_object)
{
	g_object_set_data(p_object, element_shutdown_marker, gpointer(1));
}


bool is_object_marked_as_shutting_down(GObject *p_object)
{
	return g_object_get_data(p_object, element_shutdown_marker) == gpointer(1);
}


} // unnamed namespace end



main_pipeline::stream::stream(main_pipeline &p_pipeline, guint64 const p_token, media &&p_media, GstBin *p_container_bin, GstElement *p_concat_elem, playback_properties const &p_properties)
	: m_pipeline(p_pipeline)
	, m_token(p_token)
	, m_media(std::move(p_media))
	, m_playback_properties(p_properties)
	, m_uridecodebin_elem(nullptr)
	, m_identity_elem(nullptr)
	, m_concat_elem(p_concat_elem)
	, m_queue_elem(nullptr)
	, m_identity_srcpad(nullptr)
	, m_concat_sinkpad(nullptr)
	, m_container_bin(p_container_bin)
	, m_is_buffering(false)
	, m_is_live(false)
	, m_is_live_status_known(false)
	, m_is_seekable(false)
	, m_buffering_is_blocked(false)
	, m_bitrate(0)
	, m_buffer_estimation_duration(buffer_estimation_duration_default)
	, m_buffer_timeout(buffer_timeout_default)
	, m_buffer_size_limit(buffer_size_limit_default)
	, m_effective_buffer_size_limit(0)
	, m_buffering_timeout_enabled(true)
{
	assert(m_container_bin != nullptr);

	NXPLAY_LOG_MSG(debug, "constructing stream " << guintptr(this) << " with media URI " << m_media.get_uri());

	auto elems_guard = make_scope_guard([&]()
	{
		checked_unref(m_uridecodebin_elem);
		checked_unref(m_identity_elem);
	});

	// Try to create the uridecodebin and identity elements
	// The identity element is needed to provide the pipeline's concat
	// element a srcpad that exists right from the start.
	// uridecodebin only creates its srcpads later, while loading.
	// By using an identity element in between, the concat element
	// can be linked immediately, and uridecodebin and identity is
	// linked later, when uridecodebin has loaded and produces srcpads

	if ((m_uridecodebin_elem = gst_element_factory_make("uridecodebin", nullptr)) == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create uridecodebin element");
		return;
	}

	if ((m_identity_elem = gst_element_factory_make("identity", nullptr)) == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create identity element");
		return;
	}

	gst_bin_add_many(m_container_bin, m_uridecodebin_elem, m_identity_elem, nullptr);

	// Link identity and concat
	m_identity_srcpad = gst_element_get_static_pad(m_identity_elem, "src");
	GstPadTemplate *concat_sinkpad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(m_concat_elem), "sink_%u");
	m_concat_sinkpad = gst_element_request_pad(m_concat_elem, concat_sinkpad_template, nullptr, nullptr);
	gst_pad_link(m_identity_srcpad, m_concat_sinkpad);

	// Install srcpad probe to intercept bitrate tags
	gst_pad_add_probe(
		m_identity_srcpad,
		GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
		static_tag_probe,
		gpointer(this),
		nullptr
	);

	// "async-handling" has to be set to TRUE to ensure no internal async state changes
	// "escape" from the uridecobin and affect the rest of the pipeline (otherwise, the
	// pipeline may be set to PAUSED state, which affects gapless playback)
	g_object_set(G_OBJECT(m_uridecodebin_elem), "uri", m_media.get_uri().c_str(), "async-handling", gboolean(TRUE), NULL);
	g_signal_connect(G_OBJECT(m_uridecodebin_elem), "pad-added", G_CALLBACK(static_new_pad_callback), gpointer(this));
	g_signal_connect(G_OBJECT(m_uridecodebin_elem), "element-added", G_CALLBACK(static_element_added_callback), gpointer(this));

	// Configure buffering values

	set_buffer_estimation_duration(p_properties.m_buffer_estimation_duration);
	set_buffer_timeout(p_properties.m_buffer_timeout);
	set_buffer_size_limit(p_properties.m_buffer_size);

	set_buffer_thresholds(p_properties.m_low_buffer_threshold, p_properties.m_high_buffer_threshold);

	// Do not sync states with parent here just yet, since the static_new_pad_callback
	// does checks to see if this is the current media. Let the caller assign this new
	// stream to m_current_stream or m_next_stream first. static_new_pad_callback
	// won't be called until the states are synced by the sync_states() function.

	// All done; scope guard can be disengaged
	elems_guard.unguard();
}


main_pipeline::stream::~stream()
{
	// Make sure the destructor does not run at the same time as
	// the static_new_pad_callback
	std::unique_lock < std::mutex > lock(m_shutdown_mutex);

	// Mark all of the child objects to ensure other areas know
	// these objects are being shut down. This is in particular
	// important for the sync handler, to be able to check if
	// a receiver error message comes from one of these objects.
	{
		GstIterator *iter = gst_bin_iterate_recurse(GST_BIN(m_uridecodebin_elem));

		if (iter != NULL)
		{
			GValue item = G_VALUE_INIT;
			bool done = false;

			while (!done)
			{
				switch (gst_iterator_next(iter, &item))
				{
					case GST_ITERATOR_OK:
					{
						GObject *obj = G_OBJECT(g_value_get_object(&item));
						mark_object_as_shutting_down(obj);
						g_value_reset(&item);
						break;
					}

					case GST_ITERATOR_RESYNC:
						gst_iterator_resync(iter);
						break;

					case GST_ITERATOR_ERROR:
					case GST_ITERATOR_DONE:
						done = true;
						break;
				}
			}

			g_value_unset(&item);
			gst_iterator_free(iter);
		}
	}

	if (m_container_bin == nullptr)
		return;

	NXPLAY_LOG_MSG(debug, "destroying stream " << guintptr(this));

	// Release the request pad first. Releasing must be done before the
	// element states are set to NULL, to give concat the chance to
	// wake up all streaming threads from sink pads which are currently
	// waiting. Then, concat deactivates the sinkpad. A deactivated
	// sinkpad will cause gst_pad_push() calls to return GST_FLOW_FLUSHING,
	// which does not constitute an error. Therefore, releasing here is OK
	// even if the elements' are currently set to PLAYING.
	// Releasing *below* the state switch to NULL would potentially cause
	// deadlocks.
	if (m_concat_sinkpad != nullptr)
		gst_element_release_request_pad(m_concat_elem, m_concat_sinkpad);

	// The states of the elements are locked before the element states
	// are set to NULL. This prevents race conditions where the pipeline is
	// switching to another state (for example, PLAYING) while this stream
	// is being destroyed. If the state weren't locked, the pipeline's switch
	// would propagate to the element.
	gst_element_set_locked_state(m_uridecodebin_elem, TRUE);
	gst_element_set_locked_state(m_identity_elem, TRUE);

	// Shut down the elements
	gst_element_set_state(m_uridecodebin_elem, GST_STATE_NULL);
	gst_element_set_state(m_identity_elem, GST_STATE_NULL);

	// Unlink identity and concat
	if (m_concat_sinkpad != nullptr)
	{
		gst_pad_unlink(m_identity_srcpad, m_concat_sinkpad);
		gst_object_unref(GST_OBJECT(m_identity_srcpad));
		gst_object_unref(GST_OBJECT(m_concat_sinkpad));
	}

	// Unlink uridecodebin and identity
	gst_element_unlink(m_uridecodebin_elem, m_identity_elem);

	// Finally, remove the elements from the pipeline
	// No need to unref these elements, since gst_bin_remove() does it automatically
	gst_bin_remove_many(m_container_bin, m_uridecodebin_elem, m_identity_elem, NULL);

	NXPLAY_LOG_MSG(debug, "stream " << guintptr(this) << " destroyed");
}


void main_pipeline::stream::sync_states()
{
	gst_element_sync_state_with_parent(m_identity_elem);
	gst_element_sync_state_with_parent(m_uridecodebin_elem);
}


GstPad* main_pipeline::stream::get_srcpad()
{
	return m_identity_srcpad;
}


guint64 main_pipeline::stream::get_token() const
{
	return m_token;
}


media const & main_pipeline::stream::get_media() const
{
	return m_media;
}


playback_properties const & main_pipeline::stream::get_playback_properties() const
{
	return m_playback_properties;
}


bool main_pipeline::stream::contains_object(GstObject *p_object)
{
	return gst_object_has_as_ancestor(p_object, GST_OBJECT(m_uridecodebin_elem));
}


void main_pipeline::stream::set_buffer_estimation_duration(boost::optional < guint64 > const &p_new_duration)
{
	m_buffer_estimation_duration = p_new_duration ? *p_new_duration : buffer_estimation_duration_default;
	update_buffer_limits();
}


void main_pipeline::stream::set_buffer_timeout(boost::optional < guint64 > const &p_new_timeout)
{
	m_buffer_timeout = p_new_timeout ? *p_new_timeout : buffer_timeout_default;
	update_buffer_limits();
}


void main_pipeline::stream::set_buffer_size_limit(boost::optional < guint > const &p_new_size)
{
	m_buffer_size_limit = p_new_size ? *p_new_size : buffer_size_limit_default;
	update_buffer_limits();
}


void main_pipeline::stream::set_buffer_thresholds(boost::optional < guint > const &p_low_threshold, boost::optional < guint > const &p_high_threshold)
{
	m_low_buffer_threshold = p_low_threshold ? *p_low_threshold : buffer_low_threshold_default;
	m_high_buffer_threshold = p_high_threshold ? *p_high_threshold : buffer_high_threshold_default;
	update_buffer_limits();
}


boost::optional < guint > main_pipeline::stream::get_current_buffer_level() const
{
	if (m_queue_elem != nullptr)
	{
		guint buffer_level = 0;
		g_object_get(G_OBJECT(m_queue_elem), "current-level-bytes", &buffer_level, nullptr);
		return buffer_level;
	}
	else
		return boost::none;
}


guint main_pipeline::stream::get_effective_buffer_size_limit() const
{
	return m_effective_buffer_size_limit;
}


void main_pipeline::stream::set_buffering(bool const p_flag)
{
	m_is_buffering = p_flag;
}


bool main_pipeline::stream::is_buffering() const
{
	return m_is_buffering;
}


bool main_pipeline::stream::is_live() const
{
	return m_is_live;
}


bool main_pipeline::stream::is_live_status_known() const
{
	return m_is_live_status_known;
}


void main_pipeline::stream::recheck_live_status(bool const p_is_current_media)
{
	GstQuery *query = gst_query_new_latency();
	gboolean is_live;

	if (gst_pad_query(m_identity_srcpad, query))
	{
		gst_query_parse_latency(query, &is_live, nullptr, nullptr);
		m_is_live = is_live;
		m_is_live_status_known = true;

		if (m_pipeline.m_callbacks.m_is_live_callback)
			m_pipeline.m_callbacks.m_is_live_callback(m_media, m_token, p_is_current_media, m_is_live);
	}
	else
		m_is_live_status_known = false;

	NXPLAY_LOG_MSG(debug, "live status is known: " << m_is_live_status_known << "   is live: " << m_is_live);

	gst_query_unref(query);

}


bool main_pipeline::stream::is_seekable() const
{
	return m_is_seekable;
}


bool main_pipeline::stream::performs_buffering() const
{
	/* If the pipeline is live, and the live status is known, no buffering will be done.
	 * If the pipeline is live, but the live status isn'T known, no buffering will be done
	 * either, because at this point, it is assumed to be live.
	 * If the pipeline isn't live, buffering is done only is a queue element has been found. */
	return (m_queue_elem != nullptr) && is_live_status_known() && !is_live();
}


void main_pipeline::stream::enable_buffering_timeout(bool const p_do_enable)
{
	m_buffering_timeout_enabled = p_do_enable;
	update_buffer_limits();
}


void main_pipeline::stream::block_buffering(bool const p_do_block)
{
	{
		std::unique_lock < std::mutex > (m_block_mutex);
		if (m_buffering_is_blocked != p_do_block)
		{
			NXPLAY_LOG_MSG(debug, (p_do_block ? "blocking" : "unblocking") << " the buffering of the stream with URI " << m_media.get_uri());
			m_buffering_is_blocked = p_do_block;
		}
	}

	m_block_condition.notify_all();
}


void main_pipeline::stream::static_new_pad_callback(GstElement *, GstPad *p_pad, gpointer p_data)
{
	stream *self = static_cast < stream* > (p_data);

	// Make sure this callback does not run at the same time as the destructor
	std::unique_lock < std::mutex > lock(self->m_shutdown_mutex);

	NXPLAY_LOG_MSG(debug, "linking new decodebin pad, stream: " << guintptr(self));

	// only link once
	if (GST_PAD_IS_LINKED(p_pad))
		return;

	// check media type
	GstCaps *caps = gst_pad_query_caps(p_pad, nullptr);
	GstStructure *s = gst_caps_get_structure(caps, 0);
	bool match = g_strrstr(gst_structure_get_name(s), "audio");
	gst_caps_unref(caps);
	if (!match)
		return;

	// link this pad to the identity element
	GstPad *identity_sinkpad = gst_element_get_static_pad(self->m_identity_elem, "sink");
	gst_pad_link(p_pad, identity_sinkpad);
	gst_object_unref(GST_OBJECT(identity_sinkpad));

	// Find out some details about the media, namely
	// whether or not it is seekable and live
	GstQuery *query;
	gboolean is_seekable = FALSE;

	query = gst_query_new_seeking(GST_FORMAT_TIME);
	if (gst_pad_query(p_pad, query))
		gst_query_parse_seeking(query, nullptr, &is_seekable, nullptr, nullptr);
	gst_query_unref(query);

	self->m_is_seekable = is_seekable;
	NXPLAY_LOG_MSG(debug, "decodebin pad linked  stream: " << guintptr(self) << "  is seekable: " << is_seekable);

	bool is_current_media = (self->m_pipeline.m_current_stream.get() == self);

	if (self->m_pipeline.m_callbacks.m_is_seekable_callback)
		self->m_pipeline.m_callbacks.m_is_seekable_callback(self->m_media, self->m_token, is_current_media, self->m_is_seekable);

	// Install a buffer block probe if a queue is present. The buffer block
	// probe blocks the stream by using a condition variable (m_block_condition)
	// and waiting for the notification from the main thread.
	if (self->m_queue_elem != nullptr)
	{
		GstPad *sinkpad = gst_element_get_static_pad(self->m_queue_elem, "sink");
		gst_pad_add_probe(
			sinkpad,
			GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER),
			static_buffering_block_probe,
			gpointer(self),
			nullptr
		);
		gst_object_unref(GST_OBJECT(sinkpad));
	}

	self->recheck_live_status(is_current_media);
}


void main_pipeline::stream::static_element_added_callback(GstElement *, GstElement *p_element, gpointer p_data)
{
	stream *self = static_cast < stream* > (p_data);

	gchar *name_cstr = gst_element_get_name(p_element);
	bool is_queue = g_str_has_prefix(name_cstr, "queue");

	if (is_queue)
	{
		NXPLAY_LOG_MSG(debug, "found queue element \"" << name_cstr << "\"");
		self->m_queue_elem = p_element;

		// Queue is now available; update buffer limits to make sure the queue
		// is configured with the computed limit values
		self->update_buffer_limits();
	}

	g_free(name_cstr);
}


GstPadProbeReturn main_pipeline::stream::static_tag_probe(GstPad *, GstPadProbeInfo *p_info, gpointer p_data)
{
	stream *self = static_cast < stream* > (p_data);

	if (G_UNLIKELY((p_info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) == 0))
		return GST_PAD_PROBE_OK;

	GstEvent *event = gst_pad_probe_info_get_event(p_info);
	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_TAG:
		{
			if (self->m_bitrate == 0)
			{
				GstTagList *tag_list;
				gst_event_parse_tag(GST_PAD_PROBE_INFO_EVENT(p_info), &tag_list);

				guint bitrate = 0;
				if (gst_tag_list_get_uint_index(tag_list, GST_TAG_NOMINAL_BITRATE, 0, &bitrate) ||
				    gst_tag_list_get_uint_index(tag_list, GST_TAG_BITRATE, 0, &bitrate))
				{
					NXPLAY_LOG_MSG(debug, "Found bitrate: " << bitrate);
					self->m_bitrate = bitrate;

					// Bitrate found; update buffer limits, since now it can
					// actually estimate a size limit out of duration & bitrate
					self->update_buffer_limits();
				}
			}

			break;
		};

		default:
			break;
	}

	return GST_PAD_PROBE_OK;
}


GstPadProbeReturn main_pipeline::stream::static_buffering_block_probe(GstPad *, GstPadProbeInfo *p_info, gpointer p_data)
{
	stream *self = static_cast < stream* > (p_data);

	if (G_UNLIKELY((p_info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0))
		return GST_PAD_PROBE_OK;

	// Wait until the main thread sends a notification. This effectively
	// blocks the streaming thread, because pad probes are ran in this
	// same streaming thread.
	{
		std::unique_lock < std::mutex > lock(self->m_block_mutex);
		while (self->m_buffering_is_blocked)
		{
			NXPLAY_LOG_MSG(trace, "waiting for stream with URI " << self->m_media.get_uri() << " to be unblocked");
			self->m_block_condition.wait(lock);
		}
	}

	return GST_PAD_PROBE_OK;
}


void main_pipeline::stream::update_buffer_limits()
{
	guint64 calc_size_limit = 0;

	if ((m_bitrate != 0) && (m_buffer_estimation_duration > 0))
	{
		calc_size_limit = gst_util_uint64_scale_int(m_buffer_estimation_duration, m_bitrate / 8, GST_SECOND);
		NXPLAY_LOG_MSG(debug, "estimated a size limit of " << calc_size_limit << " bytes out of a bitrate of " << m_bitrate << " bps and an estimation duration of " << m_buffer_estimation_duration << " nanoseconds");
		if (calc_size_limit > G_MAXUINT)
			calc_size_limit = G_MAXUINT;
	}

	m_effective_buffer_size_limit = (calc_size_limit == 0) ? m_buffer_size_limit : std::min(m_buffer_size_limit, guint(calc_size_limit));

	NXPLAY_LOG_MSG(debug, "setting stream buffer size limit to " << m_effective_buffer_size_limit << " bytes");

	g_object_set(
		G_OBJECT(m_uridecodebin_elem),
		"buffer-size", m_effective_buffer_size_limit,
		nullptr
	);

	if (m_queue_elem != nullptr)
	{
		// Set the max-size-time. If no buffer timeout is set, the
		// max-size-time is set to 0.
		// Since this queue gets BYTES segments as incoming data, a nonzero value
		// causes the queue to spawn a timer. If this timer runs out before the
		// queue is full (= reaches the max-size-bytes limit), it will signal
		// buffering completion. This is useful to make sure a queue does not
		// buffer for long periods of time.
		g_object_set(
			G_OBJECT(m_queue_elem),
			"max-size-time", guint64(m_buffering_timeout_enabled ? m_buffer_timeout : 0),
			"max-size-bytes", m_effective_buffer_size_limit,
			"low-percent", gint(m_low_buffer_threshold),
			"high-percent", gint(m_high_buffer_threshold),
			nullptr
		);
	}
}



main_pipeline::main_pipeline(callbacks const &p_callbacks, GstClockTime const p_needs_next_media_time, guint const p_update_interval, bool const p_postpone_all_tags, processing_objects const &p_processing_objects)
	: m_state(state_idle)
	, m_duration_in_nanoseconds(-1)
	, m_duration_in_bytes(-1)
	, m_block_abouttoend_notifications(false)
	, m_force_next_duration_update(true)
	, m_stream_eos_seen(false)
	, m_postpone_all_tags(p_postpone_all_tags)
	, m_timeout_source(nullptr)
	, m_needs_next_media_time(p_needs_next_media_time)
	, m_update_interval(p_update_interval)
	, m_current_gstreamer_state(GST_STATE_NULL)
	, m_pending_gstreamer_state(GST_STATE_VOID_PENDING)
	, m_pipeline_elem(nullptr)
	, m_concat_elem(nullptr)
	, m_audiosink_elem(nullptr)
	, m_bus(nullptr)
	, m_watch_source(nullptr)
	, m_next_token(0)
	, m_thread_loop(nullptr)
	, m_thread_loop_context(nullptr)
	, m_callbacks(p_callbacks)
	, m_processing_objects(p_processing_objects)
{
	// By default, add the bitrate tags to the list of
	// forcibly postponed ones, since these can be frequently
	// updated sometimes
	m_tags_to_always_postpone.insert(GST_TAG_MINIMUM_BITRATE);
	m_tags_to_always_postpone.insert(GST_TAG_MAXIMUM_BITRATE);
	m_tags_to_always_postpone.insert(GST_TAG_BITRATE);

	// Start the GLib mainloop thread
	start_thread();
}


main_pipeline::~main_pipeline()
{
	// Stop playback immediately and cancel transitioning states
	// by shutting down the pipeline right now
	{
		// Lock the loop mutex to ensure the pipeline isn't
		// shut down during a bus watch or playback timeout
		// callback call
		std::unique_lock < std::mutex > lock(m_loop_mutex);
		shutdown_pipeline_nolock();
	}

	// Now stop the thread with the GLib mainloop
	stop_thread();
}


void main_pipeline::set_buffer_size_limit(boost::optional < guint > const &p_new_size)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	if (m_current_stream)
		m_current_stream->set_buffer_size_limit(p_new_size);
}


void main_pipeline::set_buffer_estimation_duration(boost::optional < guint64 > const &p_new_duration)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	if (m_current_stream)
		m_current_stream->set_buffer_estimation_duration(p_new_duration);
}


void main_pipeline::set_buffer_timeout(boost::optional < guint64 > const &p_new_timeout)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	if (m_current_stream)
		m_current_stream->set_buffer_timeout(p_new_timeout);
}


void main_pipeline::set_buffer_thresholds(boost::optional < guint > const &p_new_low_threshold, boost::optional < guint > const &p_new_high_threshold)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	if (m_current_stream)
		m_current_stream->set_buffer_thresholds(p_new_low_threshold, p_new_high_threshold);
}


bool main_pipeline::play_media_impl(guint64 const p_token, media &&p_media, bool const p_play_now, playback_properties const &p_properties)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	return play_media_nolock(p_token, std::move(p_media), p_play_now, p_properties);
}


guint64 main_pipeline::get_new_token()
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	return m_next_token++; // Generate unique tokens by using a monotonically increasing counter
}


void main_pipeline::stop()
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	stop_nolock();
}


void main_pipeline::set_paused(bool const p_paused)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	set_paused_nolock(p_paused);
}


bool main_pipeline::is_transitioning() const
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	return is_transitioning_nolock();
}


states main_pipeline::get_current_state() const
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	return m_state;
}


void main_pipeline::set_current_position(gint64 const p_new_position, position_units const p_unit)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);
	set_current_position_nolock(p_new_position, p_unit);
}


gint64 main_pipeline::get_current_position(position_units const p_unit) const
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);

	if ((m_pipeline_elem == nullptr) || (m_state == state_idle))
		return -1;

        gint64 position;
        gboolean success = gst_element_query_position(GST_ELEMENT(m_pipeline_elem), pos_unit_to_format(p_unit), &position);
        return success ? position : -1;
}


gint64 main_pipeline::get_duration(position_units const p_unit) const
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);

	switch (p_unit)
	{
		case position_unit_nanoseconds: return m_duration_in_nanoseconds;
		case position_unit_bytes:     return m_duration_in_bytes;
		default: assert(0);
	}

	// not supposed to be reached due to the assert above; just shuts up the compiler
	return -1;
}



void main_pipeline::force_postpone_tag(std::string const &p_tag, bool const p_postpone)
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);

	auto iter = m_tags_to_always_postpone.find(p_tag);
	if (p_postpone && (iter == m_tags_to_always_postpone.end()))
		m_tags_to_always_postpone.insert(p_tag);
	else if (!p_postpone && (iter != m_tags_to_always_postpone.end()))
		m_tags_to_always_postpone.erase(iter);
}


bool main_pipeline::is_transitioning_nolock() const
{
	switch (m_state)
	{
		case state_starting:
		case state_stopping:
		case state_seeking:
		case state_buffering:
			return true;
		default:
			return (m_pending_gstreamer_state != GST_STATE_VOID_PENDING);
	}
}


bool main_pipeline::set_gstreamer_state_nolock(GstState const p_new_gstreamer_state)
{
	// Catch redundant state changes
	if (m_current_gstreamer_state == p_new_gstreamer_state)
		return true;

	NXPLAY_LOG_MSG(
		debug,
		"switching state of GStreamer pipeline from " <<
		gst_element_state_get_name(m_current_gstreamer_state) <<
		" to "
		<< gst_element_state_get_name(p_new_gstreamer_state)
	);

	GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(m_pipeline_elem), p_new_gstreamer_state);
	NXPLAY_LOG_MSG(debug, "return value after starting state change: " << gst_element_state_change_return_get_name(ret));

	switch (ret)
	{
		case GST_STATE_CHANGE_FAILURE:
			NXPLAY_LOG_MSG(error, "switching GStreamer pipeline state to " << gst_element_state_get_name(p_new_gstreamer_state) << " failed");
			return false;
		default:
			return true;
	}
}


void main_pipeline::handle_postponed_task_nolock()
{
	postponed_task::types type = m_postponed_task.m_type;
	m_postponed_task.m_type = postponed_task::type_none;

	switch (type)
	{
		case postponed_task::type_play:
			NXPLAY_LOG_MSG(debug, "handling postponed play_media task");
			play_media_nolock(m_postponed_task.m_token, std::move(m_postponed_task.m_media), true, m_postponed_task.m_playback_properties);
			break;

		case postponed_task::type_stop:
			NXPLAY_LOG_MSG(debug, "handling postponed stop task");
			stop_nolock();
			break;

		case postponed_task::type_pause:
			NXPLAY_LOG_MSG(debug, "handling postponed pause task");
			set_paused_nolock(m_postponed_task.m_paused);
			break;

		case postponed_task::type_set_position:
			NXPLAY_LOG_MSG(debug, "handling postponed set_position task");
			set_current_position_nolock(m_postponed_task.m_position, m_postponed_task.m_position_format);
			break;

		case postponed_task::type_set_state:
			NXPLAY_LOG_MSG(debug, "handling postponed set_state task");
			gst_element_set_state(m_pipeline_elem, m_postponed_task.m_gstreamer_state);
			break;

		default:
			break;
	}
}


main_pipeline::stream_sptr main_pipeline::setup_stream_nolock(guint64 const p_token, media &&p_media, playback_properties const &p_properties)
{
	stream_sptr new_stream(new stream(
		*this,
		p_token,
		std::move(p_media),
		GST_BIN(m_pipeline_elem),
		m_concat_elem,
		p_properties
	));

	// Add an EOS probe, necessary for the gapless switching between next and current streams
	gst_pad_add_probe(
		new_stream->get_srcpad(),
		GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
		static_stream_eos_probe,
		gpointer(this),
		nullptr
	);

	return new_stream;
}


GstPadProbeReturn main_pipeline::static_stream_eos_probe(GstPad *, GstPadProbeInfo *p_info, gpointer p_data)
{
	main_pipeline *self = static_cast < main_pipeline* > (p_data);

	if ((p_info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) == 0)
		return GST_PAD_PROBE_OK;

	GstEvent *event = gst_pad_probe_info_get_event(p_info);
	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_EOS:
		{
			NXPLAY_LOG_MSG(debug, "EOS event observed at stream srcpad");

			std::unique_lock < std::mutex > lock(self->m_stream_mutex);

			// m_current_stream and m_next_token are no longer up to date.
			// Raise the flag to ensure the next make_next_stream_current_nolock()
			// call updates these two values properly.
			self->m_stream_eos_seen = true;

			// Post a custom message to the bus to trigger a bus watch
			// call as soon as possible.
			gst_bus_post(
				self->m_bus,
				gst_message_new_application(
					GST_OBJECT(self->m_pipeline_elem),
					gst_structure_new_empty(stream_eos_msg_name)
				)
			);

			break;
		}

		default:
			break;
	}

	return GST_PAD_PROBE_OK;
}


bool main_pipeline::initialize_pipeline_nolock()
{
	// Construct the pipeline

	GstElement *audioqueue_elem = nullptr;
	GstElement *audioconvert_elem = nullptr;
	GstElement *audioresample_elem = nullptr;

	if (m_pipeline_elem != nullptr)
		shutdown_pipeline_nolock();

	auto pipeline_guard = make_scope_guard([&]()
	{
		checked_unref(m_pipeline_elem);
	});
	auto elems_guard = make_scope_guard([&]()
	{
		checked_unref(m_concat_elem);
		checked_unref(audioconvert_elem);
		checked_unref(m_audiosink_elem);
	});

	m_pipeline_elem = gst_pipeline_new(nullptr);
	if (m_pipeline_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create pipeline element");
		return false;
	}

	m_concat_elem = gst_element_factory_make("concat", "concat");
	if (m_concat_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create concat element");
		return false;
	}

	audioqueue_elem = gst_element_factory_make("queue", "audioqueue");
	if (audioqueue_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audio queue element");
		return false;
	}

	audioconvert_elem = gst_element_factory_make("audioconvert", "audioconvert");
	if (audioconvert_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audioconvert element");
		return false;
	}

	audioresample_elem = gst_element_factory_make("audioresample", "audioresample");
	if (audioresample_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audioresample element");
		return false;
	}

	m_audiosink_elem = gst_element_factory_make("autoaudiosink", "audiosink");
	if (m_audiosink_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audiosink element");
		return false;
	}

	g_object_set(G_OBJECT(audioresample_elem), "quality", 0, nullptr);

	for (auto obj : m_processing_objects)
	{
		if (!(obj->setup()))
		{
			NXPLAY_LOG_MSG(error, "error while setting up processing object");
			return false;
		}

		g_assert(obj->get_gst_element() != nullptr);

		gst_bin_add(GST_BIN(m_pipeline_elem), obj->get_gst_element());
	}

	gst_bin_add_many(GST_BIN(m_pipeline_elem), m_concat_elem, audioqueue_elem, audioconvert_elem, audioresample_elem, m_audiosink_elem, nullptr);
	// no need to guard the elements anymore, since the pipeline
	// now manages their lifetime
	elems_guard.unguard();

	// Link all of the elements together
	// A queue is placed right before audioconvert, to alleviate corner cases
	// which can cause audible glitches when resuming paused playback
	if (m_processing_objects.empty())
	{
		gst_element_link_many(m_concat_elem, audioqueue_elem, audioconvert_elem, audioresample_elem, m_audiosink_elem, nullptr);
	}
	else
	{
		gst_element_link(m_concat_elem, m_processing_objects.front()->get_gst_element());

		for (auto iter = m_processing_objects.begin() + 1; iter != m_processing_objects.end(); ++iter)
			gst_element_link((*(iter - 1))->get_gst_element(), (*iter)->get_gst_element());

		gst_element_link_many(m_processing_objects.back()->get_gst_element(), audioqueue_elem, audioconvert_elem, audioresample_elem, m_audiosink_elem, nullptr);
	}

	// Setup the pipeline bus
	m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline_elem));
	// Set up the sync handler
	gst_bus_set_sync_handler(m_bus, static_bus_sync_handler, gpointer(this), nullptr);
	// Set up the bus watch
	// NOT done by calling gst_bus_add_watch(), since we need to explicitely
	// connect the bus watch to the m_thread_loop_context
	m_watch_source = gst_bus_create_watch(m_bus);
	g_source_set_callback(m_watch_source, (GSourceFunc)static_bus_watch, gpointer(this), nullptr);
	g_source_attach(m_watch_source, m_thread_loop_context);

	pipeline_guard.unguard();

	// Announce the idle state
	set_state_nolock(state_idle);

	NXPLAY_LOG_MSG(debug, "pipeline initialized");

	return true;
}


void main_pipeline::shutdown_pipeline_nolock(bool const p_set_state)
{
	// Catch redundant calls
	if (m_pipeline_elem == nullptr)
		return;

	// Stop any periodic update timeouts
	shutdown_timeouts_nolock();

	// Cancel any postponed tasks
	m_postponed_task.m_type = postponed_task::type_none;

	// Set the pipeline to idle state immediately
	// Pass on p_set_state to let the function announce the idle state
	// if p_set_state is true
	// The handle_postponed_task_nolock() call inside this function
	// will never be ran, since postponed tasks are canceled earlier
	set_pipeline_to_idle_nolock(p_set_state);

	// Shut down the bus
	g_source_destroy(m_watch_source);
	g_source_unref(m_watch_source);
	gst_object_unref(GST_OBJECT(m_bus));

	for (auto obj : m_processing_objects)
	{
		gst_bin_remove(GST_BIN(m_pipeline_elem), obj->get_gst_element());
		obj->teardown();
	}

	// Cleanup and unref the pipeline
	gst_bin_remove_many(GST_BIN(m_pipeline_elem), m_concat_elem, m_audiosink_elem, nullptr);
	gst_object_unref(GST_OBJECT(m_pipeline_elem));

	m_pipeline_elem = nullptr;
	m_concat_elem = nullptr;
	m_audiosink_elem = nullptr;

	NXPLAY_LOG_MSG(debug, "pipeline shut down");
}


bool main_pipeline::reinitialize_pipeline_nolock()
{
	// Reinitialize by shutting down and initializing again
	// Suppress the idle state notification, since it makes
	// no sense to do so when shutting down the pipeline here
	shutdown_pipeline_nolock(false);
	return initialize_pipeline_nolock();
}


void main_pipeline::set_pipeline_to_idle_nolock(bool const p_set_state)
{
	NXPLAY_LOG_MSG(trace, "setting pipeline to idle");

	// Unblock buffering in the streams. This must happen before the pipeline is set to NULL,
	// otherwise a deadlock occurs.
	if (m_current_stream)
		m_current_stream->block_buffering(false);	
	if (m_next_stream)
		m_next_stream->block_buffering(false);	

	// Set the pipeline to NULL. This is always a synchronous state change.
	GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(m_pipeline_elem), GST_STATE_NULL);
	g_assert(ret != GST_STATE_CHANGE_ASYNC); // If this is ASYNC, something in GStreamer went seriously wrong

	// Discard any current, next, or old streams
	m_current_stream.reset();
	m_next_stream.reset();

	if (p_set_state)
		set_state_nolock(state_idle);

	NXPLAY_LOG_MSG(trace, "pipeline is idle");

	set_initial_state_values_nolock();

	// Reached idle state; now handle any postponed task
	handle_postponed_task_nolock();
}


void main_pipeline::set_initial_state_values_nolock()
{
	m_current_gstreamer_state = GST_STATE_NULL;
	m_pending_gstreamer_state = GST_STATE_VOID_PENDING;
	m_duration_in_nanoseconds = -1;
	m_duration_in_bytes = -1;
	m_block_abouttoend_notifications = false;
	m_force_next_duration_update = true;
	m_stream_eos_seen = false;
	m_aggregated_tag_list = tag_list();
	m_postponed_tags_list = tag_list();
}


void main_pipeline::set_state_nolock(states const p_new_state)
{
	states old_state = m_state;
	m_state = p_new_state;
	NXPLAY_LOG_MSG(trace, "state change: old: " << get_state_name(old_state) << " new: " << get_state_name(m_state));
	if (m_callbacks.m_state_changed_callback)
		m_callbacks.m_state_changed_callback(old_state, p_new_state);
}


bool main_pipeline::play_media_nolock(guint64 const p_token, media &&p_media, bool const p_play_now, playback_properties const &p_properties)
{
	// Try to play media right now if either one of these apply:
	// 1. Pipeline is in the idle state
	// 2. p_play_now is true (= caller explicitely wants to play media right now)
	// 3. p_token is the same as the token of the current stream
	if ((m_state == state_idle) || p_play_now || (m_current_stream && m_current_stream->get_token() == p_token))
	{
		if (!is_valid(p_media))
		{
			NXPLAY_LOG_MSG(error, "cannot play invalid media");
			return false;
		}

		NXPLAY_LOG_MSG(debug, "playing media with URI " << p_media.get_uri() << " now with token " << p_token);

		// Postpone call if transitioning
		if (is_transitioning_nolock())
		{
			m_postponed_task.m_type = postponed_task::type_play;
			m_postponed_task.m_media = std::move(p_media);
			m_postponed_task.m_token = p_token;
			m_postponed_task.m_playback_properties = p_properties;
			return true;
		}

		// Clear any leftover old pipeline
		if (!reinitialize_pipeline_nolock())
		{
			NXPLAY_LOG_MSG(error, "(re)initializing pipeline failed - aborting play attempt");
			return false;
		}

		// Cleanup any previously set next media
		m_next_stream.reset();

		// Switch to the starting state
		set_state_nolock(state_starting);

		// Create stream for the new current media
		m_current_stream = setup_stream_nolock(p_token, std::move(p_media), p_properties);
		// And sync states with parent, since the new stream
		// is now assigned to m_current_stream
		m_current_stream->sync_states();

		// Switch pipeline to PAUSED. The bus watch callback then takes care
		// of continuing the state changes to state_playing.
		if (!set_gstreamer_state_nolock(GST_STATE_PAUSED))
		{
			// GStreamer state change failed. This is considered a nonrecoverable
			// error in GStreamer, since it leaves the pipeline in an undefined
			// state. Reinitialize the pipeline in that case, and report failure.
			// Do *not* try to replay the media again, since the state change
			// failure might be caused by the very media.
			NXPLAY_LOG_MSG(error, "could not switch GStreamer pipeline to PAUSED ; reinitializing pipeline");
			reinitialize_pipeline_nolock();
			return false;
		}
	}
	else
	{
		NXPLAY_LOG_MSG(debug, "queuing media with URI " << p_media.get_uri() << " as next media with token " << p_token);
		// Discard any previously set next media
		m_next_stream.reset();
		if (is_valid(p_media))
		{
			// Create stream for the new next media
			m_next_stream = setup_stream_nolock(p_token, std::move(p_media), p_properties);
			// And sync states with parent, since the new stream
			// is now assigned to m_current_stream
			m_next_stream->sync_states();
			// Don't set a buffering timeout for the next stream. Let it instead
			// buffer until its maximum number of bytes are reached. Since the
			// next stream won't be playing until the current one is done, it is
			// OK to let it buffer even if does so for a long time. If this next
			// stream becomes the current one, a buffering timeout *will* be set.
			m_next_stream->enable_buffering_timeout(false);
		}
		else
		{
			NXPLAY_LOG_MSG(error, "cannot schedule invalid media as next one");
			return false;
		}
	}

	return true;
}


void main_pipeline::set_paused_nolock(bool const p_paused)
{
	// Do nothing if either one of these hold true:
	// 1. Pipeline is not present
	// 2. State is idle
	// 3. p_paused is true and pipeline is already paused
	// 4. p_paused is false and pipeline is already playing
	// 5. No current stream is present
	// 6. Current stream is live or live status is not known

	if ((m_pipeline_elem == nullptr) ||
	    (m_state == state_idle) ||
	    (p_paused && (m_current_gstreamer_state == GST_STATE_PAUSED)) ||
	    (!p_paused && (m_pending_gstreamer_state == GST_STATE_PLAYING)) ||
	    (m_current_stream == nullptr)
	)
		return;

	if (m_current_stream->is_live())
	{
		// This case might be less obvious, so log it
		// Live pipelines cannot be paused
		NXPLAY_LOG_MSG(info, "current stream is live, cannot pause");
		return;
	}

	if (!(m_current_stream->is_live_status_known()))
	{
		// To be on the safe side, don't pause if the live
		// status isn't known, in case it later turns out
		// to be live
		NXPLAY_LOG_MSG(info, "current stream's live status is not known yet, cannot pause");
		return;
	}

	// Pipeline is transitioning; postpone the call
	if (is_transitioning_nolock())
	{
		NXPLAY_LOG_MSG(info, "pipeline currently transitioning -> postponing pause task");
		m_postponed_task.m_type = postponed_task::type_pause;
		m_postponed_task.m_paused = p_paused;
		return;
	}

	set_gstreamer_state_nolock(p_paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
}


void main_pipeline::set_current_position_nolock(gint64 const p_new_position, position_units const p_unit)
{
	if ((m_pipeline_elem == nullptr) || (m_state == state_idle) || (m_current_stream == nullptr))
		return;

	if (!(m_current_stream->is_seekable()))
	{
		NXPLAY_LOG_MSG(info, "current stream is not seekable, cannot seek");
		return;
	}

	// Pipeline is transitioning; postpone the call
	if (is_transitioning_nolock())
	{
		NXPLAY_LOG_MSG(info, "streamer currently transitioning -> postponing set_current_position call");
		m_postponed_task.m_type = postponed_task::type_set_position;
		m_postponed_task.m_position = p_new_position;
		m_postponed_task.m_position_format = p_unit;
		return;
	}

	// Only actually seek if the current state is paused or playing
	if ((m_state != state_paused) && (m_state != state_playing))
		return;

	NXPLAY_LOG_MSG(debug, "set_current_position() called, unit " << pos_unit_description(p_unit) << "; switching to seeking state");

	// save current paused status, since it needs to be
	// temporarily modified for seeking
	m_seeking_data.m_was_paused = (m_state == state_paused);
	m_seeking_data.m_seek_to_position = p_new_position;
	m_seeking_data.m_seek_format = pos_unit_to_format(p_unit);

	set_state_nolock(state_seeking);

	if (m_seeking_data.m_was_paused)
	{
		// If the pipeline is already paused, then seeking can be
		// finished right now. This also sets the state back to
		// paused or playing, depending on what it was before.
		finish_seeking_nolock(true);
	}
	else
	{
		// Need to switch to PAUSED before actually seeking.
		// The bus watch will see the state change, also see that
		// the pipeline state is state_seeking. This will cause it
		// to call finish_seeking_nolock().
		set_gstreamer_state_nolock(GST_STATE_PAUSED);
	}
}


void main_pipeline::stop_nolock()
{
	if ((m_pipeline_elem == nullptr) || (m_state == state_stopping) || (m_state == state_idle))
		return;

	if (is_transitioning_nolock())
	{
		// Pipeline is transitioning; postpone the call
		m_postponed_task.m_type = postponed_task::type_stop;
		m_postponed_task.m_media = media();
	}
	else
	{
		// We can stop right now
		// Stopping means the pipeline is torn down
		shutdown_pipeline_nolock(true);
	}
}


gint64 main_pipeline::query_duration_nolock(position_units const p_unit) const
{	
	if ((m_pipeline_elem == nullptr) || (m_state == state_idle))
		return -1;

	gint64 duration;
	gboolean success = gst_element_query_duration(GST_ELEMENT(m_pipeline_elem), pos_unit_to_format(p_unit), &duration);
	return success ? duration : gint64(-1);
}


void main_pipeline::update_durations_nolock()
{
	// Always check durations in both bytes and nanoseconds, but only
	// notify if the duration actually changed (or if an update is forced
	// by m_force_next_duration_update)

	gint64 new_duration_in_nanoseconds = query_duration_nolock(position_unit_nanoseconds);
	gint64 new_duration_in_bytes       = query_duration_nolock(position_unit_bytes);

	bool duration_in_nanoseconds_updated = m_force_next_duration_update || (new_duration_in_nanoseconds != m_duration_in_nanoseconds);
	bool duration_in_bytes_updated       = m_force_next_duration_update || (new_duration_in_bytes != m_duration_in_bytes);

	NXPLAY_LOG_MSG(
		debug,
		"duration updated: " <<
		"  timestamp: " << yesno(duration_in_nanoseconds_updated) <<
		"  bytes: " << yesno(duration_in_bytes_updated) <<
		"    current durations: " <<
		"  timestamp: " << new_duration_in_nanoseconds <<
		"  bytes: " << new_duration_in_bytes
	);

	/* do duration updates if there is a current stream and a callback */
	if (m_callbacks.m_duration_updated_callback && (m_current_stream != nullptr))
	{
		if (duration_in_nanoseconds_updated)
		{
			m_duration_in_nanoseconds = new_duration_in_nanoseconds;
			m_callbacks.m_duration_updated_callback(m_current_stream->get_media(), m_current_stream->get_token(), new_duration_in_nanoseconds, position_unit_nanoseconds);
		}

		if (duration_in_bytes_updated)
		{
			m_duration_in_bytes = new_duration_in_bytes;
			m_callbacks.m_duration_updated_callback(m_current_stream->get_media(), m_current_stream->get_token(), new_duration_in_bytes, position_unit_bytes);
		}
	}

	// Forced updates are supposed to be a one-shot action; reset the flag
	m_force_next_duration_update = false;
}


bool main_pipeline::finish_seeking_nolock(bool const p_set_state_after_seeking)
{
	// This finishes the seeking process that was started by a
	// set_current_position() call.

	bool ret = true;

	// Perform the actual seek
	bool succeeded = gst_element_seek(
		GST_ELEMENT(m_pipeline_elem),
		1.0,
		m_seeking_data.m_seek_format,
		GstSeekFlags(GST_SEEK_FLAG_FLUSH),
		GST_SEEK_TYPE_SET, m_seeking_data.m_seek_to_position,
		GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

	if (!succeeded)
	{
		ret = false;
		NXPLAY_LOG_MSG(error, "seeking failed");

		// Do not exit early; if p_set_state_after_seeking is true,
		// then the pipeline needs to be switched back to the previous state,
		// otherwise the pipeline remains stuck in the seeking state forever
	}

	m_seeking_data.m_seek_to_position = GST_CLOCK_TIME_NONE;

	if (p_set_state_after_seeking)
	{
		if (m_seeking_data.m_was_paused)
		{
			// If the pipeline was already paused when the seeking
			// process started, then the GStreamer state does not
			// have to be changed, since it is already PAUSED
			NXPLAY_LOG_MSG(debug, "seeking finished; switching back to state paused");
			set_state_nolock(state_paused);

			// Handle any tasks that were postponed during the seeking state
			handle_postponed_task_nolock();
		}
		else
		{
			// The pipeline wasn't in the paused state when seeking started

			if (m_current_stream && m_current_stream->performs_buffering())
			{
				// Seek was performed on a stream that buffers. This means
				// that the internal stream queue is empty now, and will
				// need to be refilled. Therefore it makes little sense to
				// switch to PLAYING just yet. This would only cause a little
				// bit of playback, followed by silence when the BUFFERING
				// message is received and the code detects that it should
				// be in the buffering state. So, instead, let's just switch
				// to the buffering state here directly, avoiding this brief
				// bit of playback.

				NXPLAY_LOG_MSG(debug, "seeking finished; switching to buffering state");
				m_current_stream->set_buffering(true);
				set_state_nolock(state_buffering);
			}
			else
			{
				// Seek was performed on a stream that doesn't buffer.
				// The pipeline can be switched to the PLAYING GStreamer state now.
				// (The bus watch will handle the pipeline state_seeking -> state_playing
				// state change once it happens, which is cleaner than switching here
				// directly.)

				NXPLAY_LOG_MSG(debug, "seeking finished; switching to PLAYING now");
				set_gstreamer_state_nolock(GST_STATE_PLAYING);
			}
		}
	}

	return ret;
}


void main_pipeline::make_next_stream_current_nolock()
{
	// Lock the stream mutex to ensure this function does not
	// collide with static_stream_eos_probe
	std::unique_lock < std::mutex > lock(m_stream_mutex);

	if (!m_stream_eos_seen)
		return;

	// If m_stream_eos_seen is true, then the stream EOS probe has
	// seen an EOS coming from the concat's srcpad, which means the
	// current stream has ended. Therefore, promote the next stream
	// to become the current one, since concat is playing this one
	// now. (And set m_next_stream to null, since there is no next
	// stream anymore unless the user later calls play_media() with
	// p_play_now set to false.)
	m_current_stream.reset();
	m_current_stream = m_next_stream;
	m_next_stream.reset();

	// m_current_stream and m_next_stream are updated and in
	// sync with the situation over at the concat element now
	m_stream_eos_seen = false;
}


void main_pipeline::recheck_buffering_state_nolock()
{
	assert(m_current_stream);

	// Recheck if the buffering states changed, and if so, update
	// the relevant states and values. If however the current media
	// is a live stream, do not pause (live sources must not pause).
	// Also, if the live status isn't known, don't pause, in case
	// the media later turns out to be live.

	if (m_current_stream->is_buffering() && !(m_current_stream->is_live()) && m_current_stream->is_live_status_known() && (m_state == state_playing))
	{
		// The current stream is now buffering, but the pipeline is playing.
		// Switch pipeline state to buffering and the GStreamer state to
		// PAUSED. The former is useful for UIs to indicate buffering.
		// The latter is needed to give the current stream time to fill its
		// buffer(s).

		NXPLAY_LOG_MSG(debug, "current stream's buffering flag enabled; switching to PAUSED and setting pipeline state to buffering");
		set_state_nolock(state_buffering);
		set_gstreamer_state_nolock(GST_STATE_PAUSED);
	}
	else if (!(m_current_stream->is_buffering()) && (m_state == state_buffering))
	{
		// The current stream is no longer buffering, but the pipeline is
		// still in the buffering state. The GStreamer state is PAUSED
		// when this place is reached. Switch the GStreamer state back to
		// PLAYING. The code in the bus watch callback will then see
		// that the current pipeline state is state_buffering and the new
		// GStreamer state is PLAYING, and as result, switch the pipeline
		// state to state_playing.

		NXPLAY_LOG_MSG(debug, "current stream's buffering flag disabled; switching back to PLAYING");
		set_gstreamer_state_nolock(GST_STATE_PLAYING);
	}
}


void main_pipeline::create_dot_pipeline_dump_nolock(std::string const &p_extra_name)
{
	std::string filename = std::string("mainpipeline-") + get_state_name(m_state) + "_" + p_extra_name;
	GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline_elem), GST_DEBUG_GRAPH_SHOW_ALL, filename.c_str());
}



gboolean main_pipeline::static_timeout_cb(gpointer p_data)
{
	// The timeout callback is called by the main_pipeline's internal GLib mainloop.

	main_pipeline *self = static_cast < main_pipeline* > (p_data);

	// Lock is *not* held when this callback is invoked, since the
	// GLib mainloop is what calls it (and the lock is not held
	// for the entire lifetime of the loop)
	std::unique_lock < std::mutex > lock(self->m_loop_mutex);

	// Every time the timeout callback runs, make sure the m_current_stream
	// and m_next_stream values are up to date first. Since the loop mutex
	// is locked at this point, there is no danger of API functions
	// accessing the current stream at the same time.
	self->make_next_stream_current_nolock();

	// Pass on any postponed tags now to the m_new_tags_callback
	// (if there are any)
	if (self->m_callbacks.m_new_tags_callback && (self->m_current_stream != nullptr) && !(self->m_postponed_tags_list.is_empty()))
		self->m_callbacks.m_new_tags_callback(self->m_current_stream->get_media(), self->m_current_stream->get_token(), std::move(self->m_postponed_tags_list));
	// Reset the postponed tasks even if no callback is set,
	// to make sure this list does not accumulate and grow
	self->m_postponed_tags_list = tag_list();

	// Do updates if there is a pipeline, pipeline is playing, there is a current
	// stream, and there is a position_updated, buffer_level, or media_about_to_end callback.
	if ((self->m_pipeline_elem != nullptr) && (self->m_state == state_playing) && (self->m_current_stream != nullptr) && (self->m_callbacks.m_position_updated_callback || self->m_callbacks.m_buffer_level_callback || self->m_callbacks.m_media_about_to_end_callback))
	{
		if (self->m_callbacks.m_buffer_level_callback)
		{
			auto cur_level = self->m_current_stream->get_current_buffer_level();
			if (cur_level)
			{
				self->m_callbacks.m_buffer_level_callback(
					self->m_current_stream->get_media(),
					self->m_current_stream->get_token(),
					*cur_level,
					self->m_current_stream->get_effective_buffer_size_limit()
				);
			}
		}

		if (self->m_callbacks.m_position_updated_callback || self->m_callbacks.m_media_about_to_end_callback)
		{
			// TODO: also do BYTES queries?
			gint64 position;
			if (gst_element_query_position(GST_ELEMENT(self->m_pipeline_elem), GST_FORMAT_TIME, &position))
			{
				// Notify about the new position if the callback is set
				if (self->m_callbacks.m_position_updated_callback)
					self->m_callbacks.m_position_updated_callback(self->m_current_stream->get_media(), self->m_current_stream->get_token(), position, position_unit_nanoseconds);

				// If the current position is close enough to the duration,
				// and if a media_about_to_end callback is set, and if the callback
				// hasn't been called before for this media, notify
				if (!(self->m_block_abouttoend_notifications) &&
				    self->m_callbacks.m_media_about_to_end_callback &&
				    self->m_current_stream && (self->m_duration_in_nanoseconds != -1) &&
				    (GST_CLOCK_DIFF(position, self->m_duration_in_nanoseconds) < gint64(self->m_needs_next_media_time))
				)
				{
					// Raise the block flag to make sure the callback isn't called repeatedly
					self->m_block_abouttoend_notifications = true;
					self->m_callbacks.m_media_about_to_end_callback(self->m_current_stream->get_media(), self->m_current_stream->get_token());
				}
			}
			else
				NXPLAY_LOG_MSG(info, "could not query position");
		}
	}

	return G_SOURCE_CONTINUE;
}


void main_pipeline::setup_timeouts_nolock()
{
	// Catch redundant calls
	if (m_timeout_source != nullptr)
		return;

	NXPLAY_LOG_MSG(debug, "setting up timeout");

	// The periodic updates described in the main_pipeline constructor
	// and in the position_updated_callback documentation are implemented
	// by using a GLib timeout source that gets attached to the pipeline's
	// GLib mainloop.
	m_timeout_source = g_timeout_source_new(m_update_interval);
	g_source_set_callback(m_timeout_source, static_timeout_cb, gpointer(this), nullptr);
	g_source_attach(m_timeout_source, m_thread_loop_context);
}


void main_pipeline::shutdown_timeouts_nolock()
{
	// Catch redundant calls
	if (m_timeout_source == nullptr)
		return;

	NXPLAY_LOG_MSG(debug, "shutting down timeout");

	// Cleanup the timeout source that is driving the periodic updates
	// (there is no way to just "deactivate" the source)
	g_source_destroy(m_timeout_source);
	g_source_unref(m_timeout_source);
	m_timeout_source = nullptr;
}


GstBusSyncReply main_pipeline::static_bus_sync_handler(GstBus *, GstMessage *p_msg, gpointer)
{
	switch (GST_MESSAGE_TYPE(p_msg))
	{
		case GST_MESSAGE_ERROR:
		{
			// If the error message came from an object which has
			// the shutdown marker, drop the message, because in
			// this case, the object is part of a stream that is
			// being destroyed. Error messages coming from these
			// objects are pointless and should be ignored.

			GstObject *src = GST_MESSAGE_SRC(p_msg);

			if (is_object_marked_as_shutting_down(G_OBJECT(src)))
			{
				NXPLAY_LOG_MSG(debug, "dropping error message from stream object " << GST_OBJECT_NAME(src) << " that is being shut down");
				return GST_BUS_DROP;
			}

			break;
		}

		default:
			break;
	}

	return GST_BUS_PASS;
}


gboolean main_pipeline::static_bus_watch(GstBus *, GstMessage *p_msg, gpointer p_data)
{
	// The bus watch is called by the main_pipeline's internal GLib mainloop.

	main_pipeline *self = static_cast < main_pipeline* > (p_data);

	std::unique_lock < std::mutex > lock(self->m_loop_mutex);

	// Every time the bus watch runs, make sure the m_current_stream and
	// m_next_stream values are up to date first. Since the loop mutex
	// is locked at this point, there is no danger of API functions
	// accessing the current stream at the same time.
	self->make_next_stream_current_nolock();

	switch (GST_MESSAGE_TYPE(p_msg))
	{
		case GST_MESSAGE_APPLICATION:
		{
			if (gst_message_has_name(p_msg, stream_eos_msg_name))
				NXPLAY_LOG_MSG(trace, "received message from stream EOS probe");

			break;
		}

		case GST_MESSAGE_STREAM_START:
		{
			// This is posted when new playback actually started.

			NXPLAY_LOG_MSG(debug, "stream start reported by " << GST_MESSAGE_SRC_NAME(p_msg));

			// Fresh new media started, so clear this flag, since otherwise,
			// media-about-to-end callback calls would not ever happen for the new media
			self->m_block_abouttoend_notifications = false;

			// Clear aggregated tag list, since it contains
			// stale tags from the previous stream
			self->m_aggregated_tag_list = tag_list();
			// Clear postponed tags, since they belong to the
			// previous stream
			self->m_postponed_tags_list = tag_list();

			if (self->m_current_stream)
			{
				// Update durations. With some media, it is necessary
				// to do this here. Force it in case no further
				// duration updates will ever happen.
				self->m_force_next_duration_update = true;
				self->update_durations_nolock();

				NXPLAY_LOG_MSG(debug, "media with URI " << self->m_current_stream->get_media().get_uri() << " started to play");

				if (self->m_callbacks.m_media_started_callback)
					self->m_callbacks.m_media_started_callback(self->m_current_stream->get_media(), self->m_current_stream->get_token());

				if (!(self->m_current_stream->is_live_status_known()))
					self->m_current_stream->recheck_live_status(true);

				// Enable buffering timeouts and disable buffer blocking, to make
				// sure this new current stream plays properly, and the user does
				// not have to wait too long for playback to start.
				// If a bitrate was estimated earlier, it will take effect now,
				// and adjust the buffering size accordingly.
				self->m_current_stream->enable_buffering_timeout(true);
				self->m_current_stream->block_buffering(false);

				// The new current media might have been buffering back when
				// it was the next media. If so, it may still need to finish
				// buffering. Check for that.
				self->recheck_buffering_state_nolock();
			}
			else
			{
				// Should not happen. However, to be on the safe side, reinitialize the
				// pipeline if it does.
				NXPLAY_LOG_MSG(error, "STREAM_START received, but no current media present");
				self->reinitialize_pipeline_nolock();
			}

			break;
		}

		case GST_MESSAGE_EOS:
		{
			NXPLAY_LOG_MSG(debug, "EOS reported by " << GST_MESSAGE_SRC_NAME(p_msg));

			if (self->m_next_stream && is_valid(self->m_next_stream->get_media()))
			{
				// copy the next media to a temporary first,
				// since play_media() modifies m_next_media
				media m(std::move(self->m_next_stream->get_media()));
				guint64 token = self->m_next_stream->get_token();
				playback_properties properties = self->m_next_stream->get_playback_properties();
				NXPLAY_LOG_MSG(info, "there is next media to play with URI " << m.get_uri());
				self->play_media_nolock(token, std::move(m), true, properties);

				// the play_media_nolock() call will eventually cause a STREAM_START
				// message to be emitted, which in turn will call the media_started
				// callback, so it is not called here directly
			}
			else
			{
				// Nothing more to play. Stop the pipeline and call the end-of-stream
				// callback if set.
				self->stop_nolock();
				if (self->m_callbacks.m_end_of_stream_callback)
					self->m_callbacks.m_end_of_stream_callback();
			}

			break;
		}

		case GST_MESSAGE_STATE_CHANGED:
		{
			GstState old_gstreamer_state, new_gstreamer_state, pending_gstreamer_state;
			gst_message_parse_state_changed(
				p_msg,
				&old_gstreamer_state,
				&new_gstreamer_state,
				&pending_gstreamer_state
			);

			// Only toplevel pipeline state change messages are of interest here
			// (internal element state changes are ignored)
			if (GST_MESSAGE_SRC(p_msg) != GST_OBJECT_CAST(self->m_pipeline_elem))
				break;

			// In rare cases, a NULL pending state has been observed. This seems to
			// be a GStreamer problem. Circumvent by ignoring such state change messages.
			if (pending_gstreamer_state == GST_STATE_NULL)
				break;

			std::string dotfilename =
				std::string("gststatechange_") +
				"old-" + gst_element_state_get_name(old_gstreamer_state) +
				"_new-" + gst_element_state_get_name(new_gstreamer_state) +
				"_pending-" + gst_element_state_get_name(pending_gstreamer_state);

			NXPLAY_LOG_MSG(
				trace,
				"state change reported by " << GST_MESSAGE_SRC_NAME(p_msg) << ":" <<
				"  old: " << gst_element_state_get_name(old_gstreamer_state) <<
				"  new: " << gst_element_state_get_name(new_gstreamer_state) <<
				"  pending: " << gst_element_state_get_name(pending_gstreamer_state)
			);

			self->m_current_gstreamer_state = new_gstreamer_state;

			// Dump the pipeline to a .dot file after the state change
			// (only actually does something if pipeline dumping is enabled)
			self->create_dot_pipeline_dump_nolock(dotfilename);

			// By default, disable the period update timeout source
			bool enable_timeouts = false;

			// State changes happen for many different reasons. This big
			// switch block handles them all.
			switch (self->m_state)
			{
				case state_starting:
				{
					switch (new_gstreamer_state)
					{
						case GST_STATE_PAUSED:
							// Update durations. With some media, the update
							// must happen here.
							self->update_durations_nolock();

							if (self->m_current_stream)
							{
								playback_properties const &pprops = self->m_current_stream->get_playback_properties();

								if ((pprops.m_start_at_position > 0) && self->m_current_stream->is_seekable())
								{
									// Playback properties indicate the initial position should not be the
									// beginning of the media. Seeking is required.

									// Set the seeking parameters to the desired initial position
									self->m_seeking_data.m_was_paused = pprops.m_start_paused;
									self->m_seeking_data.m_seek_to_position = pprops.m_start_at_position;
									self->m_seeking_data.m_seek_format = pos_unit_to_format(pprops.m_start_at_position_unit);

									// We are seeking now
									self->set_state_nolock(state_seeking);

									// Seek to the defined initial position, but do not switch to any
									// state afterwards. Instead, let the code below handle state switches.
									// The idea is that after seeking, the pipeline is in the PAUSED state,
									// so for the rest of the code below, the situation is the same (since
									// this switch-case block was entered precisely because the pipeline
									// switched to the PAUSED state).
									self->finish_seeking_nolock(false);
								}

								if (pprops.m_start_paused)
								{
									// If playback properties indicate that the media should start in
									// the paused state, then we are essentially done. Just mark the
									// state as paused.

									NXPLAY_LOG_MSG(debug, "pipeline reaches the PAUSED state, and current stream is supposed to start in paused state");
									self->set_state_nolock(state_paused);
								}
								else
								{
									// Handle buffering state updates if this is a non-live source
									// If the current stream is not buffering, we can switch to
									// PLAYING immediately; otherwise, remain at PAUSED until
									// buffering is done (handled by the GST_MESSAGE_BUFFERING
									// code below)
									if (!(self->m_current_stream->is_live()) && self->m_current_stream->is_buffering())
									{
										NXPLAY_LOG_MSG(debug, "current stream is still buffering during startup; switching pipeline to buffering state");
										self->set_state_nolock(state_buffering);
									}
									else
									{
										NXPLAY_LOG_MSG(debug, "current stream fully buffered or does not need buffering; continuing to playing state");
										self->set_state_nolock(state_paused);
										self->set_gstreamer_state_nolock(GST_STATE_PLAYING);
									}
								}
							}

							break;

						case GST_STATE_PLAYING:
							enable_timeouts = true;
							self->set_state_nolock(state_playing);
							break;

						default:
							break;
					}

					break;
				}

				case state_seeking:
				{
					switch (new_gstreamer_state)
					{
						case GST_STATE_PAUSED:
							// Finish previously requested seeking after the state change
							// to PAUSED finished
							if ((old_gstreamer_state != GST_STATE_PAUSED) && (self->m_seeking_data.m_seek_to_position != GST_CLOCK_TIME_NONE))
							{
								NXPLAY_LOG_MSG(debug, "finish seeking");
								self->finish_seeking_nolock(true);
							}
							break;

						case GST_STATE_PLAYING:
							// We are playing again. Re-enable timeouts.
							enable_timeouts = true;
							NXPLAY_LOG_MSG(debug, "seeking finished, and switching back to the PLAYING GStreamer state completed; setting pipeline state to playing");
							self->set_state_nolock(state_playing);

							// Handle any tasks that were postponed during seeking
							self->handle_postponed_task_nolock();

							break;

						default:
							break;
					}

					break;
				}

				case state_buffering:
				{
					switch (new_gstreamer_state)
					{
						case GST_STATE_PAUSED:
							// In some cases, the buffering finishes so quickly, it is done even before the
							// initial switch to PAUSED (which is performed when the pipeline is set to the
							// buffering state) finishes. In this case, switch back to playing.
							// It is safe to assume that switching to playing is correct here, since
							// recheck_buffering_state_nolock() does not switch to state_buffering while
							// the pipeline is paused.
							if (self->m_current_stream && !(self->m_current_stream->is_buffering()))
								self->set_gstreamer_state_nolock(GST_STATE_PLAYING);
							break;

						case GST_STATE_PLAYING:
							// We are playing again. Re-enable timeouts.
							enable_timeouts = true;
							NXPLAY_LOG_MSG(debug, "reached PLAYING GStreamer state after buffering finished; switching back to playing state");

							self->set_state_nolock(state_playing);

							// Handle any tasks that were postponed during seeking
							self->handle_postponed_task_nolock();

							break;

						default:
							break;
					}

					break;
				}

				case state_playing:
				case state_paused:
				{
					// If either pipeline is in paused state and the new GStreamer
					// state is PLAYING or vice versa, update the pipeline state.
					// After the update, handle postponed tasks.

					switch (new_gstreamer_state)
					{
						case GST_STATE_PAUSED:
							if (self->m_state != state_paused)
							{
								self->set_state_nolock(state_paused);
								self->handle_postponed_task_nolock();
							}
							break;

						case GST_STATE_PLAYING:
							if (self->m_state != state_playing)
							{
								// We are playing, so re-enable timeouts.
								enable_timeouts = true;
								self->set_state_nolock(state_playing);
								self->handle_postponed_task_nolock();
							}
							break;

						default:
							break;
					}
					break;
				}

				default:
					break;
			}

			// Update the timeout status after handling the state change
			// This makes sure the timeout source performs periodic updates
			// only if appropriate (= if state is playing)
			if (enable_timeouts)
				self->setup_timeouts_nolock();
			else
				self->shutdown_timeouts_nolock();

			break;
		}

		case GST_MESSAGE_TAG:
		{
			if (self->m_callbacks.m_new_tags_callback)
			{
				NXPLAY_LOG_MSG(debug, "new tags reported by " << GST_MESSAGE_SRC_NAME(p_msg));

				GstTagList *raw_tag_list = nullptr;
				gst_message_parse_tag(p_msg, &raw_tag_list);

				tag_list list(raw_tag_list);
				tag_list new_tags = calculate_new_tags(self->m_aggregated_tag_list, list);
				if (!new_tags.is_empty())
				{
					// Add these new tags to the aggregated list
					// to be able to calculate new tags next time
					self->m_aggregated_tag_list.insert(new_tags, GST_TAG_MERGE_REPLACE);

					if (self->m_postpone_all_tags)
					{
						// Just add all of the tags to the list of postponed tags
						self->m_postponed_tags_list.insert(new_tags, GST_TAG_MERGE_REPLACE);
					}
					else
					{
						bool tags_left = false;

						// Go over all tags in the new_tags list, remove those which are
						// in the m_tags_to_always_postpone set, and place these in the
						// m_postponed_tags_list.
						for (gint num = 0; num < gst_tag_list_n_tags(new_tags.get_tag_list()); ++num)
						{
							std::string name(gst_tag_list_nth_tag_name(new_tags.get_tag_list(), num));

							if (self->m_tags_to_always_postpone.find(std::string(name)) == self->m_tags_to_always_postpone.end())
							{
								// This is one tag that is *not* in the set of
								// tags to always remove, meaning at least this
								// tag will remain in new_tags. Therefore, there
								// is something to report to the new_tags_callback.
								// => set tags_left to true.
								tags_left = true;
								continue;
							}

							// At this point, it is clear that this tag is one of the
							// ones included in m_tags_to_always_postpone. Therefore,
							// this tag must be postponed.

							// If this value is already present in m_postponed_tags_list,
							// remove it first.
							if (has_value(self->m_postponed_tags_list, name))
								gst_tag_list_remove_tag(self->m_postponed_tags_list.get_tag_list(), name.c_str());

							// Append each value for this tag to the m_postponed_tags_list
							for (guint index = 0; index < get_num_values_for_tag(new_tags, name); ++index)
							{
								GValue const *value = get_raw_value(new_tags, name, index);
								add_raw_value(self->m_postponed_tags_list, value, name, GST_TAG_MERGE_APPEND);
							}

							// Finally, remove this tag from new_tags
							gst_tag_list_remove_tag(new_tags.get_tag_list(), name.c_str());
						}

						// Report new_tags to the callback unless the loop above
						// removed all values
						if (tags_left)
							self->m_callbacks.m_new_tags_callback(self->m_current_stream->get_media(), self->m_current_stream->get_token(), std::move(new_tags));
					}
				}
			}

			break;
		}

		case GST_MESSAGE_INFO:
		{
			std::string text = message_to_str(p_msg, GST_LEVEL_INFO);
			if (self->m_callbacks.m_info_callback)
				self->m_callbacks.m_info_callback(text);
			break;
		}

		case GST_MESSAGE_WARNING:
		{
			std::string text = message_to_str(p_msg, GST_LEVEL_WARNING);
			if (self->m_callbacks.m_warning_callback)
				self->m_callbacks.m_warning_callback(text);
			break;
		}

		case GST_MESSAGE_ERROR:
		{
			std::string text = message_to_str(p_msg, GST_LEVEL_ERROR);
			if (self->m_callbacks.m_error_callback)
				self->m_callbacks.m_error_callback(text);

			// Error messages indicate a nonrecoverable error. Create a
			// dot pipeline dump for debugging, and reinitialize the
			// pipeline to deal with this situation.
			self->create_dot_pipeline_dump_nolock("error");
			self->reinitialize_pipeline_nolock();

			break;
		}

		case GST_MESSAGE_BUFFERING:
		{
			gint percent;
			GstObject *source;
			gst_message_parse_buffering(p_msg, &percent);
			source = GST_MESSAGE_SRC(p_msg);

			NXPLAY_LOG_MSG(debug, "buffering reported by " << GST_MESSAGE_SRC_NAME(p_msg) << " at " << percent << "%");

			stream *stream_ = nullptr;
			bool is_current;

			// Check if either the next or the current stream posted the buffering message

			if (self->m_current_stream && self->m_current_stream->contains_object(source))
			{
				stream_ = self->m_current_stream.get();
				is_current = true;
			}
			else if (self->m_next_stream && self->m_next_stream->contains_object(source))
			{
				stream_ = self->m_next_stream.get();
				is_current = false;
			}

			if (stream_ != nullptr)
			{
				char const *label = is_current ? "current" : "next";
				bool changed = false;

				// Use a low/high watermark approach. If the stream isn't buffering,
				// and the buffer fill level falls below 100%, enable buffering.
				// If the stream is buffering, and the fill level reaches 100%
				// (= stream is fully filled), disable buffering.

				if (percent < 100)
				{
					if (!(stream_->is_buffering()))
					{
						NXPLAY_LOG_MSG(debug, label << " stream's buffer fill level is too low; enabling buffering flag");
						stream_->set_buffering(true);
						changed = true;
					}
					else if (is_current && (self->m_state != state_buffering) && (self->m_state != state_starting))
					{
						// This can be reached when seeking in HTTP streams for example.
						//
						// It only makes sense to do anything here if stream_ is the current stream. If the
						// current stream caused the buffering, force a buffer state recheck, because it
						// *should* be in buffering or starting state during buffering, but it's not.
						//
						// (If the next stream caused the buffering message, it won't affect the current
						// state anyway, so it is perfectly OK if the state is not "buffering" or "starting".)

						NXPLAY_LOG_MSG(debug, label << " stream's buffering flag enabled, but not in buffering state (instead, state is " << get_state_name(self->m_state) << "); checking");
						changed = true;
					}
				}
				else
				{
					if (stream_->is_buffering())
					{
						NXPLAY_LOG_MSG(debug, label << " stream's buffer fill level is enough; disabling buffering flag");
						stream_->set_buffering(false);
						changed = true;
					}
				}

				// If the current stream is the one that posted the message,
				// and if the buffering flag actually changed, recheck the buffering situation
				if (is_current && changed)
				{
					if (self->m_next_stream)
					{
						// In here, stream_ is the current stream, and a next stream exists.
						// If the current stream is buffering, then block the next stream's buffering.
						// This gives priority to the current stream's buffering, which needs to finish
						// as soon as possible, because it interrupts the audible playback. Blocking
						// the next stream's buffering, however, does not interrupt anything audible.

						if (stream_->is_buffering())
							NXPLAY_LOG_MSG(debug, "current stream needs to buffer; block buffering in the next stream");
						else
							NXPLAY_LOG_MSG(debug, "current stream no longer needs to buffer; unblock buffering in the next stream");

						self->m_next_stream->block_buffering(stream_->is_buffering());
					}

					self->recheck_buffering_state_nolock();
				}

				// Notify about buffering
				if (self->m_callbacks.m_buffering_updated_callback)
				{
					self->m_callbacks.m_buffering_updated_callback(
						stream_->get_media(),
						stream_->get_token(),
						is_current,
						percent,
						stream_->get_current_buffer_level(),
						stream_->get_effective_buffer_size_limit()
					);
				}
			}

			break;
		}

		case GST_MESSAGE_DURATION_CHANGED:
		{
			// Ignore duration messages coming from the concat element, since these
			// otherwise cause the UI to show the next song's duration ~0.5 seconds
			// before it actually starts playing
			if (GST_MESSAGE_SRC(p_msg) == GST_OBJECT(self->m_concat_elem))
				break;

			NXPLAY_LOG_MSG(debug, "duration update reported by " << GST_MESSAGE_SRC_NAME(p_msg));
			self->update_durations_nolock();

			break;
		}

		case GST_MESSAGE_LATENCY:
		{
			NXPLAY_LOG_MSG(debug, "redistributing latency; requested by " << GST_MESSAGE_SRC_NAME(p_msg));
			gst_bin_recalculate_latency(GST_BIN(self->m_pipeline_elem));
			break;
		}

		case GST_MESSAGE_REQUEST_STATE:
		{
			// Switch to the new requested GStreamer state unless the pipeline is
			// transitioning. If it is, postpone the state switch.

			GstState requested_state;
			gst_message_parse_request_state(p_msg, &requested_state);
			NXPLAY_LOG_MSG(debug, "state change to state " << gst_element_state_get_name(requested_state) << " requested by " << GST_MESSAGE_SRC_NAME(p_msg));
			if (self->is_transitioning_nolock())
			{
				NXPLAY_LOG_MSG(debug, "postponing state change since pipeline is currently transitioning");
				self->m_postponed_task.m_type = postponed_task::type_set_state;
				self->m_postponed_task.m_gstreamer_state = requested_state;
			}
			else
				gst_element_set_state(self->m_pipeline_elem, requested_state);

			break;
		}

		default:
			break;
	}

	return TRUE;
}


void main_pipeline::thread_main()
{
	std::unique_lock < std::mutex > lock(m_loop_mutex);

	// Setup an explciit loop context. This is necessary to avoid collisions
	// with any other "default" context that may be present in the application 
	// for example.

	m_thread_loop_context = g_main_context_new();
	g_main_context_push_thread_default(m_thread_loop_context);

	// Create the actual GLib mainloop
	m_thread_loop = g_main_loop_new(m_thread_loop_context, FALSE);

	// Set up an idle source that is triggered as soon as the mainloop actually
	// starts. This is needed in the start_thread() function to wait until
	// the mainloop is running.
	GSource *idle_source = g_idle_source_new();
	g_source_set_callback(idle_source, (GSourceFunc)static_loop_start_cb, this, nullptr);
	g_source_attach(idle_source, m_thread_loop_context);
	g_source_unref(idle_source);

	// Unlock the mutex and run the mainloop
	// Mutex is locked in the bus watch, timeout source etc. whenever it is needed
	lock.unlock();
	g_main_loop_run(m_thread_loop);

	// Mutex is re-locked to make sure the thread cleanup does not cause race conditions
	lock.lock();

	// Cleanup the mainloop
	g_main_loop_unref(m_thread_loop);
	m_thread_loop = nullptr;

	// Cleanup the context
	g_main_context_pop_thread_default(m_thread_loop_context);
	g_main_context_unref(m_thread_loop_context);
	m_thread_loop_context = nullptr;
}


void main_pipeline::start_thread()
{
	{
		std::unique_lock < std::mutex > lock(m_loop_mutex);

		if (m_thread_loop != nullptr)
			return;

		m_thread_loop_running = false;
		m_thread = std::thread(&main_pipeline::thread_main, this);

		// Wait until the main loop actually started
		// (This is signaled by the idle source, which calls the
		// static_loop_start_cb callback)
		while (!m_thread_loop_running)
			m_condition.wait(lock);

		NXPLAY_LOG_MSG(debug, "thread started");
	}
}


void main_pipeline::stop_thread()
{
	{
		std::unique_lock < std::mutex > lock(m_loop_mutex);

		if (m_thread_loop != nullptr)
			g_main_loop_quit(m_thread_loop);
	}

	m_thread.join();

	m_thread_loop_running = false;

	NXPLAY_LOG_MSG(debug, "thread stopped");
}


gboolean main_pipeline::static_loop_start_cb(gpointer p_data)
{
	// When this place is reached, the mainloop actually started

	main_pipeline *self = static_cast < main_pipeline* > (p_data);

	{
		std::unique_lock < std::mutex > lock(self->m_loop_mutex);
		NXPLAY_LOG_MSG(debug, "mainloop started - can signal that thread is started");
		self->m_thread_loop_running = true;
		self->m_condition.notify_all();
	}

	// The idle source only needs to run once; let GLib remove
	// it after this call
	return G_SOURCE_REMOVE;
}


} // namespace nxplay end
