/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-uri.c - URI handling for the GNOME Virtual File System.

   Copyright (C) 1999 Free Software Foundation

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

   Author: Ettore Perazzoli <ettore@comm2000.it>
*/

/* TODO: %xx syntax for providing any character in the URI.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#define URI_MAGIC_CHAR '#'



#define ALLOCA_SUBSTRING(dest, src, len)		\
        do {						\
	      (dest) = alloca ((len) + 1);		\
	      if ((len) > 0)				\
	              memcpy ((dest), (src), (len));	\
	      dest[(len)] = 0;				\
        } while (0)


GnomeVFSURI *
gnome_vfs_uri_new (const gchar *text_uri)
{
	GnomeVFSMethod *method;
	GnomeVFSURI *uri;
	const gchar *p;
	gchar *method_string;

	g_return_val_if_fail (text_uri != NULL, NULL);
	g_return_val_if_fail (text_uri[0] != 0, NULL);

	/* FIXME: Correct to look for alpha only?  */
	for (p = text_uri; isalpha (*p); p++)
		;

	if (*p == ':') {
		/* Found toplevel method specification.  */
		ALLOCA_SUBSTRING (method_string, text_uri, p - text_uri);
		method = gnome_vfs_method_get (method_string);
		if (method == NULL)
			return NULL;
		p++;
	} else {
		/* No toplevel method specification.  Use "file" as
                   the default.  */
		method_string = "file";
		method = gnome_vfs_method_get (method_string);
		if (method == NULL)
			return NULL;
		p = text_uri;
	}

	uri = NULL;
	while (1) {
		GnomeVFSMethod *new_method;
		GnomeVFSURI *new_uri;
		const gchar *p1, *p2;
		gchar *new_method_string;

		new_uri = g_new (GnomeVFSURI, 1);
		new_uri->method = method;
		new_uri->method_string = g_strdup (method_string);
		new_uri->parent = uri;
		new_uri->ref_count = 1;

		p1 = strchr (p, URI_MAGIC_CHAR);

		if (p1 == NULL) {
			new_uri->text = g_strdup (p);
			uri = new_uri;
			break;
		}

		if (p1 > p)
			new_uri->text = g_strndup (p, p1 - p);
		else
			new_uri->text = NULL;

		p2 = p1 + 1;
		if (*p2 == 0) {
			gnome_vfs_uri_destroy (new_uri);
			return NULL;
		}

		while (*p2 != 0 && *p2 != '/' && *p2 != URI_MAGIC_CHAR)
			p2++;

		ALLOCA_SUBSTRING (new_method_string, p1 + 1, p2 - p1 - 1);
		new_method = gnome_vfs_method_get (new_method_string);

		if (new_method == NULL) {
			/* FIXME: Better error handling/reporting?  */
			gnome_vfs_uri_destroy (new_uri);
			return NULL;
		}

		p = p2;

		method = new_method;
		method_string = new_method_string;
		uri = new_uri;
	}

	return uri;
}


/* Destory an URI element, but not it's parent.  */
static void
destroy_element (GnomeVFSURI *uri)
{
	g_free (uri->text);
	g_free (uri->method_string);
	g_free (uri);
}

GnomeVFSURI *
gnome_vfs_uri_ref (GnomeVFSURI *uri)
{
	GnomeVFSURI *p;

	g_return_val_if_fail (uri != NULL, NULL);

	for (p = uri; p != NULL; p = p->parent)
		p->ref_count++;

	return uri;
}

void
gnome_vfs_uri_unref (GnomeVFSURI *uri)
{
	GnomeVFSURI *p, *parent;

	g_return_if_fail (uri != NULL);

	for (p = uri; p != NULL; p = parent) {
		parent = p->parent;
		p->ref_count--;
		if (p->ref_count == 0)
			destroy_element (p);
	}
}

void
gnome_vfs_uri_destroy (GnomeVFSURI *uri)
{
	g_return_if_fail (uri != NULL);

	while (uri != NULL) {
		GnomeVFSURI *parent;

		parent = uri->parent;
		destroy_element (uri);
		uri = parent;
	}
}


GnomeVFSURI *
gnome_vfs_uri_dup (const GnomeVFSURI *uri)
{
	const GnomeVFSURI *p;
	GnomeVFSURI *new, *child;

	g_return_val_if_fail (uri != NULL, NULL);

	new = NULL;
	child = NULL;
	for (p = uri; p != NULL; p = p->parent) {
		GnomeVFSURI *new_element;

		new_element = g_new (GnomeVFSURI, 1);
		new_element->ref_count = 1;
		new_element->text = g_strdup (p->text);
		new_element->method_string = g_strdup (p->method_string);
		new_element->method = p->method;
		new_element->parent = NULL;

		if (child != NULL)
			child->parent = new_element;
		else
			new = new_element;
			
		child = new_element;
	}

	return new;
}


GnomeVFSURI *
gnome_vfs_uri_append_text (const GnomeVFSURI *uri,
			   ...)
{
	GnomeVFSURI *new;
	gchar *new_text;
	gchar *s, *p;
	va_list args;
	guint current_len, len;

	g_return_val_if_fail (uri != NULL, NULL);

	len = 0;
	va_start (args, uri);
	s = va_arg (args, gchar *);
	while (s != NULL) {
		len += strlen (s);
		s = va_arg (args, gchar *);
	}
	va_end (args);

	current_len = strlen (uri->text);
	new_text = g_malloc (current_len + len + 1);
	memcpy (new_text, uri->text, current_len);
	p = new_text + current_len;

	va_start (args, uri);
	s = va_arg (args, gchar*);
	while (s != NULL) {
		guint l;

		l = strlen (s);
		memcpy (p, s, l);
		p += l;
		s = va_arg (args, gchar *);
	}
	va_end (args);

	*p = 0;

	new = gnome_vfs_uri_dup (uri);
	g_free (new->text);	/* FIXME, this is a waste of time */
	new->text = new_text;
	
	return new;
}


/* FIXME/TODO: Special characters such as `#' in the URI components should be
   replaced.  */
gchar *
gnome_vfs_uri_to_string (const GnomeVFSURI *uri)
{
	const GnomeVFSURI *u;
	guint size, len;
	gchar *s, *p;

	g_return_val_if_fail (uri != NULL, NULL);

	size = 0;
	u = uri;
	do {
		if (u->method_string != NULL)
			size += strlen (u->method_string);
		if (u->text != NULL)
			size += strlen (u->text);
		size += 1;	/* '#' or ':' */
		u = u->parent;
	} while (u != NULL);

	s = g_malloc (size + 1);
	p = s + size;
	*p = 0;

	u = uri;
	do {
		if (u->text != NULL) {
			len = strlen (u->text);
			p -= len;
			memcpy (p, u->text, len);
		}
		if (u->method_string != NULL) {
			if (u->parent == NULL)
				*(--p) = ':';
			len = strlen (u->method_string);
			p -= len;
			memcpy (p, u->method_string, len);
			if (u->parent != NULL)
				*(--p) = '#';
		}
		u = u->parent;
	} while (u != NULL);

	return s;
}


gboolean
gnome_vfs_uri_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	return uri->method->is_local (uri);
}
