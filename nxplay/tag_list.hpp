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
	/**
	 * To create a tag_list object with an empty taglist inside, call
	 * tag_list(gst_tag_list_new_empty()) instead.
	 **/
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

	/// Inserts the other tag list into this one.
	/**
	 * The other tag list is left unchanged.
	 * Internally, this inserts by using gst_tag_list_insert().
	 * if the other tag list object's pointer is the same as
	 * the pointer of this object, this function does nothing.
	 *
	 * @param p_other Tag list to merge
	 * @param p_merge_mode Merge mode to use in the internal
	 *        gst_tag_list_merge() call
	 */
	void insert(tag_list const &p_other, GstTagMergeMode const p_merge_mode);


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

/// Adds a raw GValue to the tag list.
/**
 * If the given tag list is empty, it will first fill it with a valid tag list.
 * The value is copied and the copy added; this function does not take ownership
 * over p_value.
 *
 * @param p_tag_list Tag list to add the value to
 * @param p_value Non-NULL pointer to the GValue to be added (will be copied,
 *        and the copy added)
 * @param p_name Name of the tag to add
 * @param p_merge_mode What to do if a tag with the given name already exists
 */
void add_raw_value(tag_list &p_tag_list, GValue const *p_value, std::string const &p_name, GstTagMergeMode const p_merge_mode);
/// Returns true if a tag with the given name exists.
/**
 * @param p_tag_list Tag list to check
 * @param p_name Name of the tag to look for
 * @return true if a tag with the given name exists in the tag list, false if
 *         no such tag is in the list, or if p_tag_list.is_empty() returns true
 */
bool has_value(tag_list const &p_tag_list, std::string const &p_name);

/// Returns the number of values in the list for the given tag.
/**
 * @param p_tag_list Tag list to check
 * @param p_name Name of the tag to count the values of
 * @return Number of values for the given tag, or 0 if p_tag_list.is_empty()
 *         returns true
 */
guint get_num_values_for_tag(tag_list const &p_tag_list, std::string const &p_name);
/// Returns a pointer to the value for a given tag.
/**
 * Since multiple values can be present for the same tag,
 * an additional integer index is needed. Minimum value is
 * 0, maximum value is get_num_values_for_tag(p_tag_list, p_name)-1 .
 *
 * The return value is not copied. Do not try to unref or modify it.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_index Index of the tag value to get
 * @return Const pointer to the value, or NULL if the index is out of bounds,
 *         or no such tag with this name exists, or if p_tag_list.is_empty()
 *         returns true
 */
GValue const * get_raw_value(tag_list const &p_tag_list, std::string const &p_name, const guint p_index);

/// Returns a signed integer value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, gint &p_value, const guint p_index);
/// Returns an unsigned integer value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, guint &p_value, const guint p_index);
/// Returns a 64-bit signed integer value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, gint64 &p_value, const guint p_index);
/// Returns a 64-bit unsigned integer value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, guint64 &p_value, const guint p_index);
/// Returns a single-precision floating point value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, gfloat &p_value, const guint p_index);
/// Returns a double-precision floating point value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, gdouble &p_value, const guint p_index);
/// Returns a pointer value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, gpointer &p_value, const guint p_index);
/// Returns a pointer to a sample value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * The sample value is internally copied, and p_value is set to point to the copy.
 * The result must be freed with gst_sample_unref() once it is no longer needed.
 * See gst_tag_list_get_sample() for details.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, GstSample* &p_value, const guint p_index);
/// Returns a date value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * The date value is internally copied, and p_value is set to point to the copy.
 * The result must be freed with g_date_free() once it is no longer needed.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, GDate* &p_value, const guint p_index);
/// Returns a datetime value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * The datetime value is internally copied, and p_value is set to point to the copy.
 * The result must be freed with gst_date_time_unref() once it is no longer needed.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, GstDateTime* &p_value, const guint p_index);
/// Returns a string value for a given tag.
/**
 * This is a typesafe variant of get_raw_value(), added for convenience.
 *
 * @param p_tag_list Tag list to get the value from
 * @param p_name Name of the tag to get a value of
 * @param p_value Where the tag value will be written to
 * @param p_index Index of the tag value to get
 * @return true if the tag is of this type, false if no such tag exists, or if
 *         p_index is out of bounds, or if this tag is not of this type, or if
 *         p_tag_list.is_empty() returns true
 */
bool get_value(tag_list const &p_tag_list, std::string const p_name, std::string &p_value, const guint p_index);

/// Calculates the tags that are new in p_other in comparison to p_reference.
/**
 * This helps with tag updates that also contain several already-reported tags.
 * Suppose that p_reference contains a TITLE tag, and so does p_other.
 * This function then checks if the TITLE tags in both lists are different, and if
 * so, copies the TITLE tags from p_other into the output tag list. If p_other
 * has a tag that p_reference doesn't, then this tag is added to the output list.
 *
 * If p_other and p_reference have both the same tag, but with a different number
 * of values, the tag values from p_other are added to the output list. This helps
 * with preserving the order of tag values in the list (which would potentially be
 * lost if only the values that are different are extracted from p_other's tag
 * values array).
 *
 * @param p_reference Reference tag list to compare p_other to
 * @param p_other Extra tag list which will be compared to the reference one
 * @return Tag list containing tags which are different between the two lists,
 *         or present in p_other and missing in p_reference, or empty if there
 *         are no differences
 */
tag_list calculate_new_tags(tag_list const &p_reference, tag_list const &p_other);

/// Serializes the tag list to a string
std::string to_string(tag_list const &p_tag_list);
/// Deserializes the tag list from a string
tag_list from_string(std::string const &p_string);


} // namespace nxplay end


#endif
