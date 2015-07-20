/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_LOG_HPP
#define NXPLAY_LOG_HPP

#include <sstream>
#include <string>
#include <functional>
#include <chrono>


/** nxplay */
namespace nxplay
{


/// Log levels
enum log_levels
{
	log_level_trace = 0,
	log_level_debug,
	log_level_info,
	log_level_warning,
	log_level_error
};


/// Returns a string representation of the given log level.
/**
 * Returned pointers refer to static strings. Do not try to deallocate.
 *
 * @param p_log_level Log level to get a string representation for
 * @param p_padded If true, adds trailing whitespaces to ensure all
 *        strings are of equal length
 * @return Pointer to static string
 */
char const * get_log_level_name(log_levels const p_log_level, bool const p_padded = false);


/// Callback function for writing log messages
/**
 * @param p_timestamp Monotonic timestamp when the message was logged
 * @param p_log_level Log level for the message
 * @param p_srcfile Pointer to C string with the name of the source file
 *        where the message was logged
 * @param p_srcline Number of the line in the source file where the message
 *        was logged
 * @param p_srcfunction Pointer to C string with the name of the function
 *        where the message was logged
 * @param p_message The message that shall be logged
 */
typedef std::function < void(std::chrono::steady_clock::duration const p_timestamp, log_levels const p_log_level, char const *p_srcfile, int const p_srcline, char const *p_srcfunction, std::string const &p_message) > log_write_function;


/// Installs a default log write function that writes log messages to stderr
void set_stderr_output();
/// Sets a custom log write function, overwriting any previously set function
/**
 * @param p_function The new log write function to use
 */
void set_log_write_function(log_write_function const &p_function);
/// Core function for logging messages. For most cases, use the NXPLAY_LOG_MSG macro instead.
/*
 * This is used inside the NXPLAY_LOG_MSG macro, which takes care of determining the
 * source file name, source line number, and source function, and provides a
 * C++ stream operator based interface.
 */
void log_message(log_levels const p_log_level, char const *p_srcfile, int const p_srcline, char const *p_srcfunction, std::string const &p_message);

/// Sets the minimum level for logging.
/*
 * Messages with levels below this threshold will be discarded.
 *
 * @param p_min_log_level Minimum level messages must have to be allowed
 *        to be written with the defined log write function
 */
void set_min_log_level(log_levels const p_min_log_level);
/// Returns the currently set minimum log level
log_levels get_min_log_level();



/**
 * Convenience macro for logging.
 *
 * Typically, this is used instead of the log_message() function. This macro
 * uses __FILE__, __LINE__ and __func__ macros to determine source file name,
 * line number, and function name. It also makes it possible to use an
 * iostream-like like directly. Example: NXPLAY_LOG_MSG(debug, "test " << value);
 */
#define NXPLAY_LOG_MSG(LEVEL, MSG) \
	do \
	{ \
		if (( ::nxplay::log_level_##LEVEL) >= ::nxplay::get_min_log_level()) \
		{ \
			std::stringstream nxplay_log_msg_internal_sstr_813585712987; \
			nxplay_log_msg_internal_sstr_813585712987 << MSG; \
			::nxplay::log_message(::nxplay::log_level_##LEVEL, __FILE__, __LINE__, __func__, nxplay_log_msg_internal_sstr_813585712987.str()); \
		} \
	} \
	while (false)


} // namespace nxplay end


#endif
