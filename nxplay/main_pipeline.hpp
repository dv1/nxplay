/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_MAIN_PIPELINE_HPP
#define NXPLAY_MAIN_PIPELINE_HPP

#include <functional>
#include <queue>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "pipeline.hpp"
#include "tag_list.hpp"


/** nxplay */
namespace nxplay
{


/// Main pipeline implementation based on GStreamer.
/**
 * This is a pipeline implementation that creates a GStreamer pipeline to facilitate
 * media playback. It also starts its own internal GLib mainloop to receive GStreamer
 * pipeline messages and handle them. To that end, main_pipeline spawns an internal
 * thread, where the GLib mainloop is ran. As a result, this pipeline can be used
 * even in environments where no GLib mainloop is set up and running.
 *
 * As described in the pipeline class documentation, state changes, updates etc.
 * typically happen asynchronously. For this reason, main_pipeline houses a set
 * of function objects which act as callbacks. These callbacks are typically called
 * while an internal mutex is held, so it is not recommended to let execution inside
 * these callbacks run for too long. The callbacks should finish their business
 * quickly. Also, they are called in the internal thread. Synchronization primitives
 * might be required.
 *
 * Internally, gapless playback is implemented with the concat element that got
 * introduced in GStreamer 1.5.
 *
 * @note Some of these callback have an argument which passes a reference to the
 * current or next media. This reference is only guaranteed to remain valid for the
 * duration of the callback. Once the callback finishes, the referred media object
 * might get overwritten or discarded. If the values of the media object are also
 * needed after the callback finishes, be sure to copy the media object.
 */
class main_pipeline
	: public pipeline
{
public:
	/// Callback for when media actually started to play.
	/**
	 * This is called as soon as media becomes the current media, that is, when it
	 * starts playing. During the execution of this callback, no next media is
	 * set.
	 *
	 * @param p_current_media Const reference to internal new current media
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 */
	typedef std::function < void(media const &p_current_media, guint64 const p_token) > media_started_callback;
	/// Called when current media playback ended and no next media was set.
	/**
	 * Once end-of-stream is reached, the pipeline will stop, and get to the idle
	 * state. Then, this callback will be invoked. This makes sure the pipeline
	 * does not reach an undefined state (which otherwise would happen if the
	 * pipeline isn't stopped after end-of-stream).
	 *
	 * This callback is also useful to continue to play media sometimes. See the
	 * documentation of media_about_to_end_callback for details.
	 */
	typedef std::function < void() > end_of_stream_callback;
	/// Notifies about an informational message coming from GStreamer.
	/**
	 * @param p_info_message Human-readable information message
	 */
	typedef std::function < void(std::string const &p_info_message) > info_callback;
	/// Notifies about a warning coming from GStreamer.
	/**
	 * @param p_warning_message Human-readable warning
	 */
	typedef std::function < void(std::string const &p_warning_message) > warning_callback;
	/// Notifies about an error message coming from GStreamer.
	/**
	 * When GStreamer pipelines produce error messages, the GStreamer
	 * pipeline is considered to be in a nonrecoverable state. Therefore,
	 * when this occurs, this callback is invoked, and afterwards,
	 * the pipeline is reinitialized.
	 *
	 * @param p_error_message Human-readable information message
	 */
	typedef std::function < void(std::string const &p_error_message) > error_callback;
	/// Notifies about newly seen tags.
	/**
	 * @param p_current_media Const reference to internal new current media
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 * @param p_tag_list tag_list object containing the new tags. It is a
	 *                   temporary value which can be moved.
	 */
	typedef std::function < void(media const &p_current_media, guint64 const p_token, tag_list &&p_tag_list) > new_tags_callback;
	/// Notifies about state changes.
	/**
	 * If p_new_state is a transitional state, some kind of wait indicator should be
	 * displayed on user interfaces. In the state_buffering state, a progress indicator
	 * is also an option. If p_new_state is not a transitional state, any present
	 * wait/progress indicator should be removed.
	 *
	 * @param p_old_state The previous state the pipeline was in
	 * @param p_new_state The state the pipeline is in now
	 */
	typedef std::function < void(states const p_old_state, states const p_new_state) > state_changed_callback;
	/// Notifies about buffering updates.
	/**
	 * When media needs to buffer, this callback is invoked. This gives applications
	 * the chance to display a wait indicator if p_is_current_media is true.
	 *
	 * @note This can even be called for current media while it is playing. This happens
	 * if buffering is going on with live sources. Playback cannot be paused if the
	 * media source is live, so the pipeline state won't switch to state_buffering then.
	 * Instead, it will stay at state_playing. Buffering messages with live sources are
	 * a rough indicator of an internal fill level, nothing more.
	 *
	 * @param p_media Media that is buffering
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 * @param p_is_current_media true if p_media is the current media (in which case the
	 *        pipeline's current state will be state_buffering), false otherwise
	 * @param p_percentage Buffering percentage (0-100)
	 */
	typedef std::function < void(media const &p_media, guint64 const p_token, bool const p_is_current_media, unsigned int const p_percentage) > buffering_updated_callback;
	/// Notifies about a newly determined duration value for the current media.
	/**
	 * Duration updates might happen more than once for the same current media. With
	 * some media, duration might only be known in bytes. With others, only nanosecond
	 * durations might exist. Yet others might have no duration at all (one example is
	 * an internet radio stream). If the duration values are -1, then no duration is
	 * known. The user interface then should indicate that.
	 *
	 * @param p_current_media Const reference to internal new current media
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 * @param p_new_duration New duration value, or -1 if no duration is known in the given units
	 * @þaram p_unit Units for the given duration value
	 */
	typedef std::function < void(media const &p_current_media, guint64 const p_token, gint64 const p_new_duration, position_units const p_unit) > duration_updated_callback;
	/// Notifies about additional information about media
	/**
	 * This is called once some more details about the media are known after it is
	 * loaded. Such details are: whether or not this is a live media source, and
	 * whether or not this media is seekable. The pipeline will automatically
	 * refuse to seek if the media is not seekable, and if it is live, buffering
	 * will never pause playback.
	 *
	 * If it cannot be determined whether or not media is seekable, it is assumed
	 * to be non-seekable. Same with the live flag; if it cannot be determined,
	 * the media is assumed to not be live.
	 *
	 * @note "live" refers to the GStreamer definition of live. This means that
	 * for example HTTP streams are not live, while RTSP and line-in sources are.
	 *
	 * @param p_media Const reference to the media this information is about
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 * @param p_is_current_media true if p_media is the current media, false
	 *        otherwise
	 * @param p_is_seekable true if the set_current_position() call is supported
	 *        with the current media, false otherwise
	 * @param p_is_live true if this is a live media source, false otherwise
	 */
	typedef std::function < void(media const &p_media, guint64 const p_token, bool const p_is_current_media, bool const p_is_seekable, bool const p_is_live) > media_information_callback;
	/// Notifies about the current media's playback position in the given unit
	/**
	 * This callback is invoked repeatedly if playback positions can be determined.
	 * If the media cannot determine playback positions, this is never called.
	 * Before this call, the user interface should indicate that no position is
	 * known. (It is valid to disable this if a manual get_current_position() call
	 * returned a valid position.)
	 *
	 * The call intervals are determined by the p_update_interval argument in
	 * the main_pipeline constructor.
	 *
	 * @param p_current_media Const reference to internal new current media
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 * @param p_new_duration New position value
	 * @þaram p_unit Units for the given duration value
	 */
	typedef std::function < void(media const &p_current_media, guint64 const p_token, gint64 const p_new_position, position_units const p_unit) > position_updated_callback;
	/// Notifies that the current media is about to end.
	/**
	 * This is called a short while before the current media ends. This short
	 * while is determined by the p_needs_next_media_time argument of the
	 * main_pipeline constructor. With this callback, the application gets a
	 * chance to set up the next media shortly before the current playback
	 * ends.
	 *
	 * It sometimes is important to wait until the current one almost ended,
	 * as opposed to setting the next media as soon as possible. Some media
	 * services produce URIs that are temporariy, for example. If they aren't
	 * accessed within a certain period, the URI becomes invalid.
	 *
	 * @note Some media have broken duration indicators. In this case, this
	 * callback might not be called. For example, some files might claim a
	 * duration of 50 seconds, but actually end after 42 seconds. In this
	 * case, the m_end_of_stream_callback will be called, but not this one.
	 *
	 * @param p_current_media The current media that is about to end
	 * @param p_token Associated playback token (see pipeline::play_media() )
	 */
	typedef std::function < void(media const &p_current_media, guint64 const p_token) > media_about_to_end_callback;

	/// Structure containing all of the callbacks.
	/**
	 * All callbacks are optional. If a callback is not defined, it will not be called.
	 */
	struct callbacks
	{
		media_started_callback      m_media_started_callback;
		end_of_stream_callback      m_end_of_stream_callback;
		info_callback               m_info_callback;
		warning_callback            m_warning_callback;
		error_callback              m_error_callback;
		new_tags_callback           m_new_tags_callback;
		state_changed_callback      m_state_changed_callback;
		buffering_updated_callback  m_buffering_updated_callback;
		duration_updated_callback   m_duration_updated_callback;
		media_information_callback  m_media_information_callback;
		position_updated_callback   m_position_updated_callback;
		media_about_to_end_callback m_media_about_to_end_callback;
	};

	/// Constructor. Sets up the callbacks and initializes the pipeline.
	/**
	 * After the constructor finishes, the pipeline is in the idle state.
	 *
	 * @param p_callbacks Callbacks to use in this pipeline
	 * @param p_needs_next_media_time If current media has only this much time to
	 *        time to play, call the media_about_to_end_callback (see its
	 *        documentation for details)
	 * @param p_update_interval Update interval for position (and optionally tag) updaes
	 * @param p_update_tags_in_interval If true, the tag updates (that is,
	 *        the new_tags_callback calls) will happen in sync with the
	 *        periodic updates; if false, they happen asynchronously
	 */
	explicit main_pipeline(callbacks const &p_callbacks, GstClockTime const p_needs_next_media_time = GST_SECOND * 5, guint const p_update_interval = 500, bool const p_update_tags_in_interval = false);
	~main_pipeline();

	virtual guint64 get_new_token() override;
	virtual void stop() override;

	virtual void set_paused(bool const p_paused) override;

	virtual bool is_transitioning() const override;

	virtual states get_current_state() const override;

	virtual void set_current_position(gint64 const p_new_position, position_units const p_unit) override;
	virtual gint64 get_current_position(position_units const p_unit) const override;

	virtual gint64 get_duration(position_units const p_unit) const override;

	virtual void set_volume(double const p_new_volume, GstStreamVolumeFormat const p_format) override;
	virtual double get_volume(GstStreamVolumeFormat const p_format) const override;
	virtual void set_muted(bool const p_mute) override;
	virtual bool is_muted() const override;


protected:
	virtual bool play_media_impl(guint64 const p_token, media &&p_media, bool const p_play_now) override;


private:
	// Internal functions have a _nolock suffix if they don't lock the mutex


	// miscellaneous

	bool is_transitioning_nolock() const;
	bool set_gstreamer_state_nolock(GstState const p_new_gstreamer_state);


	// postponed task

	struct postponed_task
	{
		enum types
		{
			type_none,
			type_play,
			type_pause,
			type_stop,
			type_set_position,
			type_set_state
		};

		types m_type;
		bool m_paused;
		media m_media;
		guint64 m_token;
		gint64 m_position;
		position_units m_position_format;
		GstState m_gstreamer_state;

		postponed_task()
			: m_type(type_none)
		{
		}
	};

	void handle_postponed_task_nolock();

	postponed_task m_postponed_task;


	// streams encapsulate media objects and their associated tokens and GStreamer elements.
	// The main_pipeline works with the streams directly, not with the media objects.
	class stream
	{
	public:
		explicit stream(main_pipeline &p_pipeline, guint64 const p_token, media &&p_media, GstBin *p_container_bin, GstElement *p_concat_elem);
		~stream();

		void sync_states();

		GstPad* get_srcpad();
		guint64 get_token() const;
		media const & get_media() const;

		bool contains_object(GstObject *p_object);

		void set_buffering(bool const p_flag);
		bool is_buffering() const;

		bool is_live() const;
		bool is_seekable() const;

	private:
		static void static_new_pad_callback(GstElement *p_uridecodebin, GstPad *p_pad, gpointer p_data);

		main_pipeline &m_pipeline;
		guint64 m_token;
		media m_media;
		GstElement *m_uridecodebin_elem, *m_identity_elem, *m_concat_elem;
		GstPad *m_identity_srcpad, *m_concat_sinkpad;
		GstBin *m_container_bin;
		bool m_is_buffering;
		bool m_is_live, m_is_seekable;
	};

	typedef std::shared_ptr < stream > stream_sptr;
	typedef std::queue < stream_sptr > stream_queue;

	stream_sptr setup_stream_nolock(guint64 const p_token, media &&p_media);
	void cleanup_old_streams_nolock();

	static GstPadProbeReturn static_stream_eos_probe(GstPad *p_pad, GstPadProbeInfo *p_info, gpointer p_data);

	stream_sptr m_current_stream, m_next_stream;
	stream_queue m_old_streams;


	// pipeline state & management

	struct seeking_data
	{
		bool m_was_paused;
		GstClockTime m_seek_to_position;
		GstFormat m_seek_format;
	};

	bool initialize_pipeline_nolock();
	void shutdown_pipeline_nolock(bool const p_set_state = true);
	bool reinitialize_pipeline_nolock();
	void set_pipeline_to_idle_nolock(bool const p_set_state);
	void set_initial_state_values_nolock();
	void set_state_nolock(states const p_new_state);
	bool play_media_nolock(guint64 const p_token, media &&p_media, bool const p_play_now);
	void set_paused_nolock(bool const p_paused);
	void set_current_position_nolock(gint64 const p_new_position, position_units const p_unit);
	void stop_nolock();
	gint64 query_duration_nolock(position_units const p_unit) const;
	void update_durations_nolock();
	void finish_seeking_nolock();
	void recheck_buffering_state_nolock();
	void create_dot_pipeline_dump_nolock(std::string const &p_extra_name);

	seeking_data m_seeking_data;
	states m_state;
	gint64 m_duration_in_nanoseconds, m_duration_in_bytes;
	bool m_update_tags_in_interval;
	bool m_block_abouttoend_notifications;
	bool m_force_next_duration_update;


	// playback timer

	static gboolean static_timeout_cb(gpointer p_data);
	void setup_timeouts_nolock();
	void shutdown_timeouts_nolock();

	GSource *m_timeout_source;
	GstClockTime m_needs_next_media_time;
	guint m_update_interval;


	// GStreamer specifics

	static gboolean static_bus_watch(GstBus *p_bus, GstMessage *p_msg, gpointer p_data);
	GstStreamVolume* find_stream_volume_interface();

	GstState m_current_gstreamer_state, m_pending_gstreamer_state;
	GstElement *m_pipeline_elem, *m_concat_elem, *m_volume_elem, *m_audiosink_elem;
	GstStreamVolume *m_volume_iface;
	GstBus *m_bus;
	GSource *m_watch_source;


	// token management

	guint64 m_next_token;


	// thread management

	void thread_main();
	void start_thread();
	void stop_thread();
	static gboolean static_loop_start_cb(gpointer p_data);

	std::thread m_thread;
	mutable std::mutex m_mutex;
	std::condition_variable m_condition;
	GMainLoop *m_thread_loop;
	GMainContext *m_thread_loop_context;
	bool m_thread_loop_running;


	// miscellaneous

	callbacks m_callbacks;
};


} // namespace nxplay end


#endif
