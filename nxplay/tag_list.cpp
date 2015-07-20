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


void tag_list::merge(tag_list const &p_other, GstTagMergeMode const p_merge_mode)
{
        // Safety check
        if (p_other.get_tag_list() == m_tag_list)
                return;

	GstTagList *new_tag_list = gst_tag_list_merge(m_tag_list, p_other.get_tag_list(), p_merge_mode);

        // Discard the old tag list
	if (m_tag_list != NULL)
		gst_tag_list_unref(m_tag_list);

        // Adopt the new, merged tag list
	m_tag_list = new_tag_list;
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
