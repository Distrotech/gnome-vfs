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

   `split_toplevel_uri()' derived from Midnight Commander code by Norbert
   Warmuth, Miguel de Icaza, Janne Kukonlehto, Dugan Porter, Jakub Jelinek.  */

/* TODO: %xx syntax for providing any character in the URI.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


#define ALLOCA_SUBSTRING(dest, src, len)		\
        do {						\
	      (dest) = alloca ((len) + 1);		\
	      if ((len) > 0)				\
	              memcpy ((dest), (src), (len));	\
	      dest[(len)] = 0;				\
        } while (0)


/* Extract the hostname and username from the path of length `path_len' pointed
   by `path'.  The path is in the form: [user@]hostname:port/remote-dir, e.g.:

       sunsite.unc.edu/pub/linux
       miguel@sphinx.nuclecu.unam.mx/c/nc
       tsx-11.mit.edu:8192/
       joe@foo.edu:11321/private
       joe:password@foo.se

*/

static gchar *
split_toplevel_uri (const gchar *path, guint path_len,
		    gchar **host_return, gchar **user_return,
		    guint *port_return, gchar **password_return)
{
	const gchar *dir, *colon, *at, *rest;
	const gchar *path_end;
	gchar *retval;

	*host_return = NULL;
	*port_return = 0;
	*user_return = NULL;
	*password_return = NULL;
	retval = NULL;

	if (path_len == 0)
		return retval;

	path_end = path + path_len;
    
	/* Locate path component.  */
	dir = memchr (path, GNOME_VFS_URI_PATH_CHR, path_len);
	if (dir != NULL) {
		retval = g_strndup (dir, path_len - (dir - path));
		if (dir != path)
			at = memchr (path, '@', dir - path);
		else
			at = NULL;
	} else {
		retval = g_strdup (GNOME_VFS_URI_PATH_STR);
		at = strchr (path, '@');
	}

	/* Check for username/password.  */
	if (at != NULL && at != path) {
		const gchar *p;

		p = memchr (path, ':', at - path );
		if (p != NULL && at - p > 1) {
			*password_return = g_strndup (p + 1, at - p - 1);
			if (p != path)
				*user_return = g_strndup (path, p - path);
		} else {
			*user_return = g_strndup (path, at - path);
		}

		if (path_end == at + 1)
			rest = at;
		else
			rest = at + 1;
	} else {
		rest = path;
	}

	/* Check if the host comes with a port spec, if so, chop it.  */
	colon = memchr (rest, ':', dir - rest);
	if (colon != NULL && colon != dir - 1) {
		*host_return = g_strndup (rest, colon - rest);

		if (sscanf (colon + 1, "%d", port_return) == 1) {
			if (*port_return > 0xffff)
				*port_return = 0;
		} else {
			while (1) {
				colon++;
				switch(*colon) {
				case 'C':
					*port_return = 1;
					break;
				case 'r':
					*port_return = 2;
					break;
				case 0:
					goto done;
				}
			}
		}
	} else {
		*host_return = g_strndup (rest, dir - rest);
	}

 done:
	return retval;
}


static void
set_uri_element (GnomeVFSURI *uri,
		 const gchar *text,
		 guint len)
{
	if (text == NULL || len == 0) {
		uri->text = NULL;
		return;
	}

	if (uri->parent == NULL && text[0] == '/' && text[1] == '/') {
		GnomeVFSToplevelURI *toplevel;

		toplevel = (GnomeVFSToplevelURI *) uri;
		uri->text = split_toplevel_uri (text + 2, len - 2,
						&toplevel->host_name,
						&toplevel->user_name,
						&toplevel->host_port,
						&toplevel->password);
	} else {
		if (text[0] != '/') {
			uri->text = g_malloc (len + 2);
			uri->text[0] = '/';
			memcpy (uri->text + 1, text, len);
			uri->text[len + 1] = 0;
		} else {
			uri->text = g_strndup (text, len);
		}
	}

	gnome_vfs_canonicalize_pathname (uri->text);
}

GnomeVFSURI *
gnome_vfs_uri_new (const gchar *text_uri)
{
	GnomeVFSMethod *method;
	GnomeVFSToplevelURI *toplevel;
	GnomeVFSURI *uri, *new_uri;
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

	/* The toplevel URI element is special, as it also contains host/user
           information.  */
	toplevel = g_new (GnomeVFSToplevelURI, 1);
	toplevel->host_name = NULL;
	toplevel->host_port = 0;
	toplevel->user_name = NULL;
	toplevel->password = NULL;

	new_uri = (GnomeVFSURI *) toplevel;
	new_uri->parent = NULL;

	while (1) {
		GnomeVFSMethod *new_method;
		const gchar *p1, *p2;
		gchar *new_method_string;

		new_uri->method = method;
		new_uri->method_string = g_strdup (method_string);
		new_uri->ref_count = 1;

		p1 = strchr (p, GNOME_VFS_URI_MAGIC_CHR);
		if (p1 == NULL) {
			set_uri_element (new_uri, p, strlen (p));
			uri = new_uri;
			break;
		}

		set_uri_element (new_uri, p, p1 - p);

		p2 = p1 + 1;
		if (*p2 == 0) {
			gnome_vfs_uri_destroy (new_uri);
			return NULL;
		}

		while (*p2 != 0 && *p2 != '/' && *p2 != GNOME_VFS_URI_MAGIC_CHR)
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

		new_uri = g_new (GnomeVFSURI, 1);
		new_uri->parent = uri;
	}

	return uri;
}


/* Destroy an URI element, but not its parent.  */
static void
destroy_element (GnomeVFSURI *uri)
{
	g_free (uri->text);
	g_free (uri->method_string);

	if (uri->parent == NULL) {
		GnomeVFSToplevelURI *toplevel;

		toplevel = (GnomeVFSToplevelURI *) uri;
		g_free (toplevel->host_name);
		g_free (toplevel->user_name);
		g_free (toplevel->password);
	}

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

	if (uri == NULL)
		return NULL;

	new = NULL;
	child = NULL;
	for (p = uri; p != NULL; p = p->parent) {
		GnomeVFSURI *new_element;

		if (p->parent == NULL) {
			GnomeVFSToplevelURI *toplevel;
			GnomeVFSToplevelURI *new_toplevel;

			toplevel = (GnomeVFSToplevelURI *) p;
			new_toplevel = g_new (GnomeVFSToplevelURI, 1);

			new_toplevel->host_name = g_strdup (toplevel->host_name);
			new_toplevel->host_port = toplevel->host_port;
			new_toplevel->user_name = g_strdup (toplevel->user_name);
			new_toplevel->password = g_strdup (toplevel->password);

			new_element = (GnomeVFSURI *) new_toplevel;
		} else {
			new_element = g_new (GnomeVFSURI, 1);
		}

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


/* FIXME this must be implemented in a much smarter way.  The most important
   issue is that a `#' in `path' will break things rather badly.  */

