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


namespace nxplay
{


namespace
{


static char const *cleanup_old_streams_msg_name = "cleanup-old-streams";


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


// Utility function to unref a GstObject if the pointer is non-null
template < typename T >
void checked_unref(T* &p_object)
{
	if (p_object != nullptr)
	{
		gst_object_unref(GST_OBJECT(p_object));
		p_object = nullptr;
	
	}
}


} // unnamed namespace end



main_pipeline::stream::stream(main_pipeline &p_pipeline, guint64 const p_token, media &&p_media, GstBin *p_container_bin, GstElement *p_concat_elem)
	: m_pipeline(p_pipeline)
	, m_token(p_token)
	, m_media(std::move(p_media))
	, m_uridecodebin_elem(nullptr)
	, m_identity_elem(nullptr)
	, m_concat_elem(p_concat_elem)
	, m_identity_srcpad(nullptr)
	, m_concat_sinkpad(nullptr)
	, m_container_bin(p_container_bin)
	, m_is_buffering(false)
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

	// "async-handling" has to be set to TRUE to ensure no internal async state changes
	// "escape" from the uridecobin and affect the rest of the pipeline (otherwise, the
	// pipeline may be set to PAUSED state, which affects gapless playback)
	g_object_set(G_OBJECT(m_uridecodebin_elem), "uri", m_media.get_uri().c_str(), "async-handling", gboolean(TRUE), NULL);
	g_signal_connect(G_OBJECT(m_uridecodebin_elem), "pad-added", G_CALLBACK(static_new_pad_callback), gpointer(this));

	// Now set the element states to the state of the bin
	gst_element_sync_state_with_parent(m_identity_elem);
	gst_element_sync_state_with_parent(m_uridecodebin_elem);

	// All done; scope guard can be disengaged
	elems_guard.unguard();
}


