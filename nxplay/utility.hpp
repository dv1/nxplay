/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_UTILITY_HPP
#define NXPLAY_UTILITY_HPP

#include <gst/gst.h>


/** nxplay */
namespace nxplay
{


/// Utility function to unref a GStreamer object only if the pointer isn't nullptr.
/**
 * This also sets the pointer to nullptr after unref'ing.
 */
template < typename T >
void checked_unref(T* &p_object)
{
	if (p_object != nullptr)
	{
		gst_object_unref(GST_OBJECT(p_object));
		p_object = nullptr;
	}
}


/// Utility class for scoped thread priority changes.
/**
 * The original scheduling and priority settings are restored in the destructor.
 */
class thread_priority_change
{
public:
	/// Constructor. Sets the thread scheduling and priority to the given values.
	/**
	 * Makes an internal copy of the original scheduling and priority first
	 * so the destructor can restore these.
	 *
	 * This is functionally equivalent to using the default constructor and the
	 * set_priority() function.
	 *
	 * @param p_policy Thread policy to use; valid values are the SCHED_* constants
	 *        from the POSIX sched.h header
	 * @param p_priority Thread priority to use
	 */
	explicit thread_priority_change(int const p_policy, int const p_priority);

	/// Default constructor.
	/**
	 * Makes an internal copy of the original scheduling and priority
	 * so the destructor can restore these. Does not modify the scheduling/priority.
	 */
	thread_priority_change();

	/// Destructor. Restores the original scheduling and priority.
	/*
	 * NOTE: This affects the thread that was seen in the constructor, even if
	 * this is somehow invoked from a different thread.
	 */
	~thread_priority_change();

	/// Sets the priority and scheduling of the thread.
	/**
	 * NOTE: This affects the thread that was seen in the constructor, even if
	 * this is called from a different thread.
	 *
	 * @param p_policy Thread policy to use; valid values are the SCHED_* constants
	 *        from the POSIX sched.h header
	 * @param p_priority Thread priority to use
	 */
	void set_priority(int const p_policy, int const p_priority);


	// Make this class noncopyable
	thread_priority_change(thread_priority_change const &) = delete;
	thread_priority_change& operator = (thread_priority_change const &) = delete;


private:
	pthread_t m_thread_id;
	int m_original_policy;
	struct sched_param m_original_sched_param;
};


} // namespace nxplay end


#endif