GnomeVFSURI *
gnome_vfs_uri_append_path (const GnomeVFSURI *uri,
			   const gchar *path)
{
	gchar *uri_string;
	GnomeVFSURI *new;
	gchar *new_string;
	guint len;

	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (path != NULL, NULL);

	/* FIXME this is just a reminder.  */
	if (strchr (path, '#') != NULL)
		g_warning ("gnome_vfs_uri_append_path() is broken with names containing `#'.");

	uri_string = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	len = strlen (uri_string);
	if (len == 0)
		return gnome_vfs_uri_new ("");

	len--;
	while (uri_string[len] == '/' && len > 0)
		len--;
	uri_string[len + 1] = '\0';

	while (*path == '/')
		path++;

	new_string = g_strconcat (uri_string, "/", path, NULL);
	new = gnome_vfs_uri_new (new_string);
	g_free (new_string);

	return new;
}


/* FIXME/TODO: Special characters such as `#' in the URI components should be
   replaced.  */
gchar *
gnome_vfs_uri_to_string (const GnomeVFSURI *uri,
			 GnomeVFSURIHideOptions hide_options)
{
	gchar *toplevel_info;
	const GnomeVFSURI *u;
	const GnomeVFSToplevelURI *toplevel;
	guint size, len;
	gchar *s, *p;

	g_return_val_if_fail (uri != NULL, NULL);

	size = 0;
	u = uri;
	while (1) {
		if (u->method_string != NULL)
			size += strlen (u->method_string);
		if (u->text != NULL)
			size += strlen (u->text);
		size += 1;	/* '#' or ':' */

		if (u->parent == NULL)
			break;
		u = u->parent;
	}

	toplevel = (GnomeVFSToplevelURI *) u;
	if (toplevel->host_name != NULL) {
		gchar *concat_list[9];
		gchar *port_string;
		guint count;

		count = 0;
		concat_list[count++] = "//";

		if (toplevel->user_name != NULL
		    && ! (hide_options & GNOME_VFS_URI_HIDE_USER_NAME)) {
			concat_list[count++] = toplevel->user_name;
			if (toplevel->password != NULL
			    && ! (hide_options & GNOME_VFS_URI_HIDE_PASSWORD)) {
				concat_list[count++] = ":";
				concat_list[count++] = toplevel->password;
			}
			concat_list[count++] = "@";
		}

		concat_list[count++] = toplevel->host_name;

		if (toplevel->host_port != 0) {
			port_string = g_strdup_printf ("%d",
						       toplevel->host_port);
			concat_list[count++] = ":";
			concat_list[count++] = port_string;
		} else {
			port_string = NULL;
		}

		concat_list[count] = NULL;

		toplevel_info = g_strjoinv (NULL, concat_list);
		size += strlen (toplevel_info);

		g_free (port_string);
	} else {
		toplevel_info = NULL;
	}

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

		if (u->parent == NULL && toplevel_info != NULL) {
			len = strlen (toplevel_info);
			p -= len;
			memcpy (p, toplevel_info, len);
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

	g_free (toplevel_info);

	return s;
}


gboolean
gnome_vfs_uri_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	return uri->method->is_local (uri);
}

