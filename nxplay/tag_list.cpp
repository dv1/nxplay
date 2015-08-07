/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

#include "tag_list.hpp"


namespace nxplay
{


tag_list::tag_list()
	: m_tag_list(nullptr)
{
}


tag_list::tag_list(tag_list const &p_src)
{
	if (p_src.m_tag_list != nullptr)
		m_tag_list = gst_tag_list_copy(p_src.m_tag_list);
	else
		m_tag_list = nullptr;
}


tag_list::tag_list(tag_list &&p_src)
	: m_tag_list(p_src.m_tag_list)
{
	p_src.m_tag_list = nullptr;
}


tag_list::tag_list(GstTagList *p_tag_list)
	: m_tag_list(p_tag_list)
{
}


tag_list::~tag_list()
{
	if (m_tag_list != nullptr)
		gst_tag_list_unref(m_tag_list);
}


tag_list& tag_list::operator = (tag_list const &p_src)
{
	if (m_tag_list != nullptr)
		gst_tag_list_unref(m_tag_list);

	if (p_src.m_tag_list != nullptr)
		m_tag_list = gst_tag_list_copy(p_src.m_tag_list);
	else
		m_tag_list = nullptr;

	return *this;
}


tag_list& tag_list::operator = (tag_list &&p_src)
{
	m_tag_list = p_src.m_tag_list;
	p_src.m_tag_list = nullptr;
	return *this;
}


GstTagList* tag_list::get_tag_list() const
{
	return m_tag_list;
}


bool tag_list::is_empty() const
{
	return (m_tag_list == NULL) || gst_tag_list_is_empty(m_tag_list);
}


void tag_list::insert(tag_list const &p_other, GstTagMergeMode const p_merge_mode)
{
        // Safety check
        if (p_other.get_tag_list() == m_tag_list)
                return;

	if (m_tag_list == NULL)
		m_tag_list = gst_tag_list_new_empty();

	gst_tag_list_insert(m_tag_list, p_other.get_tag_list(), p_merge_mode);
}


bool operator == (tag_list const &p_first, tag_list const &p_second)
{
	if (p_first.get_tag_list() == p_second.get_tag_list())
		return true;
	else if ((p_first.get_tag_list() != NULL) && (p_second.get_tag_list() == NULL))
		return false;
	else if ((p_first.get_tag_list() == NULL) && (p_second.get_tag_list() != NULL))
		return false;
	else
		return gst_tag_list_is_equal(p_first.get_tag_list(), p_second.get_tag_list());
}


void add_raw_value(tag_list &p_tag_list, GValue const *p_value, std::string const &p_name, GstTagMergeMode const p_merge_mode)
{
	g_assert(p_value != NULL);
	g_assert(!p_name.empty());

	if (p_tag_list.is_empty())
		p_tag_list = tag_list(gst_tag_list_new_empty());

	gst_tag_list_add_value(p_tag_list.get_tag_list(), p_merge_mode, p_name.c_str(), p_value);
}


bool has_value(tag_list const &p_tag_list, std::string const &p_name)
{
	g_assert(!p_name.empty());
	return p_tag_list.is_empty() ? false : gst_tag_list_get_value_index(p_tag_list.get_tag_list(), p_name.c_str(), 0) != NULL;
}


guint get_num_values_for_tag(tag_list const &p_tag_list, std::string const &p_name)
{
	g_assert(!p_name.empty());
	return p_tag_list.is_empty() ? 0 : gst_tag_list_get_tag_size(p_tag_list.get_tag_list(), p_name.c_str());
}


GValue const * get_raw_value(tag_list const &p_tag_list, std::string const &p_name, const guint p_index)
{
	g_assert(!p_name.empty());
	return p_tag_list.is_empty() ? NULL : gst_tag_list_get_value_index(p_tag_list.get_tag_list(), p_name.c_str(), p_index);
}


#define GET_VALUE_IMPL(TYPE) \
bool get_value(tag_list const &p_tag_list, std::string const &p_name, g ## TYPE &p_value, const guint p_index) \
{ \
	g_assert(!p_name.empty()); \
	return p_tag_list.is_empty() ? false : gst_tag_list_get_ ## TYPE ## _index(p_tag_list.get_tag_list(), p_name.c_str(), p_index, &p_value); \
}


GET_VALUE_IMPL(int)
GET_VALUE_IMPL(uint)
GET_VALUE_IMPL(int64)
GET_VALUE_IMPL(uint64)
GET_VALUE_IMPL(float)
GET_VALUE_IMPL(double)
GET_VALUE_IMPL(pointer)


bool get_value(tag_list const &p_tag_list, std::string const &p_name, GstSample* &p_value, const guint p_index)
{
	g_assert(!p_name.empty());
	return p_tag_list.is_empty() ? false : gst_tag_list_get_sample_index(p_tag_list.get_tag_list(), p_name.c_str(), p_index, &p_value);
}


bool get_value(tag_list const &p_tag_list, std::string const &p_name, GDate* &p_value, const guint p_index)
{
	g_assert(!p_name.empty());
	return p_tag_list.is_empty() ? false : gst_tag_list_get_date_index(p_tag_list.get_tag_list(), p_name.c_str(), p_index, &p_value);
}


bool get_value(tag_list const &p_tag_list, std::string const &p_name, GstDateTime* &p_value, const guint p_index)
{
	g_assert(!p_name.empty());
	return p_tag_list.is_empty() ? false : gst_tag_list_get_date_time_index(p_tag_list.get_tag_list(), p_name.c_str(), p_index, &p_value);
}


bool get_value(tag_list const &p_tag_list, std::string const &p_name, std::string &p_value, const guint p_index)
{
	g_assert(!p_name.empty());

	if (p_tag_list.is_empty())
		return false;

	gchar const *str;
	gboolean ret = gst_tag_list_peek_string_index(p_tag_list.get_tag_list(), p_name.c_str(), p_index, &str);
	if (!ret)
		return false;

	p_value = str;

	return true;
}


tag_list calculate_new_tags(tag_list const &p_reference, tag_list const &p_other)
{
	tag_list result;
	guint num_tags;

	if (p_other.is_empty())
		return result;

	num_tags = gst_tag_list_n_tags(p_other.get_tag_list());
	for (guint num = 0; num < num_tags; ++num)
	{
		std::string name(gst_tag_list_nth_tag_name(p_other.get_tag_list(), num));
		guint num_tag_values_in_other = get_num_values_for_tag(p_other, name);

		// If the reference list already has this tag, check if the values differ.
		// If it doesn't have this tag yet, it is an addition, so add the tag to
		// the result right away.
		if (has_value(p_reference, name))
		{
			guint num_tag_values_in_reference = get_num_values_for_tag(p_reference, name);

			// If the number of tag values differ, add the other's values to
			// the result, assuming a change (an addition).
			// If the numbers are the same, compare each value pair.
			if (num_tag_values_in_other == num_tag_values_in_reference)
			{
				bool equal = true;

				for (guint index = 0; index < num_tag_values_in_other; ++index)
				{
					GValue const *other_value = get_raw_value(p_other, name, index);
					GValue const *reference_value = get_raw_value(p_reference, name, index);
					if (gst_value_compare(other_value, reference_value) != GST_VALUE_EQUAL)
					{
						equal = false;
						break;
					}
				}

				if (equal)
					continue;
			}
		}

		for (guint index = 0; index < num_tag_values_in_other; ++index)
			add_raw_value(result, get_raw_value(p_other, name, index), name, GST_TAG_MERGE_APPEND);
	}

	return result;
}


std::string to_string(tag_list const &p_tag_list)
{
	gchar *cstr = gst_tag_list_to_string(p_tag_list.get_tag_list());
	if (cstr == nullptr)
		return "";

	std::string str(cstr);
	g_free(cstr);
	return str;
}


tag_list from_string(std::string const &p_string)
{
	GstTagList *tag_list_ = gst_tag_list_new_from_string(p_string.c_str());
	if (tag_list_ == nullptr)
		return tag_list();
	else
		return tag_list(tag_list_);
}


} // namespace nxplay end
