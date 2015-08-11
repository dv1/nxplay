/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_MEDIA_HPP
#define NXPLAY_MEDIA_HPP

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <boost/any.hpp>


/** nxplay */
namespace nxplay
{


/// Class representing a media to be played.
/**
 * The media is represented as a pair of an URI string and a free-form payload.
 * The payload is of the boost::any type. Values of any type can be stored in the
 * payload and extracted again by using the boost::any_cast < T > function.
 * Example;
 *
 *   @code
 *   int mydata = boost::any_cast < int > (media.get_payload());
 *   @endcode
 *
 * This allows callers to associate media with any kind of information they need
 * without having to turn media and the pipeline classes into templates.
 *
 * A media instance can be invalid. This is the case when the URI string is
 * empty. is_valid() checks for that.
 */
class media
{
public:
	/// Default constructor. Creates an invalid media.
	media();
	/// Copy constructor.
	media(media const &p_other);
	/// Move constructor.
	media(media && p_other);
	/// Constructs a media object with the given URI and no payload.
	explicit media(std::string const &p_uri);
	/// Constructs a media object with the given URI and payload. Payload is copied.
	explicit media(std::string const &p_uri, boost::any const &p_payload);
	/// Constructs a media object with the given URI and payload. Payload is moved.
	explicit media(std::string const &p_uri, boost::any &&p_payload);

	/// Copy assignment operator.
	media& operator = (media const &p_other);
	/// Move assignment operator.
	media& operator = (media &&p_other);

	/// Retrieves the URI from the media object.
	std::string const & get_uri() const;
	/** Retrieves the payload from the media object.
	 *
	 *  Note that the return value is an non-const reference
	 *  on purpose. The payload is not considered to be "owned"
	 *  by the object in a sense. The object merely "hosts" it,
	 *  and never actually does anything with its value.
	 *  It is therefore practical to return the non-const
	 *  reference even if the media object itself is const.
	 */
	boost::any& get_payload() const;

private:
	std::string m_uri;
	mutable boost::any m_payload;
};


/// Returns true if media is considered valid
bool is_valid(media const &p_media);


} // namespace nxplay end


#endif
