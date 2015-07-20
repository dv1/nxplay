/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include "media.hpp"
#include "log.hpp"


namespace nxplay
{


media::media()
	: m_payload(nullptr)
{
}


media::media(media const &p_other)
	: m_uri(p_other.m_uri)
	, m_payload(p_other.m_payload)
{
}


media::media(media && p_other)
	: m_uri(std::move(p_other.m_uri))
	, m_payload(std::move(p_other.m_payload))
{
}


media::media(std::string const &p_uri)
	: m_uri(p_uri)
{
}


media::media(std::string const &p_uri, any const &p_payload)
	: m_uri(p_uri)
	, m_payload(p_payload)
{
}


media::media(std::string const &p_uri, any &&p_payload)
	: m_uri(p_uri)
	, m_payload(std::move(p_payload))
{
}


media& media::operator = (media const &p_other)
{
	m_uri = p_other.m_uri;
	m_payload = p_other.m_payload;
	return *this;
}


media& media::operator = (media &&p_other)
{
	m_uri = std::move(p_other.m_uri);
	m_payload = std::move(p_other.m_payload);
	return *this;
}


std::string const & media::get_uri() const
{
	return m_uri;
}


any& media::get_payload() const
{
	return m_payload;
}


bool is_valid(media const &p_media)
{
	return !(p_media.get_uri().empty());
}


} // namespace nxplay end
