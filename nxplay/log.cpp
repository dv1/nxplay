/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include <iomanip>
#include <iostream>
#include <cstring>
#include <assert.h>
#include "log.hpp"


namespace nxplay
{


namespace
{


void stderr_logfunc(std::chrono::steady_clock::duration const p_timestamp, log_levels const p_log_level, char const *p_srcfile, int const p_srcline, char const *p_srcfunction, std::string const &p_message)
{
	// format: [    x.xxx] loglevel  [source.cpp:linenumber function-name]    log message
	// timestamp string: [    x.xxx]
	// source string: source.cpp:linenumber
	// location string: source.cpp:linenumber function-name


	std::string linestr = std::to_string(p_srcline);


	static std::chrono::steady_clock::duration::rep cur_max_timestamp_value = 1000000;
	static int cur_num_timestamp_digits = 6;

	// get current time and calculate the timestamp length for proper padding
	// advance padding in groups of 3 digits to improve readability
	auto ms = std::chrono::duration_cast < std::chrono::milliseconds > (p_timestamp).count();
	auto s = ms / 1000;
	if (s >= cur_max_timestamp_value)
	{
		cur_max_timestamp_value *= 1000;
		cur_num_timestamp_digits += 3;
	}


	// calculate proper padding for source string
	static std::size_t cur_max_source_str_length = 0;
	static int decay_max_source_str_length = 0;

	std::size_t source_str_length = std::strlen(p_srcfile) + 1 + linestr.length();
	if ((cur_max_source_str_length < source_str_length) || (decay_max_source_str_length == 0))
	{
		cur_max_source_str_length = source_str_length;
		decay_max_source_str_length = 100;
	}
	--decay_max_source_str_length;
	std::size_t source_str_padding = cur_max_source_str_length - source_str_length;


	// calculate proper padding for location string
	static std::size_t cur_max_location_str_length = 0;
	static int decay_max_location_str_length = 0;

	std::size_t location_str_length = source_str_length + source_str_padding + std::strlen(p_srcfunction);
	if ((cur_max_location_str_length < location_str_length) || (decay_max_location_str_length == 0))
	{
		cur_max_location_str_length = location_str_length;
		decay_max_location_str_length = 100;
	}
	--decay_max_location_str_length;
	std::size_t location_str_padding = cur_max_location_str_length - location_str_length;


	// print the [    x.xxx] timestamp
	std::cerr << "[" << std::dec << std::setfill (' ') << std::setw(cur_num_timestamp_digits) << s << "." << std::setfill ('0') << std::setw(3) << (ms % 1000) << std::setfill(' ') << "] ";

	// print the log level
	std::cerr << get_log_level_name(p_log_level, true) << " ";

	// print the location ( [source.cpp:linenumber function-name] )
	std::cerr << "[" << p_srcfile << ":" << linestr;
	if (source_str_padding > 0)
		std::cerr << std::setfill(' ') << std::setw(source_str_padding) << "";
	std::cerr << " " << p_srcfunction;
	if (location_str_padding > 0)
		std::cerr << std::setfill(' ') << std::setw(location_str_padding) << "";
	std::cerr << "] ";

	// print the actual log message
	std::cerr << "  " << p_message;

	// end the line
	std::cerr << "\n";
}


struct logger_internal
{
	logger_internal()
		: m_logfunc(stderr_logfunc)
		, m_min_log_level(log_level_info)
	{
		m_time_base = std::chrono::steady_clock::now();
	}

	static logger_internal& instance()
	{
		static logger_internal logger;
		return logger;
	}

	log_write_function m_logfunc;
	log_levels m_min_log_level;
	std::chrono::steady_clock::time_point m_time_base;
};


}


char const * get_log_level_name(log_levels const p_log_level, bool const p_padded)
{
	if (p_padded)
	{
		switch (p_log_level)
		{
			case log_level_trace:   return "trace  ";
			case log_level_debug:   return "debug  ";
			case log_level_info:    return "info   ";
			case log_level_warning: return "warning";
			case log_level_error:   return "error  ";
			default:                return "unknown";
		}
	}
	else
	{
		switch (p_log_level)
		{
			case log_level_trace:   return "trace";
			case log_level_debug:   return "debug";
			case log_level_info:    return "info";
			case log_level_warning: return "warning";
			case log_level_error:   return "error";
			default:                return "unknown";
		}
	}
}


void set_stderr_output()
{
	logger_internal::instance().m_logfunc = stderr_logfunc;
}


void set_log_write_function(log_write_function const &p_function)
{
	logger_internal::instance().m_logfunc = p_function;
}

void log_message(log_levels const p_log_level, char const *p_srcfile, int const p_srcline, char const *p_srcfunction, std::string const &p_message)
{
	assert(logger_internal::instance().m_logfunc);
	logger_internal::instance().m_logfunc(
		std::chrono::steady_clock::now() - logger_internal::instance().m_time_base,
		p_log_level,
		p_srcfile, p_srcline, p_srcfunction,
		p_message
	);
}


void set_min_log_level(log_levels const p_min_log_level)
{
	logger_internal::instance().m_min_log_level = p_min_log_level;
}


log_levels get_min_log_level()
{
	return logger_internal::instance().m_min_log_level;
}


} // namespace ironseed end