/* FIXME Maybe we have to make more definite decisions on what URLs have to
   look like here.  */

gboolean
gnome_vfs_uri_has_parent (const GnomeVFSURI *uri)
{
	gchar *p;

	g_return_val_if_fail (uri != NULL, FALSE);

	if (uri->parent != NULL)
		return TRUE;

	if (uri->text == NULL)
		return FALSE;

	if (strcmp (uri->text, "/") == 0)
		return FALSE;

	return TRUE;
}

GnomeVFSURI *
gnome_vfs_uri_get_parent   (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, NULL);

	if (uri->text != NULL && uri->text[0] != 0) {
		gchar *p;
		guint len;

		len = strlen (uri->text);
		p = uri->text + len - 1;

		/* Skip trailing slash.  */
		if (*p == '/' && p != uri->text)
			p--;

		/* Search backwards for next slash.  URIs are normalized, so
                   the next slash we find cannot be a trailing one.  */
		while (p != uri->text && *p != '/')
			p--;

		if (p[1] != '\0') {
			GnomeVFSURI *new;

			/* The following is not extremely efficient, but quite
			   safe in case we change the internals.  */

			new = gnome_vfs_uri_dup (uri);
			g_free (new->text);

			if (p == uri->text) {
				new->text = g_strdup ("/");
			} else {
				new->text = g_malloc (p - uri->text + 2);
				memcpy (new->text, uri->text, p - uri->text);
				new->text[p - uri->text] = '/';
				new->text[p - uri->text + 1] = '\0';
			}

			return new;
		}
	}

	return gnome_vfs_uri_dup (uri->parent);
}


GnomeVFSToplevelURI *
gnome_vfs_uri_get_toplevel (const GnomeVFSURI *uri)
{
	const GnomeVFSURI *p;

	g_return_val_if_fail (uri != NULL, NULL);

	for (p = uri; p->parent != NULL; p = p->parent)
		;

	return (GnomeVFSToplevelURI *) p;
}


const gchar *
gnome_vfs_uri_get_host_name (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->host_name;
}

guint
gnome_vfs_uri_get_host_port (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, 0);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->host_port;
}

const gchar *
gnome_vfs_uri_get_user_name (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->user_name;
}

const gchar *
gnome_vfs_uri_get_password (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->password;
}


void
gnome_vfs_uri_set_host_name (GnomeVFSURI *uri,
			     const gchar *host_name)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_if_fail (uri != NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);

	if (toplevel->host_name != NULL)
		g_free (toplevel->host_name);
	toplevel->host_name = g_strdup (host_name);
}

void
gnome_vfs_uri_set_host_port (GnomeVFSURI *uri,
			     guint host_port)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_if_fail (uri != NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);

	toplevel->host_port = host_port;
}

void
gnome_vfs_uri_set_user_name (GnomeVFSURI *uri,
			     const gchar *user_name)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_if_fail (uri != NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);

	if (toplevel->user_name != NULL)
		g_free (toplevel->user_name);
	toplevel->host_name = g_strdup (user_name);
}

void
gnome_vfs_uri_set_password (GnomeVFSURI *uri,
			    const gchar *password)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_if_fail (uri != NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);

	if (toplevel->password != NULL)
		g_free (toplevel->password);
	toplevel->host_name = g_strdup (password);
}
