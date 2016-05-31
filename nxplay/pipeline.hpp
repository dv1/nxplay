/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_PIPELINE_HPP
#define NXPLAY_PIPELINE_HPP

#include <string>
#include <gst/gst.h>
#include <boost/optional.hpp>
#include "media.hpp"


/** nxplay */
namespace nxplay
{


/// Pipeline states
/**
 * Some of these states are transitional. pipeline::is_transitioning() returns true if
 * called during one of these states. Certain calls like pipeline::play_media() or
 * pipeline::set_current_position() will internally be postponed until the transitional
 * state has finished.
 */
enum states
{
	/// Pipeline is idling. No media is loaded, no devices are acquired.
	state_idle,
	/// Pipeline is starting. This state is transitional, and will switch to state
	/// paused/playing when done.
	state_starting,
	/// Pipeline is stopping. This state is transitional, and will switch to state
	/// idle when done.
	state_stopping,
	/// Pipeline is seeking in the currently media.
	/// This state is transitional; it will remain until seeking is complete.
	/// Afterwards, it will return to the previous paused/playing state.
	state_seeking,
	/// Pipeline is buffering the current media.
	/// This state is transitional; it will remain until buffering is complete.
	/// Afterwards, it will return to the previous paused/playing state.
	state_buffering,
	/// Pipeline is playing the current media.
	state_playing,
	/// Pipeline is paused.
	state_paused
};


/// Allowed transport protocols for incoming network streams
/**
 * These are flags which can be bitwise-OR combined to inform the pipeline what transports
 * shall be permitted for input network streams. Note that these flags are not used by
 * all protocols. For example, HTTP always uses TCP.
 *
 * Currently, RTSP is the only protocol that can be configured with these flags.
 * With RTSP, UDP is preferred over TCP if both are available. By default, UDP and TCP
 * are allowed.
 */
enum transport_protocols
{
	/// Allow UDP-based data transport
	transport_protocol_udp = 0x01,
	/// Allow TCP-based data transport
	transport_protocol_tcp = 0x02
};


/// Positioning units.
/**
 * These are needed for duration updates and playback position requests. nxplay supports
 * two ways of specifying position and duraiton: nanoseconds (the GStreamer timestamp unit)
 * and bytes. Some media might not support them both. If for example bytes are not supported,
 * duration updates in bytes and position queries in bytes will always return -1.
 */
enum position_units
{
	position_unit_nanoseconds,
	position_unit_bytes,
};


/// Returns a string representation of a state; useful for logging
std::string get_state_name(states const p_state);


/// Additional properties for play_media() calls.
/**
 * These are optional properties which affect the behavior of play_media().
 * They make it possible to start playback in paused state, to seek right
 * when starting playback, and to adjust the streaming buffer size/duration.
 *
 * There are two types of network connections: stream-based and packet-based.
 *
 * Stream-based connections are used with HTTP for example, and use the stream
 * buffering. Stream buffering essentially downloads the first N bytes and
 * fills a buffer with the downloaded content. Once the buffer is filled enough,
 * playback can start. During playback, the download continues at the same rate
 * as the data is consumed.
 *
 * Packet-based connections explicitely operate on discrete packets. One example
 * is RTSP. By default, RTSP uses UDP for the media transport, but can also use
 * TCP. But even with TCP, it transmits packets, and not a stream. With packets,
 * the stream buffering makes no sense. Instead, a jitter buffer is used. This
 * jitter buffer retains received packets for a given period (which is the
 * m_jitter_buffer_length value below). Packets can be up to this many milli-
 * seconds late. Any packets that arrive later than that are discarded, and the
 * rest of the pipeline considers them lost. Truly lost packets are also detected
 * and reported. In both cases, the packet_loss_callback is invoked. Buffer levels
 * also do not exist with packet-based connections.
 *
 *
 * Configuring the stream buffer (for stream-based connections):
 * -------------------------------------------------------------
 *
 * The stream buffering is controlled by three properties: estimation duration,
 * timeout, and size.
 *
 * Size is an explicit maximum size for the buffering, in bytes. This value
 * must be nonzero.
 *
 * Timeout specifies a timeout value for an internal timer which a stream
 * uses. If this timer runs out, and the stream is still buffering, then the
 * buffering finishes (= is set as having reached 100%). This is a mechanism
 * to prevent overly long buffering. For example, if the maximum size is set
 * to 2 MB, and the input is a 64 kbps MP3 stream, it would take a long time
 * to fill these 2 MB. Note that a next stream will never use this timeout;
 * only if it becomes the current stream while it is buffering will it enable
 * this timer.
 * If m_buffer_timeout is set to zero, no timeout is used.
 *
 * Estimation duration is a duration value which is used when a bitrate is
 * determined. Using this bitrate, a buffer size in bytes is estimated. The
 * buffering will then use either this calculated size, or the m_buffer_size
 * value, whichever is smaller. This is useful to configure a stream's buffer
 * to cover a certain duration, which is better for cases where the source
 * delivers data rather slowly (internet radios with poor connectivity, for
 * example).
 * If m_buffer_estimation_duration is set to zero, this estimation is not done.
 *
 * If the fill level percentage of the buffer goes below m_low_buffer_threshold,
 * the pipeline is set to a buffering state. If the pipeline is buffering, and
 * the fill level exceeds m_high_buffer_threshold, the buffering state is reset.
 * A m_high_buffer_threshold value lower than 100% means that the pipeline will
 * exit the buffering state, but will still fill up the buffer until its maximum
 * size is reached. For example, m_high_buffer_threshold set to 20, and
 * m_buffer_size set to 10 MB, will cause the pipeline to exit the state once
 * the buffer contains 2 MB (= 20% of 10 MB), but the buffer will still get
 * filled until it contains 10 MB (or until the end-of-stream is reached).
 * m_low_buffer_threshold must always be smaller than m_high_buffer_threshold.
 * The default values are 10 for m_low_buffer_threshold and 99 for m_high_buffer_threshold.
 *
 * The m_allowed_transports value is ignored with stream-based connections.
 *
 *
 * Configuring the jitter buffer (for packet-based connections):
 * -------------------------------------------------------------
 *
 * The m_jitter_buffer_length value specifies how late packets can be. Packet-based
 * connections have real-time characteristics, meaning that packets must arrive
 * on time. Since the transport delay from sender to receiver is never zero,
 * packets are given a grace period for arriving. This period is m_jitter_buffer_length.
 * By default, 2 seconds are used. Higher values increase robustness, but also cause
 * noticeable delays when starting reception, pausing/unpausing, and seeking. Lower
 * values increase responsiveness, but reduce robustness (for example, if on average,
 * packets arrive 300ms late, and the jitter buffer length is set to 290 ms, then
 * packets will often be considered lost.
 *
 * The m_allowed_transports value is respected by some packet-based connections.
 * See the transport_protocols documentation for details.
 *
 * m_do_retransmissions specifies whether or not the server shall be asked to
 * retransmit lost packets. By default, retransmissions are done.
 */
struct playback_properties
{
	/// If true, playback will be initially paused. Default value is false.
	bool m_start_paused;

