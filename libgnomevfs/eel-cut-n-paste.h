/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-cut-n-paste.h - convenience code stolen from eel

   Copyright (C) 1999, 2000 Eazel, Inc.
   Copyright (C) 2001 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: Darin Adler <darin@eazel.com>
	    Pavel Cisler <pavel@eazel.com>
	    Mike Fleming  <mfleming@eazel.com>
            John Sullivan <sullivan@eazel.com>
	    Seth Nickell <snickell@stanford.edu>
*/

#ifndef EEL_CUT_N_PASTE_H
#define EEL_CUT_N_PASTE_H

#include <glib/glist.h>
#include <libxml/tree.h>

int stolen_strcasecmp (const char *string_a, const char *string_b);
int stolen_strcmp_case_breaks_ties (const char *string_a, const char *string_b);
int stolen_strcoll (const char *string_a, const char *string_b);
int stolen_strcmp (const char *string_a, const char *string_b);
char * stolen_str_strip_trailing_chr (const char *source, char remove_this);
void stolen_xml_remove_node (xmlNodePtr node);
gboolean stolen_str_has_prefix (const char *haystack, const char *needle);
gboolean stolen_istr_has_prefix (const char *haystack, const char *needle);
gboolean stolen_str_is_empty (const char *string_or_null);
xmlNodePtr stolen_xml_get_root_children (xmlDocPtr document);
xmlNodePtr stolen_xml_get_children (xmlNodePtr parent);
GList * stolen_xml_get_property_for_children (xmlNodePtr parent,
					      const char *child_name,
					      const char *property_name);

#endif
