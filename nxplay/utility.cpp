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


static gint find_by_factory_func(gconstpointer p_value_ptr, gconstpointer p_user_data)
{
	GValue const *value = static_cast < GValue const* > (p_value_ptr);
	std::string const *expected_factory_name = static_cast < std::string const * > (p_user_data);

	GstElement *elem = GST_ELEMENT(g_value_get_object(value));

	GST_OBJECT_LOCK(elem);

	GstElementFactory *factory = gst_element_get_factory(elem);
	gchar const *factory_name = gst_plugin_feature_get_name(factory);

	gint result = (factory_name == *expected_factory_name) ? 0 : 1;

	GST_OBJECT_UNLOCK(elem);

	return result;
}


bool check_if_element_from_factory(GstElement *p_element, std::string const &p_factory_name)
{
	GstElementFactory *factory = gst_element_get_factory(p_element);
	gchar const *factory_name = gst_plugin_feature_get_name(factory);
	return p_factory_name == factory_name;
}


GstElement *find_element_by_factory_name(GstBin *p_bin, std::string const &p_factory_name)
{
	GstIterator *iter = gst_bin_iterate_recurse(p_bin);
	if (iter == nullptr)
		return nullptr;

	GValue result = G_VALUE_INIT;
	gboolean found = gst_iterator_find_custom(iter, find_by_factory_func, &result, (gpointer)(&p_factory_name));
	gst_iterator_free(iter);

	if (found)
	{
		GstElement *elem = GST_ELEMENT(g_value_dup_object(&result));
		g_value_unset(&result);
		return elem;
	}
	else
		return nullptr;
}


} // namespace nxplay end