	/// If >0, playback will initially start at this position. Default value is 0.
	gint64 m_start_at_position;
	/// Unit to use for m_start_at_position. Default value is position_unit_nanoseconds.
	position_units m_start_at_position_unit;

	/// Values other than boost::none specify the duration to be used for bitrate-based estimations, in nanoseconds.
	/**
	 * See the buffering description in the playback_properties documentation for details.
	 */
	boost::optional < guint64 > m_buffer_estimation_duration;

	/// Values other than boost::none specify the buffering timeout, in nanoseconds.
	/**
	 * See the buffering description in the playback_properties documentation for details.
	 */
	boost::optional < guint64 > m_buffer_timeout;
	/// Values other than boost::none specify the maximum size of the streaming buffer, in bytes.
	/**
	 * See the buffering description in the playback_properties documentation for details.
	 */
	boost::optional < guint > m_buffer_size;

	/// Values other than boost::none specify the low buffering threshold, in percent.
	/**
	 * See the threshold percentage in the playback_properties documentation for details.
	 */
	boost::optional < guint > m_low_buffer_threshold;
	/// Values other than boost::none specify the high buffering threshold, in percent.
	/**
	 * See the threshold percentage in the playback_properties documentation for details.
	 */
	boost::optional < guint > m_high_buffer_threshold;