main_pipeline::stream::~stream()
{
	if (m_container_bin == nullptr)
		return;

	NXPLAY_LOG_MSG(debug, "destroying stream " << guintptr(this));

	// The states of the elements are locked before the element states
	// are set to NULL. This prevents race conditions where the pipeline is
	// switching to another state (for example, PLAYING) while this stream
	// is being destroyed. If the state weren't locked, the pipeline's switch
	// would propagate to the element.

	gst_element_set_locked_state(m_identity_elem, TRUE);
	gst_element_set_locked_state(m_uridecodebin_elem, TRUE);

	gst_element_set_state(m_identity_elem, GST_STATE_NULL);
	gst_element_set_state(m_uridecodebin_elem, GST_STATE_NULL);

	// Unlink identity and concat
	if (m_concat_sinkpad != nullptr)
	{
		gst_pad_unlink(m_identity_srcpad, m_concat_sinkpad);
		gst_object_unref(GST_OBJECT(m_identity_srcpad));
		gst_element_release_request_pad(m_concat_elem, m_concat_sinkpad);
		gst_object_unref(GST_OBJECT(m_concat_sinkpad));
	}

	// Unlink uridecodebin and identity
	gst_element_unlink(m_uridecodebin_elem, m_identity_elem);

	// Finally, remove the elements from the pipeline
	// No need to unref these elements, since gst_bin_remove() does it automatically
	gst_bin_remove_many(m_container_bin, m_uridecodebin_elem, m_identity_elem, NULL);

	NXPLAY_LOG_MSG(debug, "stream " << guintptr(this) << " destroyed");
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


bool main_pipeline::stream::contains_object(GstObject *p_object)
{
	return gst_object_has_as_ancestor(p_object, GST_OBJECT(m_uridecodebin_elem));
}


void main_pipeline::stream::set_buffering(bool const p_flag)
{
	m_is_buffering = p_flag;
}


bool main_pipeline::stream::is_buffering() const
{
	return m_is_buffering;
}


void main_pipeline::stream::static_new_pad_callback(GstElement *, GstPad *p_pad, gpointer p_data)
{
	stream *self = static_cast < stream* > (p_data);

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

	NXPLAY_LOG_MSG(debug, "decodebin pad linked, stream: " << guintptr(self));
}



main_pipeline::main_pipeline(callbacks const &p_callbacks, GstClockTime const p_needs_next_media_time, guint const p_update_interval, bool const p_update_tags_in_interval)
	: m_state(state_idle)
	, m_duration_in_nanoseconds(-1)
	, m_duration_in_bytes(-1)
	, m_update_tags_in_interval(p_update_tags_in_interval)
	, m_block_abouttoend_notifications(false)
	, m_timeout_source(nullptr)
	, m_needs_next_media_time(p_needs_next_media_time)
	, m_update_interval(p_update_interval)
	, m_current_gstreamer_state(GST_STATE_NULL)
	, m_pending_gstreamer_state(GST_STATE_VOID_PENDING)
	, m_pipeline_elem(nullptr)
	, m_concat_elem(nullptr)
	, m_volume_elem(nullptr)
	, m_audiosink_elem(nullptr)
	, m_volume_iface(nullptr)
	, m_bus(nullptr)
	, m_watch_source(nullptr)
	, m_next_token(0)
	, m_thread_loop(nullptr)
	, m_thread_loop_context(nullptr)
	, m_callbacks(p_callbacks)
{
	// Start the GLib mainloop thread
	start_thread();
}


main_pipeline::~main_pipeline()
{
	// Stop playback immediately and cancel transitioning states
	// by shutting down the pipeline right now
	{
		std::unique_lock < std::mutex > lock(m_mutex);
		shutdown_pipeline_nolock();
	}

	// Now stop the thread with the GLib mainloop
	stop_thread();
}


bool main_pipeline::play_media_impl(guint64 const p_token, media &&p_media, bool const p_play_now)
{
	std::unique_lock < std::mutex > lock(m_mutex);
	return play_media_nolock(p_token, std::move(p_media), p_play_now);
}


guint64 main_pipeline::get_new_token()
{
	std::unique_lock < std::mutex > lock(m_mutex);
	return m_next_token++; // Generate unique tokens by using a monotonically increasing counter
}


void main_pipeline::stop()
{
	std::unique_lock < std::mutex > lock(m_mutex);
	stop_nolock();
}


void main_pipeline::set_paused(bool const p_paused)
{
	std::unique_lock < std::mutex > lock(m_mutex);
	set_paused_nolock(p_paused);
}


bool main_pipeline::is_paused() const
{
	std::unique_lock < std::mutex > lock(m_mutex);
	return m_state == state_paused;
}


bool main_pipeline::is_transitioning() const
{
	std::unique_lock < std::mutex > lock(m_mutex);
	return is_transitioning_nolock();
}


states main_pipeline::get_current_state() const
{
	std::unique_lock < std::mutex > lock(m_mutex);
	return m_state;
}


void main_pipeline::set_current_position(gint64 const p_new_position, position_units const p_unit)
{
	std::unique_lock < std::mutex > lock(m_mutex);
	set_current_position_nolock(p_new_position, p_unit);
}


gint64 main_pipeline::get_current_position(position_units const p_unit) const
{
	std::unique_lock < std::mutex > lock(m_mutex);

	if ((m_pipeline_elem == nullptr) || (m_state == state_idle))
		return -1;

        gint64 position;
        gboolean success = gst_element_query_position(GST_ELEMENT(m_pipeline_elem), pos_unit_to_format(p_unit), &position);
        return success ? position : -1;
}


gint64 main_pipeline::get_duration(position_units const p_unit) const
{
	std::unique_lock < std::mutex > lock(m_mutex);

	switch (p_unit)
	{
		case position_unit_nanoseconds: return m_duration_in_nanoseconds;
		case position_unit_bytes:     return m_duration_in_bytes;
		default: assert(0);
	}

	// not supposed to be reached due to the assert above; just shuts up the compiler
	return -1;
}


void main_pipeline::set_volume(double const p_new_volume, GstStreamVolumeFormat const p_format)
{
	std::unique_lock < std::mutex > lock(m_mutex);
	if (m_volume_iface != nullptr)
		gst_stream_volume_set_volume(m_volume_iface, p_format, p_new_volume);
}


double main_pipeline::get_volume(GstStreamVolumeFormat const p_format) const
{
	std::unique_lock < std::mutex > lock(m_mutex);
	if (m_volume_iface != nullptr)
		return gst_stream_volume_get_volume(m_volume_iface, p_format);
	else
		return 1.0;
}


void main_pipeline::set_muted(bool const p_mute)
{
	std::unique_lock < std::mutex > lock(m_mutex);
	if (m_volume_iface != nullptr)
		gst_stream_volume_set_mute(m_volume_iface, p_mute ? TRUE : FALSE);
}


bool main_pipeline::is_muted() const
{
	std::unique_lock < std::mutex > lock(m_mutex);
	if (m_volume_iface != nullptr)
		return gst_stream_volume_get_mute(m_volume_iface);
	else
		return false;
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
			play_media_nolock(m_postponed_task.m_token, std::move(m_postponed_task.m_media), true);
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


main_pipeline::stream_sptr main_pipeline::setup_stream_nolock(guint64 const p_token, media &&p_media)
{
	stream_sptr new_stream(new stream(
		*this,
		p_token,
		std::move(p_media),
		GST_BIN(m_pipeline_elem),
		m_concat_elem
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


void main_pipeline::cleanup_old_streams_nolock()
{
	// Clean up all the old streams in the queue
	// See static_stream_eos_probe() and the GST_MESSAGE_APPLICATION
	// code below for details
	while (!(m_old_streams.empty()))
	{
		stream_sptr old_stream = m_old_streams.front();
		m_old_streams.pop();
		old_stream.reset();
	}
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
			std::unique_lock < std::mutex > lock(self->m_mutex);

			NXPLAY_LOG_MSG(debug, "EOS event observed at stream srcpad");

			// An EOS seen by concat means that the concat element just
			// switched its current pad to the next one. This is when
			// the m_current_stream is done, has to be ditched,
			// and m_next_stream needs to start playing.

			// Do not destroy the finished m_current_stream right away.
			// Instead, move it in the queue, and post a message to the
			// mainloop. This makes sure this pad probe isn't potentially
			// blocked for a long time if the stream takes a while to be
			// destroyed.
			if (self->m_current_stream)
				self->m_old_streams.push(self->m_current_stream);

			// Promote the next stream to be the new current one
			self->m_current_stream = self->m_next_stream;
			self->m_next_stream.reset();

			// Notify the bus that there is a new entry in m_old_streams to
			// take care of
			gst_bus_post(
				self->m_bus,
				gst_message_new_application(
					GST_OBJECT(self->m_pipeline_elem),
					gst_structure_new_empty(cleanup_old_streams_msg_name)
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

	GstElement *audioconvert_elem = nullptr;

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
		checked_unref(m_volume_elem);
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

	audioconvert_elem = gst_element_factory_make("audioconvert", "audioconvert");
	if (audioconvert_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audioconvert element");
		return false;
	}

	m_volume_elem = gst_element_factory_make("volume", "volume");
	if (m_volume_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create volume element");
		return false;
	}

	m_audiosink_elem = gst_element_factory_make("autoaudiosink", "audiosink");
	if (m_audiosink_elem == nullptr)
	{
		NXPLAY_LOG_MSG(error, "could not create audiosink element");
		return false;
	}

	gst_bin_add_many(GST_BIN(m_pipeline_elem), m_concat_elem, audioconvert_elem, m_volume_elem, m_audiosink_elem, nullptr);
	// no need to guard the elements anymore, since the pipeline
	// now manages their lifetime
	elems_guard.unguard();

	// Link all of the elements together
	gst_element_link_many(m_concat_elem, audioconvert_elem, m_volume_elem, m_audiosink_elem, nullptr);

	// Setup the pipeline bus
	// NOT done by calliong gst_bus_add_watch(), since we need to explicitely
	// connect the bus watch to the m_thread_loop_context
	m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline_elem));
	m_watch_source = gst_bus_create_watch(m_bus);
	g_source_set_callback(m_watch_source, (GSourceFunc)static_bus_watch, gpointer(this), nullptr);
	g_source_attach(m_watch_source, m_thread_loop_context);

	pipeline_guard.unguard();

	// Announce the idle state
	set_state_nolock(state_idle);

	// find_stream_volume_interface() is not called here, since some elements
	// won't be able to handle volume until they are in the READY state. Therefore,
	// this function is called in the bus watch callback instead.

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

	// Cleanup and unref the pipeline
	gst_bin_remove_many(GST_BIN(m_pipeline_elem), m_concat_elem, m_audiosink_elem, nullptr);
	gst_object_unref(GST_OBJECT(m_pipeline_elem));

	m_pipeline_elem = nullptr;
	m_concat_elem = nullptr;
	m_audiosink_elem = nullptr;
	m_volume_iface = nullptr;

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

	// Set the pipeline to NULL. This is always a synchronous state change.
	GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(m_pipeline_elem), GST_STATE_NULL);
	g_assert(ret != GST_STATE_CHANGE_ASYNC); // If this is ASYNC, something in GStreamer went seriously wrong

	// Discard any current, next, or old streams
	m_current_stream.reset();
	m_next_stream.reset();
	cleanup_old_streams_nolock();

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
}


void main_pipeline::set_state_nolock(states const p_new_state)
{
	states old_state = m_state;
	m_state = p_new_state;
	NXPLAY_LOG_MSG(trace, "state change: old: " << get_state_name(old_state) << " new: " << get_state_name(m_state));
	if (m_callbacks.m_state_changed_callback)
		m_callbacks.m_state_changed_callback(old_state, p_new_state);
}


bool main_pipeline::play_media_nolock(guint64 const p_token, media &&p_media, bool const p_play_now)
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
		m_current_stream = setup_stream_nolock(p_token, std::move(p_media));

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
			m_next_stream = setup_stream_nolock(p_token, std::move(p_media));
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
	if ((m_pipeline_elem == nullptr) || (m_state == state_idle) || (p_paused && (m_current_gstreamer_state == GST_STATE_PAUSED)) || (!p_paused && (m_pending_gstreamer_state == GST_STATE_PLAYING)))
		return;

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
	if ((m_pipeline_elem == nullptr) || (m_state == state_idle))
		return;

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
		// finished right now.
		finish_seeking_nolock();
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
	// notify if the duration actually changed

	gint64 new_duration_in_nanoseconds = query_duration_nolock(position_unit_nanoseconds);
	gint64 new_duration_in_bytes       = query_duration_nolock(position_unit_bytes);

	bool duration_in_nanoseconds_updated = (new_duration_in_nanoseconds != m_duration_in_nanoseconds);
	bool duration_in_bytes_updated       = (new_duration_in_bytes != m_duration_in_bytes);

	NXPLAY_LOG_MSG(
		debug,
		"duration updated: " <<
		"  timestamp: " << yesno(duration_in_nanoseconds_updated) <<
		"  bytes: " << yesno(duration_in_bytes_updated) <<
		"    current durations: " <<
		"  timestamp: " << new_duration_in_nanoseconds <<
		"  bytes: " << new_duration_in_bytes
	);

	if (m_callbacks.m_duration_updated_callback)
	{
		if (duration_in_nanoseconds_updated)
		{
			m_duration_in_nanoseconds = new_duration_in_nanoseconds;
			m_callbacks.m_duration_updated_callback(new_duration_in_nanoseconds, position_unit_nanoseconds);
		}

		if (duration_in_bytes_updated)
		{
			m_duration_in_bytes = new_duration_in_bytes;
			m_callbacks.m_duration_updated_callback(new_duration_in_bytes, position_unit_bytes);
		}
	}
}


void main_pipeline::finish_seeking_nolock()
{
	// This finishes the seeking process that was started by a
	// set_current_position() call.

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
		NXPLAY_LOG_MSG(error, "seeking failed");
		return;
	}

	m_seeking_data.m_seek_to_position = GST_CLOCK_TIME_NONE;

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
		NXPLAY_LOG_MSG(debug, "seeking finished; witching to PLAYING now");
		set_gstreamer_state_nolock(GST_STATE_PLAYING);
		// the bus watch will handle the pipeline state_seeking -> state_playing change
	}
}


void main_pipeline::recheck_buffering_state_nolock()
{
	assert(m_current_stream);

	// Recheck if the buffering states changed, and if so, update
	// the relevant states and values.

	if (m_current_stream->is_buffering() && (m_state == state_playing))
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
	main_pipeline *self = static_cast < main_pipeline* > (p_data);

	// lock is *not* held when this callback is invoked, since the
	// GLib mainloop is what calls it (and the lock is not held
	// for the entire lifetime of the loop)
	std::unique_lock < std::mutex > lock(self->m_mutex);

	// Do updates if there is a pipeline, pipeline is playing, and there is
	// either a position_updated or media_about_to_end callback.
	if ((self->m_pipeline_elem != nullptr) && (self->m_state == state_playing) && (self->m_callbacks.m_position_updated_callback || self->m_callbacks.m_media_about_to_end_callback))
	{
		// TODO: also do BYTES queries?
        	gint64 position;
	        if (gst_element_query_position(GST_ELEMENT(self->m_pipeline_elem), GST_FORMAT_TIME, &position))
		{
			// Notify about the new position if the callback is set
			if (self->m_callbacks.m_position_updated_callback)
				self->m_callbacks.m_position_updated_callback(position, position_unit_nanoseconds);

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



gboolean main_pipeline::static_bus_watch(GstBus *, GstMessage *p_msg, gpointer p_data)
{
	main_pipeline *self = static_cast < main_pipeline* > (p_data);

	switch (GST_MESSAGE_TYPE(p_msg))
	{
		case GST_MESSAGE_APPLICATION:
		{
			if (gst_message_has_name(p_msg, cleanup_old_streams_msg_name))
			{
				// The static_stream_eos_probe pad probe saw an EOS earlier,
				// and pushed the finished stream in the m_old_streams queue.
				// Then, it posted this message to make sure the cleanup
				// happens here, and not in the probe (which must not block
				// for long).

				std::unique_lock < std::mutex > lock(self->m_mutex);
				self->cleanup_old_streams_nolock();
			}

			break;
		}

		case GST_MESSAGE_STREAM_START:
		{
			// This is posted when new playback actually started.
		
			std::unique_lock < std::mutex > lock(self->m_mutex);

			NXPLAY_LOG_MSG(debug, "stream start reported by " << GST_MESSAGE_SRC_NAME(p_msg));

			// Fresh new media started, so clear this flag, since otherwise,
			// media-about-to-end callback calls would not ever happen for the new media
			self->m_block_abouttoend_notifications = false;

			if (self->m_current_stream)
			{
				// Update durations. With some media, it is necessary
				// to do this here.
				self->update_durations_nolock();

				NXPLAY_LOG_MSG(debug, "media with URI " << self->m_current_stream->get_media().get_uri() << " started to play");

				if (self->m_callbacks.m_media_started_callback)
					self->m_callbacks.m_media_started_callback(self->m_current_stream->get_media(), self->m_current_stream->get_token());

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
			std::unique_lock < std::mutex > lock(self->m_mutex);

			NXPLAY_LOG_MSG(debug, "EOS reported by " << GST_MESSAGE_SRC_NAME(p_msg));

			if (self->m_next_stream && is_valid(self->m_next_stream->get_media()))
			{
				// copy the next media to a temporary first,
				// since play_media() modifies m_next_media
				media m(std::move(self->m_next_stream->get_media()));
				guint64 token = self->m_next_stream->get_token();
				NXPLAY_LOG_MSG(info, "there is next media to play with URI " << m.get_uri());
				self->play_media_nolock(token, std::move(m), true);

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
			std::unique_lock < std::mutex > lock(self->m_mutex);

			GstState old_gstreamer_state, new_gstreamer_state, pending_gstreamer_state;
			gst_message_parse_state_changed(
				p_msg,
				&old_gstreamer_state,
				&new_gstreamer_state,
				&pending_gstreamer_state
			);

			// Only toplevel pipeline state change messages are of interest here
			// (internal element state changes are ignored)
			if (GST_MESSAGE_SRC(p_msg) != GST_OBJECT(self->m_pipeline_elem))
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
						case GST_STATE_READY:
							// This is the right place to check for volume interfaces.
							// Some elements might not be able to do this until they are
							// switched to the READY state.
							self->m_volume_iface = self->find_stream_volume_interface();
							break;

						case GST_STATE_PAUSED:
							// Update durations. With some media, the update
							// must happen here.
							self->update_durations_nolock();

							// Handle buffering state updates
							// If the current stream is not buffering, we can switch to
							// PLAYING immediately; otherwise, remain at PAUSED until
							// buffering is done (handled by the GST_MESSAGE_BUFFERING
							// code below)
							if (self->m_current_stream && self->m_current_stream->is_buffering())
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
								self->finish_seeking_nolock();
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

				GstTagList *tag_list_ = nullptr;
				gst_message_parse_tag(p_msg, &tag_list_);

				tag_list list(tag_list_);
				self->m_callbacks.m_new_tags_callback(std::move(list));
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
			std::unique_lock < std::mutex > lock(self->m_mutex);
			self->create_dot_pipeline_dump_nolock("error");
			self->reinitialize_pipeline_nolock();

			break;
		}

		case GST_MESSAGE_BUFFERING:
		{
			std::unique_lock < std::mutex > lock(self->m_mutex);

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
				// and the buffer fill level falls below 20%, enable buffering.
				// If the stream is buffering, and the fill level reaches 100%
				// (= stream is fully filled), disable buffering.

				// TODO: make the low watermark a constructor parameter
				// (100% as high watermark is always required, otherwise GStreamer
				// queues may behave funny)
				if ((percent <= 20) && !(stream_->is_buffering()))
				{
					NXPLAY_LOG_MSG(debug, label << " stream's buffering state too low; enabling buffering flag");
					stream_->set_buffering(true);
					changed = true;
				}
				else if ((percent >= 100) && stream_->is_buffering())
				{
					NXPLAY_LOG_MSG(debug, label << " stream's buffering state high enough; disabling buffering flag");
					stream_->set_buffering(false);
					changed = true;
				}

				// If the current stream is the one that posted the message,
				// and if the buffering flag actually changed, recheck the buffering situation
				if (is_current && changed)
					self->recheck_buffering_state_nolock();

				// Notify about buffering
				if (self->m_callbacks.m_buffering_updated_callback)
					self->m_callbacks.m_buffering_updated_callback(stream_->get_media(), stream_->get_token(), is_current, percent);
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

			std::unique_lock < std::mutex > lock(self->m_mutex);
			NXPLAY_LOG_MSG(debug, "duration update reported by " << GST_MESSAGE_SRC_NAME(p_msg));
			self->update_durations_nolock();

			break;
		}

		case GST_MESSAGE_LATENCY:
		{
			std::unique_lock < std::mutex > lock(self->m_mutex);
			NXPLAY_LOG_MSG(debug, "redistributing latency; requested by " << GST_MESSAGE_SRC_NAME(p_msg));
			gst_bin_recalculate_latency(GST_BIN(self->m_pipeline_elem));
			break;
		}

		case GST_MESSAGE_REQUEST_STATE:
		{
			std::unique_lock < std::mutex > lock(self->m_mutex);

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


GstStreamVolume* main_pipeline::find_stream_volume_interface()
{
	// First, check if the audio sink can handle volume.
	// Then, try using the soft-volume element as fallback.

	GstElement *volume_element = nullptr;

	if ((volume_element == nullptr) && G_TYPE_CHECK_INSTANCE_TYPE(m_audiosink_elem, GST_TYPE_STREAM_VOLUME))
		volume_element = m_audiosink_elem;

	if ((volume_element == nullptr) && GST_IS_BIN(m_audiosink_elem))
		volume_element = gst_bin_get_by_interface(GST_BIN(m_audiosink_elem), GST_TYPE_STREAM_VOLUME);

	if (volume_element == nullptr)
		volume_element = m_volume_elem;

	if (volume_element != nullptr)
	{
		gchar *name = gst_element_get_name(volume_element);
		NXPLAY_LOG_MSG(debug, "element " << ((name != nullptr) ? name : "(NULL)") << " with volume interface found");
		return GST_STREAM_VOLUME(volume_element);
	}
	else
	{
		NXPLAY_LOG_MSG(warning, "element with volume interface not found");
		return nullptr;
	}
}


void main_pipeline::thread_main()
{
	std::unique_lock < std::mutex > lock(m_mutex);

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
		std::unique_lock < std::mutex > lock(m_mutex);

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
		std::unique_lock < std::mutex > lock(m_mutex);

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
		std::unique_lock < std::mutex > lock(self->m_mutex);
		NXPLAY_LOG_MSG(debug, "mainloop started - can signal that thread is started");
		self->m_thread_loop_running = true;
		self->m_condition.notify_all();
	}

	// The idle source only needs to run once; let GLib remove
	// it after this call
	return G_SOURCE_REMOVE;
}


} // namespace nxplay end
