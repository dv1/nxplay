/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include <cstring>
#include "utility.hpp"
#include "log.hpp"


namespace nxplay
{


thread_priority_change::thread_priority_change(int const p_policy, int const p_priority)
{
	m_thread_id = pthread_self();

	// store original priority
	pthread_getschedparam(pthread_self(), &m_original_policy, &m_original_sched_param);

	struct sched_param param;
	param.sched_priority = p_priority;
	int ret = pthread_setschedparam(m_thread_id, p_policy, &param);
	if (ret != 0)
		NXPLAY_LOG_MSG(error, "could not set thread priority: " << std::strerror(ret));
}


thread_priority_change::thread_priority_change()
{
	m_thread_id = pthread_self();

	// store original priority
	pthread_getschedparam(pthread_self(), &m_original_policy, &m_original_sched_param);
}


thread_priority_change::~thread_priority_change()
{
	// restore original priority
	int ret = pthread_setschedparam(m_thread_id, m_original_policy, &m_original_sched_param);
	if (ret != 0)
		NXPLAY_LOG_MSG(error, "could not set thread priority: " << std::strerror(ret));
}


void thread_priority_change::set_priority(int const p_policy, int const p_priority)
{
	struct sched_param param;
	param.sched_priority = p_priority;
	int ret = pthread_setschedparam(m_thread_id, p_policy, &param);
	if (ret != 0)
		NXPLAY_LOG_MSG(error, "could not set thread priority: " << std::strerror(ret));
}


} // namespace nxplay end