	/// Length of the packet jitter buffer, in milliseconds.
	/**
	 * See the descriptions for the jitter buffer and packet-based connections in the
	 * playback_properties documentation above for details.
	 */
	boost::optional < guint64 > m_jitter_buffer_length;

	/// Whether or not to do retransmissions in case of packet loss.
	/**
	 * See the retransmission description in the playback_properties documentation for details.
	 */
	boost::optional < bool > m_do_retransmissions;

	/// Which transports shall be permitted with network connections.
	/**
	 * Default permissions vary from protocol to protocol. See the transport_protocols
	 * documentation for details.
	 */
	boost::optional < guint32 > m_allowed_transports;

	/// Default constructor. Sets the values above to their defaults.
	playback_properties();

	/// Constructor for explicitely initializing all values.
	explicit playback_properties(
		bool const p_start_paused,
		gint64 const p_start_at_position,
		position_units const p_start_at_position_unit,
		boost::optional < guint64 > const &p_buffer_estimation_duration,
		boost::optional < guint64 > const &p_buffer_duration_timeout,
		boost::optional < guint > const &p_buffer_size,
		boost::optional < guint > const &p_low_buffer_threshold,
		boost::optional < guint > const &p_high_buffer_threshold,
		boost::optional < guint64 > const &p_jitter_buffer_length,
		boost::optional < bool > const &p_do_retransmissions,
		boost::optional < guint32 > const p_allowed_transports
	);
};


/// Abstract pipeline interface.
/**
 * This is the core interface in nxplay. Through this interface, playback is started and
 * controlled.
 *
 * Pipelines get media objects to play or schedule as next playback. Derived classes are
 * however free to not support next media. One example would be a pipeline which acts as
 * a fixed receiver of some kind. A "next media" makes no sense there.
 *
 * If something goes wrong, pipelines reinitialize themselves. If a fatal error occurs
 * which cannot be fixed even by reinitialization, pipelines are allowed to throw
 * exceptions based on std::exception . Exceptions which cross the boundaries of the
 * pipeline are only allowed for this purpose. Otherwise, the public pipeline methods
 * never throw exceptions.
 *
 * Method calls are only rejected if there is absolutely no other way. If for example
 * the pipeline is in a transitional state (see the states enum for details), and
 * a play_media() call cannot be performed right then, this call must somehow be
 * internally recorded and postponed until the transition is finished. Rejections are
 * only permitted if the request fails for some reason (for example, when the media
 * URI points to a non-existing source).
 *
 * Transitional states exist because pipelines are free to do state changes asynchronously.
 * For example, it is not required (and generally not recommended) to block inside
 * a set_current_position() call until the pipeline finished seeking. Instead, callers
 * should make use of any notification mechanisms the derived pipeline implementation
 * offer (for example, main_pipeline uses callback functions). Transitional states allow
 * the caller to display some sort of waiting indicator in the user interface.
 *
 * Unless there is a very good reason to do so, pipeline implementations do not allow to
 * directly set the state from the outside, since this can lead to many undefined cases.
 *
 * The fundamental goal is to make the pipeline robust and simple to use. It must be
 * able to handle any requests without deadlocking, reaching some undefined state, or
 * requiring multiple manual steps for a request to succeeed. For example, if something
 * is currently playing, it must not be necessary to manually call stop() prior to the
 * play_media() call, or call set_paused() before and after a set_current_position()
 * call. Any public method can be called at any time in any state until explicitely
 * stated otherwise.
 *
 * In most cases, the derived main_pipeline class will be used. The pipeline base class
 * is useful as a building block to a "selector" however which can switch between
 * pipelines. This is planned for future nxplay versions.
 *
 * Unless documented otherwise, pipeline reinitializations always cancel any internal
 * postponed tasks.
 *
 * The methods in general are not guaranteed to be thread safe.
 */
class pipeline
{
public:
	/// Destructor. Cancels any current transitions and ends playback immediately.
	virtual ~pipeline();

