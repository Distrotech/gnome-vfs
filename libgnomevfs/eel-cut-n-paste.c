/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-cut-n-paste.c - convenience code stolen from eel

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

#include <config.h>
#include "eel-cut-n-paste.h"

#include "gnome-vfs-helpers.h"
#include <ctype.h>
#include <errno.h>
#include <glib/gmessages.h>
#include <glib/gstrfuncs.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

int
stolen_strcasecmp (const char *string_a, const char *string_b)
{
	/* FIXME bugzilla.eazel.com 5450: Maybe we need to make this
	 * treat 'NULL < ""', or have a flavor that does that. If we
	 * didn't have code that already relies on 'NULL == ""', I
	 * would change it right now.
	 */
	return g_ascii_strcasecmp (string_a == NULL ? "" : string_a,
				   string_b == NULL ? "" : string_b);
}

int
stolen_strcmp_case_breaks_ties (const char *string_a, const char *string_b)
{
	int casecmp_result;

	/* FIXME bugzilla.eazel.com 5450: Maybe we need to make this
	 * treat 'NULL < ""', or have a flavor that does that. If we
	 * didn't have code that already relies on 'NULL == ""', I
	 * would change it right now.
	 */
	casecmp_result = stolen_strcasecmp (string_a, string_b);
	if (casecmp_result != 0) {
		return casecmp_result;
	}
	return stolen_strcmp (string_a, string_b);
}

int
stolen_strcoll (const char *string_a, const char *string_b)
{
	const char *locale;
	int result;
	
	/* FIXME bugzilla.eazel.com 5450: Maybe we need to make this
	 * treat 'NULL < ""', or have a flavor that does that. If we
	 * didn't have code that already relies on 'NULL == ""', I
	 * would change it right now.
	 */

	locale = setlocale (LC_COLLATE, NULL);
	
	if (locale == NULL || strcmp (locale, "C") == 0 || strcmp (locale, "POSIX") == 0) {
		/* If locale is NULL or default "C" or "POSIX" use eel sorting */
		return stolen_strcmp_case_breaks_ties (string_a, string_b);
	} else {
		/* Use locale-specific collated sorting */
		result = strcoll (string_a == NULL ? "" : string_a,
				  string_b == NULL ? "" : string_b);
		if (result != 0) {
			return result;
		}
		return stolen_strcmp (string_a, string_b);
	}
}

void
stolen_xml_remove_node (xmlNodePtr node)
{
	g_return_if_fail (node != NULL);
	g_return_if_fail (node->doc != NULL);
	g_return_if_fail (node->parent != NULL);
	g_return_if_fail (node->doc->xmlRootNode != node);

	if (node->prev == NULL) {
		g_assert (node->parent->xmlChildrenNode == node);
		node->parent->xmlChildrenNode = node->next;
	} else {
		g_assert (node->parent->xmlChildrenNode != node);
		node->prev->next = node->next;
	}

	if (node->next == NULL) {
		g_assert (node->parent->last == node);
		node->parent->last = node->prev;
	} else {
		g_assert (node->parent->last != node);
		node->next->prev = node->prev;
	}

	node->doc = NULL;
	node->parent = NULL;
	node->next = NULL;
	node->prev = NULL;
}

gboolean
stolen_str_has_prefix (const char *haystack, const char *needle)
{
	const char *h, *n;

	/* Eat one character at a time. */
	h = haystack == NULL ? "" : haystack;
	n = needle == NULL ? "" : needle;
	do {
		if (*n == '\0') {
			return TRUE;
		}
		if (*h == '\0') {
			return FALSE;
		}
	} while (*h++ == *n++);
	return FALSE;
}

gboolean
stolen_istr_has_prefix (const char *haystack, const char *needle)
{
	const char *h, *n;
	char hc, nc;

	/* Eat one character at a time. */
	h = haystack == NULL ? "" : haystack;
	n = needle == NULL ? "" : needle;
	do {
		if (*n == '\0') {
			return TRUE;
		}
		if (*h == '\0') {
			return FALSE;
		}
		hc = *h++;
		nc = *n++;
		hc = g_ascii_tolower (hc);
		nc = g_ascii_tolower (nc);
	} while (hc == nc);
	return FALSE;
}




char *
stolen_str_strip_trailing_chr (const char *source, char remove_this)
{
	const char *end;
	
        if (source == NULL) {
		return NULL;
	}

	for (end = source + strlen (source); end != source; end--) {
		if (end[-1] != remove_this) {
			break;
		}
	}
	
        return g_strndup (source, end - source);
}

int
stolen_strcmp (const char *string_a, const char *string_b)
{
	return strcmp (string_a == NULL ? "" : string_a,
		       string_b == NULL ? "" : string_b);
}

gboolean
stolen_str_is_empty (const char *string_or_null)
{
	return stolen_strcmp (string_or_null, NULL) == 0;
}

xmlNodePtr
stolen_xml_get_root_children (xmlDocPtr document)
{
	return stolen_xml_get_children (xmlDocGetRootElement (document));
}
xmlNodePtr
stolen_xml_get_children (xmlNodePtr parent)
{
	if (parent == NULL) {
		return NULL;
	}
	return parent->xmlChildrenNode;
}

GList *
stolen_xml_get_property_for_children (xmlNodePtr parent,
					const char *child_name,
					const char *property_name)
{
	GList *properties;
	xmlNode *child;
	xmlChar *property;
	
	properties = NULL;
	
	for (child = stolen_xml_get_children (parent);
	     child != NULL;
	     child = child->next) {
		if (strcmp (child->name, child_name) == 0) {
			property = xmlGetProp (child, property_name);
			if (property != NULL) {
				properties = g_list_prepend (properties,
							     g_strdup (property));
				xmlFree (property);
			}
		}
	}

	/* Reverse so you get them in the same order as the XML file. */
	return g_list_reverse (properties);
}
