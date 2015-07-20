/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_TAG_LIST_HPP
#define NXPLAY_TAG_LIST_HPP

#include <gst/gst.h>
#include <string>


/** nxplay */
namespace nxplay
{


/// Convenience GstTagList wrapper which unrefs in the destructor.
/**
 * This class is useful for passing around tag lists in a C++-compatible manner.
 */
class tag_list
{
public:
	/// Default constructor. Sets the internal GStreamer tag list pointer to null.
	tag_list();
	/// Copy constructor. Copies by using gst_tag_list_copy() internally.
	tag_list(tag_list const &p_src);
	/// Move constructor.
	/**
	 * Moves by copying the tag list pointer. Does NOT ref/unref
	 * the GStreamer tag lists.
	 */
	tag_list(tag_list &&p_src);
	/// Creates a tag list object, and copies the given pointer.
	/**
	 * IMPORTANT: This does NOT do a deep copy of the given
	 * GStreamer tag list value. Nor does it ref it.
	 * It simply copies the pointer.
	 *
	 * @param p_tag_list Tag list to adopt
	 */
	explicit tag_list(GstTagList *p_tag_list);
	/// Destructor. Unrefs the internal GStreamer tag list if it is non-null.
	~tag_list();

	/// Copy assignment operator. Copies by using gst_tag_list_copy() internally.
	tag_list& operator = (tag_list const &p_src);
	/// Move assignment operator.
	/*
	 * Moves by copying the tag list pointer. Does NOT ref/unref
	 * the GStreamer tag lists.
	 */
	tag_list& operator = (tag_list &&p_src);

	/// Returns the internal GStreamer tag list pointer.
	GstTagList* get_tag_list() const;

	/// Returns true if the tag list is empty or the pointer is null.
	bool is_empty() const;

	/// Merge the other tag list into this one.
	/**
	 * The other tag list is left unchanged.
	 * Internally, this merges by using gst_tag_list_merge().
	 * if the other tag list object's pointer is the same as
	 * the pointer of this object, this function does nothing.
	 *
	 * @param p_other Tag list to merge
	 * @param p_merge_mode Merge mode to use in the internal
	 *        gst_tag_list_merge() call
	 */
	void merge(tag_list const &p_other, GstTagMergeMode const p_merge_mode);


private:
	GstTagList *m_tag_list;
};


/// Equality operator.
/**
 * Returns true if either one of these holds true:
 *
 * 1. GstTagList pointers of both tag list objects are the same
 * 2. An internal gst_tag_list_is_equal() call returns true
 *
 * Returns false if one tag list pointer is non-NULL and the other is NULL,
 * or if gst_tag_list_is_equal() returns false
 */
bool operator == (tag_list const &p_first, tag_list const &p_second);

/// Inequality operator.
inline bool operator != (tag_list const &p_first, tag_list const &p_second)
{
	return !(p_first == p_second);
}

/// Serializes the tag list to a string
std::string to_string(tag_list const &p_tag_list);
/// Deserializes the tag list from a string
tag_list from_string(std::string const &p_string);


} // namespace nxplay end


#endif