	/// Begins playback of given media, either right now, or when the current playback ends.
	/**
	 * This function instructs the pipeline to commence playing the given media.
	 * If p_play_now is true, or if the current playback's token is the same as p_token
	 * (explained in detail below), or if no playback is currently running, p_media is
	 * played immediately, and becomes the "current media". Otherwise, p_media is scheduled
	 * to become the "next media", and is played as soon as the current media ends. This
	 * makes it possible for pipeline implementations to support gapless playback. If
	 * some other media has already been scheduled as next media earlier, then this new
	 * next media replaces it.
	 *
	 * If media cannot currently be played because the pipeline is in a transitional state,
	 * the call is postponed, and automatically executed as soon as the transition is finished.
	 * If it is postponed, the return value is still true.
	 *
	 * The call is also given a token. A token is a method to identify unique calls and
	 * prevent certain otherwise ambiguous cases. Example: user wants to play X now,
	 * calls play_media(X, true), and wants to play Y afterwards, thus calls play_media(Y, false).
	 * But then, *before* X ends, the user changes his mind, and wants to play Z instead of Y
	 * after X ends. If the user is quick enough, the play_media(Z, false); call will
	 * overwrite the previous "next media"; it will replace Y with Z. If however the user
	 * is not fast enough, and Y starts playing, the play_media(Z, false); call will schedule
	 * Z to be played after Y.
	 * To counter this, tokens are used. With tokens, this situation is resolved. The user
	 * then simply reuses the token he used for the play_media(Y, false); call. Example:
	 * play_media(1, X, true) -> play_media(2, Y, false) -> play_media(2, Z, false) .
	 * If the user is not fast enough, and Y starts playing, the last play_media() call
	 * unambiguously tells the pipeline that Z is replacing Y. Therefore, in this case, Y
	 * will immediately stop, and Z will start playing.
	 *
	 * If playback starts right now, any previously set next media gets discarded.
	 *
	 * Token numbers can in theory be anything, as long as they are assigned properly, just like
	 * the example above demonstrates. For convenience, the get_new_token() function can be used,
	 * which generates unique tokens.
	 *
	 * @note Derived classes typically don't override the play_media() overloads. Instead, they
	 * override the protected play_media_impl() function.
	 *
	 * @param p_token Token to associate the playback request with
	 * @param p_media Media to play (either now or later); the media object is copied internally
	 * @param p_play_now If true, the media must be played right now (see above)
	 * @param p_buffer_size If set, describes the size of the internal buffer (only used if
	 *        the internal uridecodebin creates a buffer, for example for HTTP streams)
	 * @param p_buffer_size_in_nanoseconds If true, the value of p_buffer_size is in nanoseconds,
	 *        otherwise the value is in bytes
	 * @return true if the request succeeded, false otherwise (a postponed playback request
	 *         still returns true!)
	 */
	virtual bool play_media(guint64 const p_token, media const &p_media, bool const p_play_now, playback_properties const &p_properties = playback_properties());
	/// Overlaoded play_media() function for movable media obejcts
	/**
	 * The only difference to the other overload is that p_media is is an rvalue reference.
	 * Useful to avoid temporary media object copies (this includes their payloads).
	 */
	virtual bool play_media(guint64 const p_token, media &&p_media, bool const p_play_now, playback_properties const &p_properties = playback_properties());
	/// Stops any current playback and erases any scheduled next media.
	/**
	 * If this is called in the idle state, nothing happens. Otherwise, the pipeline will be
	 * put to the idle state. Any present current/next media will be erased. Any internal
	 * playback pipelines will be shut down. If the pipeline is in a transitional state and
	 * thus cannot be stopped immediately, the call is postponed, and the pipeline stopped
	 * as soon as the transition finishes.
	 */
	virtual void stop() = 0;
	/// Convenience function, useful for play_media() calls.
	/**
	 * @return Newly generated unique tokens
	 */
	virtual guint64 get_new_token() = 0;

