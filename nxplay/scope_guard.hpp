/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#ifndef NXPLAY_SCOPE_GUARD_HPP
#define NXPLAY_SCOPE_GUARD_HPP


namespace nxplay
{


/// Utility class to make use of RAII in a generic way.
/**
 * It does so by accepting a function object that is then called
 * in the destructor unless unguard() is called prior to destruction.
 * Typically, this class is not used directly. Use make_scope_guard()
 * instead.
 */
template < typename RollbackFunc >
class scope_guard
{
public:
	/** @cond HIDDEN_SYMBOLS */

	typedef scope_guard < RollbackFunc > self;


	explicit scope_guard(RollbackFunc const &p_rollback_func)
		: m_rollback_func(p_rollback_func)
		, m_guarded(true)
	{
	}

	~scope_guard()
	{
		if (m_guarded)
			m_rollback_func();
	}

	scope_guard(self &&p_other)
		: m_rollback_func(std::move(p_other.m_rollback_func))
		, m_guarded(p_other.m_guarded)
	{
		p_other.m_guarded = false;
	}

	self& operator = (self &&p_other)
	{
		m_rollback_func = std::move(p_other.m_rollback_func);
		m_guarded = p_other.m_guarded;
		p_other.m_guarded = false;
		return *this;
	}


	void unguard()
	{
		m_guarded = false;
	}


	scope_guard(self const &) = delete;
	self& operator = (self const &) = delete;


private:
	RollbackFunc m_rollback_func;
	bool m_guarded;

	/** @endcond */
};


/// Utility function to create a "scope guard", a generic RAII adapter.
/**
 * The scope guard is actually a class which calls a custom defined function
 * in its destructor unless unguard() was called earlier. This makes it
 * possible to do arbitrary cleanup procedures until a system is considered
 * initialized. This is particulary useful in conjunction with lambda
 * expressions.
 *
 * Example:
 *
 *   @code
 *   {
 *     device *d = nullptr;
 *     auto guard = make_scope_guard([&]() {
 *       if (d != nullptr)
 *         shutdown_device(d);
 *     });
 *
 *     d = init_device();
 *
 *     if (!foo(d))
 *       return -1;
 *
 *     if (bar(d))
 *       return -2;
 *
 *     guard.unguard();
 *   }
 *   @endcode
 *
 * This initializes the device. If foo() or bar() fail, shutdown_device() is
 * automatically called upon exiting the scope. If however the initializations
 * all succeed, it is time to disable this safety mechanism by calling unguard().
 * After the unguard() call, the scope guard won't call the function anymore.
 */
template < typename RollbackFunc >
scope_guard < RollbackFunc > make_scope_guard(RollbackFunc const &p_rollback_func)
{
	return scope_guard < RollbackFunc > (p_rollback_func);
}


} // namespace nxplay end


#endif