	/// Pauses/unpauses the pipeline.
	/**
	 * This call is only meaningful if the pipeline is either in the playing or paused state
	 * or is transitioning to one of these two states. Otherwise, it is ignored. If the
	 * pipeline is already paused, and p_paused is true, the call is ignored. Same if
	 * the pipeline is playing, and p_paused is false. If the current media is a live media,
	 * it is ignored as well.
	 *
	 * In the special transitioning case described earlier where the pipeline is transitioning
	 * to either the paused or the playing state, this call is postponed, and executed
	 * once the transition is finished.
	 *
	 * @param p_paused If true, this initiates a state change to state_paused, otherwise
	 *        it initiates a state change to state_playing (see above for exceptions to this rule)
	 */
	virtual void set_paused(bool const p_paused) = 0;

	/// Returns true if the  pipeline is currently in a transitioning state.
	/**
	 * A transitioning state is a state where certain actions like play_media() cannot
	 * be executed immediately. See the states enum for details.
	 *
	 * @return true if the pipeline is in a transitional state, false otherwise
	 */
	virtual bool is_transitioning() const = 0;

	/// Returns the state the pipeline is currently in.
	virtual states get_current_state() const = 0;

	/// Sets the pipeline's current playback position (also known as "seeking").
	/**
	 * This call is ignored unless the pipeline is in a paused or playing state, or
	 * transitioning to one of these two states.
	 *
	 * This call is postponed if the pipeline is in a transitional state, and executed
	 * as soon as the transition ends. Pipelines do not have to support seeking, and can
	 * ignore this call if they don't, since seeking may not be supported with certain
	 * media (for example, some RTSP & HTTP radio streams). Some media might also only
	 * support byte seeks, or nanosecond seeks (in practice, the latter is supported by
	 * pretty much all types of media that can seek in general, so it is a safe bet to
	 * use it).
	 *
	 * Seeking may occur asynchronously in the background. In this case, the pipeline
	 * state switches to state_seeking, and back to the original state (either playing
	 * or paused) when seeking is done.
	 *
	 * @param p_new_position New position, either in nanoseconds or in bytes, depending on p_unit
	 * @param p_unit Unit for the position value
	 */
	virtual void set_current_position(gint64 const p_new_position, position_units const p_unit = position_unit_nanoseconds) = 0;
	/// Returns the current position in the given units.
	/**
	 * @param p_unit Units to use for the current position
	 * @return The current position in the given units, or -1 if the current position cannot
	 *         be determined (at least not with the given unit)
	 */
	virtual gint64 get_current_position(position_units const p_unit = position_unit_nanoseconds) const = 0;

	/// Returns the current duration in the given units.
	/**
	 * @param p_unit Units to use for the current duration
	 * @return The current duration in the given units, or -1 if the current duration cannot
	 *         be determined (at least not with the given unit)
	 */
	virtual gint64 get_duration(position_units const p_unit = position_unit_nanoseconds) const = 0;

	/// Adds/removes a tag to/from the set of forcibly postponed ones.
	/**
	 * If a tag is added to this set, they will not be reported immediately
	 * when GStreamer posts them. Instead, the new_tags_callback will be invoked
	 * later, in a next update interval. The nature of the interval is specific
	 * to the implementation, but it is guaranteed that tags are then not
	 * reported immediately. This is useful for tags which can change very often,
	 * like the BITRATE one.
	 *
	 * @param p_tag Name of the tag to add/remove
	 * @param p_postpone If true, adds the tag to the list, otherwise removes it;
	 *        if p_postpone is true and the tag is already added, or if p_postpone
	 *        is false and the tag is not in the set, this function does nothing
	 */
	virtual void force_postpone_tag(std::string const &p_tag, bool const p_postpone) = 0;


protected:
	// Derived classes only need to overload this one, and can leave the two play_media() functions alone
	virtual bool play_media_impl(guint64 const p_token, media &&p_media, bool const p_play_now, playback_properties const &p_properties) = 0;
};


} // namespace nxplay end


#endif
