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

   Author: Ettore Perazzoli <ettore@gnu.org>

   `split_toplevel_uri()' derived from Midnight Commander code by Norbert
   Warmuth, Miguel de Icaza, Janne Kukonlehto, Dugan Porter, Jakub Jelinek.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

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
	if(dir) {
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
						g_warning("Setting *port_return = 1");
						break;
					case 'r':
						*port_return = 2;
						g_warning("Setting *port_return = 2");
						break;
					case 0:
						*port_return = 0;
						goto done;
					}
				}
			}
		} else {
			*host_return = g_strndup (rest, dir - rest);
		}
	} else {
		*host_return = g_strdup(path);
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
#if 0
/* This causes problems for URI's that may not be relative, but may still not have a leading / */
		if (text[0] != '/') {
			uri->text = g_malloc (len + 2);
			uri->text[0] = '/';
			memcpy (uri->text + 1, text, len);
			uri->text[len + 1] = 0;
		} else {
#endif
			uri->text = g_strndup (text, len);
#if 0
		}
#endif
	}

	gnome_vfs_canonicalize_pathname (uri->text);
}

static const gchar *
get_method_string (const gchar *substring, gchar **method_string)
{
	const gchar *p;
	
	for (p = substring; isalnum (*p) || *p == '+' || *p == '-' || *p == '.'; p++)
		;

	if (*p == ':') {
		/* Found toplevel method specification.  */
		*method_string = g_strndup (substring, p - substring);
		p++;
	} else {
		*method_string = g_strdup ("file");
		p = substring;
	}
	return p;
}

static GnomeVFSURI *
parse_uri_substring (const gchar *substring)
{
	GnomeVFSMethod *method;
	GnomeVFSURI *uri, *new_uri;
	const gchar *p;
	gchar *p1;
	gchar *method_string;

	if (substring == NULL || *substring == '\000')
		return NULL;

	p = get_method_string (substring, &method_string);

	method = gnome_vfs_method_get (method_string);
	if (method == NULL) {
		g_free (method_string);
		return NULL;
	}

	uri = g_new0 (GnomeVFSURI, 1);
	uri->method = method;
	uri->method_string = method_string;
	uri->ref_count = 1;

	p1 = strchr (p, GNOME_VFS_URI_MAGIC_CHR);
	if (p1 == NULL) {
		set_uri_element (uri, p, strlen (p));
		return uri;
	}

	set_uri_element (uri, p, p1 - p);

	new_uri = parse_uri_substring (p1 + 1);
	if (new_uri != NULL)
		new_uri->parent = uri;
	return uri;
}
/**
 * gnome_vfs_uri_new:
 * @text_uri: A string representing a URI.
 * 
 * Create a new URI from @text_uri.
 * 
 * Return value: The new URI.
 **/
GnomeVFSURI *
gnome_vfs_uri_new (const gchar *text_uri)
{
	GnomeVFSMethod *method;
	GnomeVFSTransform *trans;
	GnomeVFSToplevelURI *toplevel;
	GnomeVFSURI *uri, *new_uri;
	const gchar *p, *p1, *p2;
	gchar *method_string;
	gchar *new_uri_string = NULL;

	g_return_val_if_fail (text_uri != NULL, NULL);

	if (text_uri[0] == 0)
		return NULL;

	toplevel = g_new (GnomeVFSToplevelURI, 1);
	toplevel->host_name = NULL;
	toplevel->host_port = 0;
	toplevel->user_name = NULL;
	toplevel->password = NULL;

	uri = (GnomeVFSURI *) toplevel;
	uri->parent = NULL;

	p = get_method_string (text_uri, &method_string);
	trans = gnome_vfs_transform_get (method_string);
	if (trans && trans->transform) {
		GnomeVFSContext *context;

		context = gnome_vfs_context_new ();
		(* trans->transform) (trans, p, &new_uri_string, context);
		g_print ("new_uri_string: %s\n", new_uri_string);
		gnome_vfs_context_unref (context);
		if (new_uri_string != NULL) {
			toplevel->urn = g_strdup (text_uri);
			g_free (method_string);
			p = get_method_string (new_uri_string, &method_string);
		}
	}
	
	method = gnome_vfs_method_get (method_string);
	if (method == NULL) {
		g_free (method_string);
		gnome_vfs_uri_unref (uri);
		g_free (new_uri_string);
		return NULL;
	}

	/* The toplevel URI element is special, as it also contains host/user
           information.  */
	uri->method = method;
	uri->ref_count = 1;
	uri->method_string = method_string;
	
	p1 = strchr (p, GNOME_VFS_URI_MAGIC_CHR);
	if (p1 == NULL) {
		set_uri_element (uri, p, strlen (p));
		g_free (new_uri_string);
		return uri;
	}

	set_uri_element (uri, p, p1 - p);

	p2 = p1 + 1;
		
	if (*p2 == 0) {
		if (uri->ref_count > 0)
			gnome_vfs_uri_unref (uri);
		g_free (new_uri_string);
		return NULL;
	}
	new_uri = parse_uri_substring (p2);

	g_free (new_uri_string);

	if (new_uri != NULL) {
		new_uri->parent = uri;
		return new_uri;
	} else
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

/**
 * gnome_vfs_uri_ref:
 * @uri: A GnomeVFSURI.
 * 
 * Increment @uri's reference count.
 * 
 * Return value: @uri.
 **/
GnomeVFSURI *
gnome_vfs_uri_ref (GnomeVFSURI *uri)
{
	GnomeVFSURI *p;

	g_return_val_if_fail (uri != NULL, NULL);

	for (p = uri; p != NULL; p = p->parent)
		p->ref_count++;

	return uri;
}

/**
 * gnome_vfs_uri_unref:
 * @uri: A GnomeVFSURI.
 * 
 * Decrement @uri's reference count.  If the reference count reaches zero,
 * @uri is destroyed.
 **/
void
gnome_vfs_uri_unref (GnomeVFSURI *uri)
{
	GnomeVFSURI *p, *parent;

	g_return_if_fail (uri != NULL);
	g_return_if_fail (uri->ref_count > 0);

	for (p = uri; p != NULL; p = parent) {
		parent = p->parent;
		g_assert (p->ref_count > 0);
		p->ref_count--;
		if (p->ref_count == 0)
			destroy_element (p);
	}
}


/**
 * gnome_vfs_uri_dup:
 * @uri: A GnomeVFSURI.
 * 
 * Duplicate @uri.
 * 
 * Return value: A pointer to a new URI that is exactly the same as @uri.
 **/
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


/**
 * gnome_vfs_uri_append_append_path:
 * @uri: A GnomeVFSURI.
 * @path: A piece of a URI (ie a fully escaped partial path)
 * 
 * Create a new URI obtained by appending @path to @uri.  This will take care
 * of adding an appropriate directory separator between the end of @uri and
 * the start of @path if necessary.
 * 
 * Return value: The new URI obtained by combining @uri and @path.
 **/
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

	/* FIXME bugzilla.eazel.com 1209: this is just a reminder.  */
	if (strchr (path, '#') != NULL)
		g_warning ("gnome_vfs_uri_append_path() is broken with names containing `#'.");

	uri_string = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	len = strlen (uri_string);
	if (len == 0) {
		g_free (uri_string);
		return gnome_vfs_uri_new (path);
	}

	len--;
	while (uri_string[len] == GNOME_VFS_URI_PATH_CHR && len > 0)
		len--;
	uri_string[len + 1] = '\0';

	while (*path == GNOME_VFS_URI_PATH_CHR)
		path++;

	new_string = g_strconcat (uri_string, GNOME_VFS_URI_PATH_STR, path, NULL);
	new = gnome_vfs_uri_new (new_string);

	g_free (new_string);
	g_free (uri_string);

	return new;
}

/**
 * gnome_vfs_uri_append_file_name:
 * @uri: A GnomeVFSURI.
 * @path: any "regular" file name (can include #, /, etc)
 * 
 * Create a new URI obtained by appending @file_name to @uri.  This will take care
 * of adding an appropriate directory separator between the end of @uri and
 * the start of @file_name if necessary.
 * 
 * Return value: The new URI obtained by combining @uri and @path.
 **/
GnomeVFSURI *
gnome_vfs_uri_append_file_name (const GnomeVFSURI *uri,
				const gchar *file_name)
{
	gchar *escaped_string;
	GnomeVFSURI *new_uri;
	
	escaped_string = gnome_vfs_escape_string (file_name);
	new_uri = gnome_vfs_uri_append_path (uri, escaped_string);
	g_free (escaped_string);
	return new_uri;
}


/**
 * gnome_vfs_uri_to_string:
 * @uri: A GnomeVFSURI.
 * @hide_options: Bitmask specifying what URI elements (e.g. password,
 * user name etc.) should not be represented in the returned string.
 * 
 * Translate @uri into a printable string.  The string will not contain the
 * URI elements specified by @hide_options.
 * 
 * Return value: A malloced printable string representing @uri.
 **/
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
		if (u->text != NULL)
			size += strlen (u->text);

		if (u->parent != NULL
		    || ! (hide_options
			  & GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD)) {
			if (u->method_string != NULL)
				size += strlen (u->method_string);
			size += 1;	/* '#' or ':' */
		}

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
		if(!(hide_options & GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD))
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

		if(!(hide_options & GNOME_VFS_URI_HIDE_HOST_PORT))
			concat_list[count++] = toplevel->host_name;

		if (toplevel->host_port != 0 && !(hide_options & GNOME_VFS_URI_HIDE_HOST_PORT)) {
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

		if (u->method_string != NULL
		    && (u->parent != NULL
			|| ! (hide_options
			      & GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD))) {
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


/**
 * gnome_vfs_uri_is_local:
 * @uri: A GnomeVFSURI.
 * 
 * Check if @uri is a local (native) file system.
 * 
 * Return value: %FALSE if @uri is not a local file system, %TRUE otherwise.
 **/
gboolean
gnome_vfs_uri_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	/* It's illegal to have is_local be NULL in a method.
	 * That's why we fail here. If we decide that it's legal,
	 * then we can change this into an if statement.
	 */
	g_return_val_if_fail (uri->method->is_local != NULL, FALSE);

	return uri->method->is_local (uri->method, uri);
}

/**
 * gnome_vfs_uri_has_parent:
 * @uri: A GnomeVFSURI.
 * 
 * Check if URI has a parent or not.
 * 
 * Return value: %TRUE if @uri has a parent, %FALSE otherwise.
 **/
gboolean
gnome_vfs_uri_has_parent (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	if (uri->parent != NULL)
		return TRUE;

	if (uri->text == NULL)
		return FALSE;

	if (strcmp (uri->text, GNOME_VFS_URI_PATH_STR) == 0)
		return FALSE;

	return TRUE;
}

/**
 * gnome_vfs_uri_get_parent:
 * @uri: A GnomeVFSURI.
 * 
 * Retrieve @uri's parent URI.
 * 
 * Return value: A pointer to @uri's parent URI.
 **/
GnomeVFSURI *
gnome_vfs_uri_get_parent (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, NULL);

	if (uri->text != NULL && uri->text[0] != 0) {
		gchar *p;
		guint len;

		len = strlen (uri->text);
		p = uri->text + len - 1;

		/* Skip trailing slashes  */
		while (p != uri->text && *p == GNOME_VFS_URI_PATH_CHR)
			p--;

		/* Search backwards to the next slash.  */
		while (p != uri->text && *p != GNOME_VFS_URI_PATH_CHR)
			p--;

		/* Get the parent without slashes  */
		while (p > uri->text + 1 && p[-1] == GNOME_VFS_URI_PATH_CHR)
			p--;

		if (p[1] != '\0') {
			GnomeVFSURI *new_uri;
			char *new_uri_text;
			int length;

			/* build a new parent text */
			length = p - uri->text;			
			if (length == 0) {
				new_uri_text = g_strdup (GNOME_VFS_URI_PATH_STR);
			} else {
				new_uri_text = g_malloc (length + 1);
				memcpy (new_uri_text, uri->text, length);
				new_uri_text[length] = '\0';
			}

			/* copy the uri and replace the uri text with the new parent text */
			new_uri = gnome_vfs_uri_dup (uri);
			g_free (new_uri->text);
			new_uri->text = new_uri_text;
			
			return new_uri;
		}
	}

	return gnome_vfs_uri_dup (uri->parent);
}


/**
 * gnome_vfs_uri_get_toplevel:
 * @uri: A GnomeVFSURI.
 * 
 * Retrieve the toplevel URI in @uri.
 * 
 * Return value: A pointer to the toplevel URI object.
 **/
GnomeVFSToplevelURI *
gnome_vfs_uri_get_toplevel (const GnomeVFSURI *uri)
{
	const GnomeVFSURI *p;

	g_return_val_if_fail (uri != NULL, NULL);

	for (p = uri; p->parent != NULL; p = p->parent)
		;

	return (GnomeVFSToplevelURI *) p;
}


/**
 * gnome_vfs_uri_get_host_name:
 * @uri: A GnomeVFSURI.
 * 
 * Retrieve the host name for @uri.
 * 
 * Return value: A string representing the host name.
 **/
const gchar *
gnome_vfs_uri_get_host_name (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->host_name;
}

/**
 * gnome_vfs_uri_get_scheme:
 * @uri: A GnomeVFSURI
 *
 * Retrieve the scheme used for @uri
 *
 * Return value: A string representing the scheme
 **/
const gchar *
gnome_vfs_uri_get_scheme (const GnomeVFSURI *uri)
{
	return uri->method_string;
}

/**
 * gnome_vfs_uri_get_host_port:
 * @uri: A GnomeVFSURI.
 * 
 * Retrieve the host port number in @uri.
 * 
 * Return value: The host port number used by @uri.  If the value is zero, the
 * default port value for the specified toplevel access method is used.
 **/
guint
gnome_vfs_uri_get_host_port (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, 0);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->host_port;
}

/**
 * gnome_vfs_uri_get_user_name:
 * @uri: A GnomeVFSURI.
 * 
 * Retrieve the user name in @uri.
 * 
 * Return value: A string representing the user name in @uri.
 **/
const gchar *
gnome_vfs_uri_get_user_name (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->user_name;
}

/**
 * gnome_vfs_uri_get_password:
 * @uri: A GnomeVFSURI.
 * 
 * Retrieve the password for @uri.
 * 
 * Return value: The password for @uri.
 **/
const gchar *
gnome_vfs_uri_get_password (const GnomeVFSURI *uri)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_val_if_fail (uri != NULL, NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);
	return toplevel->password;
}


/**
 * gnome_vfs_uri_set_host_name:
 * @uri: A GnomeVFSURI.
 * @host_name: A string representing a host name.
 * 
 * Set @host_name as the host name accessed by @uri.
 **/
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

/**
 * gnome_vfs_uri_set_host_port:
 * @uri: A GnomeVFSURI.
 * @host_port: A TCP/IP port number.
 * 
 * Set the host port number in @uri.  If @host_port is zero, the default port
 * for @uri's toplevel access method is used.
 **/
void
gnome_vfs_uri_set_host_port (GnomeVFSURI *uri,
			     guint host_port)
{
	GnomeVFSToplevelURI *toplevel;

	g_return_if_fail (uri != NULL);

	toplevel = gnome_vfs_uri_get_toplevel (uri);

	toplevel->host_port = host_port;
}

/**
 * gnome_vfs_uri_set_user_name:
 * @uri: A GnomeVFSURI.
 * @user_name: A string representing a user name on the host accessed by @uri.
 * 
 * Set @user_name as the user name for @uri.
 **/
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

/**
 * gnome_vfs_uri_set_password:
 * @uri: A GnomeVFSURI.
 * @password: A password string.
 * 
 * Set @password as the password for @uri.
 **/
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

static gboolean
my_streq (const gchar *a,
	  const gchar *b)
{
	if (a == NULL || *a == '\0') {
		return b == NULL || *b == '\0';
	}

	if (a == NULL || b == NULL)
		return FALSE;

	return strcmp (a, b) == 0;
}

static gboolean
compare_elements (const GnomeVFSURI *a,
		  const GnomeVFSURI *b)
{
	if (! my_streq (a->text, b->text)
	    || ! my_streq (a->method_string, b->method_string))
		return FALSE;

	/* The following should not happen, but we make sure anyway.  */
	if (a->method != b->method)
		return FALSE;

	return TRUE;
}

/**
 * gnome_vfs_uri_equal:
 * @a: A GnomeVFSURI.
 * @b: A GnomeVFSURI.
 * 
 * Compare @a and @b.
 * 
 * Return value: %TRUE if @a and @b are equal, %FALSE otherwise.
 **/
gboolean
gnome_vfs_uri_equal (const GnomeVFSURI *a,
		     const GnomeVFSURI *b)
{
	const GnomeVFSToplevelURI *toplevel_a;
	const GnomeVFSToplevelURI *toplevel_b;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	/* First check non-toplevel elements.  */
	while (a->parent != NULL && b->parent != NULL)
		if (! compare_elements (a, b))
			return FALSE;

	/* Now we should be at toplevel for both.  */
	if (a->parent != NULL || b->parent != NULL)
		return FALSE;

	if (! compare_elements (a, b))
		return FALSE;

	toplevel_a = (GnomeVFSToplevelURI *) a;
	toplevel_b = (GnomeVFSToplevelURI *) b;

	/* Finally, compare the extra toplevel members.  */

	if (toplevel_a->host_port != toplevel_b->host_port
	    || ! my_streq (toplevel_a->host_name, toplevel_b->host_name)
	    || ! my_streq (toplevel_a->user_name, toplevel_b->user_name)
	    || ! my_streq (toplevel_a->password, toplevel_b->password))
		return FALSE;

	return TRUE;
}

/**
 * gnome_vfs_uri_is_parent:
 * @possible_parent: A GnomeVFSURI.
 * @possible_child: A GnomeVFSURI.
 * @recursive: a flag to turn recursive check on.
 * 
 * Check if @possible_child is contained by @possible_parent.
 * If @recursive is FALSE, just try the immediate parent directory, else
 * search up through the hierarchy.
 * 
 * Return value: %TRUE if @possible_child is contained in  @possible_child.
 **/
gboolean
gnome_vfs_uri_is_parent (const GnomeVFSURI *possible_parent,
			 const GnomeVFSURI *possible_child,
			 gboolean recursive)
{
	gboolean result;
	GnomeVFSURI *item_parent_uri;
	GnomeVFSURI *item;

	if (!recursive) {
		item_parent_uri = gnome_vfs_uri_get_parent (possible_child);

		if (item_parent_uri == NULL) 
			return FALSE;

		result = gnome_vfs_uri_equal (item_parent_uri, possible_parent);	
		gnome_vfs_uri_unref (item_parent_uri);

		return result;
	}
	
	item = gnome_vfs_uri_dup (possible_child);
	for (;;) {
		item_parent_uri = gnome_vfs_uri_get_parent (item);
		gnome_vfs_uri_unref (item);
		
		if (item_parent_uri == NULL) 
			return FALSE;

		result = gnome_vfs_uri_equal (item_parent_uri, possible_parent);
	
		if (result) {
			gnome_vfs_uri_unref (item_parent_uri);
			break;
		}

		item = item_parent_uri;
	}

	return result;
}


/**
 * gnome_vfs_uri_get_basename:
 * @uri: A GnomeVFSURI
 * 
 * Retrieve base file name for @uri.
 * 
 * Return value: A pointer to the base file name in @uri.  Notice that the
 * pointer points to the name store in @uri, so the name returned must not
 * be modified nor freed.
 **/
const gchar *
gnome_vfs_uri_get_basename (const GnomeVFSURI *uri)
{
	gchar *p;

	g_return_val_if_fail (uri != NULL, NULL);

	if (uri->text == NULL)
		return NULL;

	p = strrchr (uri->text, GNOME_VFS_URI_PATH_CHR);
	if (p == NULL)
		return NULL;

	p++;
	if (*p == '\0')
		return NULL;

	return p;
}

/**
 * gnome_vfs_uri_extract_dirname:
 * @uri: A GnomeVFSURI
 * 
 * Extract the name of the directory in which the file pointed to by @uri is
 * stored as a newly allocated string.  The string will end with a
 * GNOME_VFS_URI_PATH_CHR.
 * 
 * Return value: A pointer to the newly allocated string representing the
 * parent directory.
 **/
gchar *
gnome_vfs_uri_extract_dirname (const GnomeVFSURI *uri)
{
	const gchar *base;

	g_return_val_if_fail (uri != NULL, NULL);

	base = gnome_vfs_uri_get_basename (uri);
	if (base == NULL || base == uri->text)
		return g_strdup (GNOME_VFS_URI_PATH_STR);

	return g_strndup (uri->text, base - uri->text);
}

/**
 * gnome_vfs_uri_extract_short_name:
 * @uri: A GnomeVFSURI
 * 
 * Retrieve base file name for @uri, ignoring any trailing path separators.
 * This matches the XPG definition of basename, but not g_basename. This is
 * often useful when you want the name of something that's pointed to by a
 * uri, and don't care whether the uri has a directory or file form.
 * If @uri points to the root of a domain, returns the host name. If there's
 * no host name, returns GNOME_VFS_URI_PATH_STR.
 * 
 * See also: gnome_vfs_uri_extract_short_path_name.
 * 
 * Return value: A pointer to the newly allocated string representing the
 * unescaped short form of the name.
 **/
gchar *
gnome_vfs_uri_extract_short_name (const GnomeVFSURI *uri)
{
	gchar *escaped_short_path_name, *short_path_name;
	const gchar *host_name;

	escaped_short_path_name = gnome_vfs_uri_extract_short_path_name (uri);
	short_path_name = gnome_vfs_unescape_string_for_display (escaped_short_path_name);
	g_free (escaped_short_path_name);

	host_name = NULL;
	if (short_path_name != NULL
	    && strcmp (short_path_name, GNOME_VFS_URI_PATH_STR) == 0) {
		host_name = gnome_vfs_uri_get_host_name (uri);
	}

	if (host_name == NULL || strlen (host_name) == 0) {
		return short_path_name;
	}

	g_free (short_path_name);
	return g_strdup (host_name);
}

/**
 * gnome_vfs_uri_extract_short_path_name:
 * @uri: A GnomeVFSURI
 * 
 * Retrieve base file name for @uri, ignoring any trailing path separators.
 * This matches the XPG definition of basename, but not g_basename. This is
 * often useful when you want the name of something that's pointed to by a
 * uri, and don't care whether the uri has a directory or file form.
 * If @uri points to the root (including the root of any domain),
 * returns GNOME_VFS_URI_PATH_STR.
 * 
 * See also: gnome_vfs_uri_extract_short_name.
 * 
 * Return value: A pointer to the newly allocated string representing the
 * escaped short form of the name.
 **/
gchar *
gnome_vfs_uri_extract_short_path_name (const GnomeVFSURI *uri)
{
	const gchar *p, *short_name_start, *short_name_end;

	g_return_val_if_fail (uri != NULL, NULL);

	if (uri->text == NULL)
		return NULL;

	/* Search for the last run of non-'/' characters. */
	p = uri->text;
	short_name_start = NULL;
	short_name_end = p;
	do {
		if (*p == '\0' || *p == GNOME_VFS_URI_PATH_CHR) {
			/* While we are in a run of non-separators, short_name_end is NULL. */
			if (short_name_end == NULL)
				short_name_end = p;
		} else {
			/* While we are in a run of separators, short_name_end is not NULL. */
			if (short_name_end != NULL) {
				short_name_start = p;
				short_name_end = NULL;
			}
		}
	} while (*p++ != '\0');
	g_assert (short_name_end != NULL);
	
	/* If we never found a short name, that means that the string is all
	   directory separators. Since it can't be an empty string, that means
	   it points to the root, so "/" is a good result.
	*/
	if (short_name_start == NULL)
		return g_strdup (GNOME_VFS_URI_PATH_STR);

	/* Return a copy of the short name. */
	return g_strndup (short_name_start, short_name_end - short_name_start);
}


/* The following functions are useful for creating URI hash tables.  */

gint
gnome_vfs_uri_hequal (gconstpointer a,
		      gconstpointer b)
{
	return gnome_vfs_uri_equal (a, b);
}

guint
gnome_vfs_uri_hash (gconstpointer p)
{
	const GnomeVFSURI *uri;
	const GnomeVFSURI *uri_p;
	guint hash_value;

#define HASH_STRING(value, string)		\
	if ((string) != NULL)			\
		(value) ^= g_str_hash (string);

#define HASH_NUMBER(value, number)		\
	(value) ^= number;

	uri = (const GnomeVFSURI *) p;
	hash_value = 0;

	for (uri_p = uri; uri_p != NULL; uri_p = uri_p->parent) {
		HASH_STRING (hash_value, uri_p->text);
		HASH_STRING (hash_value, uri_p->method_string);

		if (uri_p->parent != NULL) {
			const GnomeVFSToplevelURI *toplevel;

			toplevel = (const GnomeVFSToplevelURI *) uri_p;

			HASH_STRING (hash_value, toplevel->host_name);
			HASH_NUMBER (hash_value, toplevel->host_port);
			HASH_STRING (hash_value, toplevel->user_name);
			HASH_STRING (hash_value, toplevel->password);
		}
	}

	return hash_value;

#undef HASH_STRING
#undef HASH_NUMBER
}

GList *
gnome_vfs_uri_list_ref (GList *list)
{
	g_list_foreach (list, (GFunc) gnome_vfs_uri_ref, NULL);
	return list;
}

GList *
gnome_vfs_uri_list_unref (GList *list)
{
	g_list_foreach (list, (GFunc) gnome_vfs_uri_unref, NULL);
	return list;
}

GList *
gnome_vfs_uri_list_copy (GList *list)
{
	return g_list_copy (gnome_vfs_uri_list_ref (list));
}

void
gnome_vfs_uri_list_free (GList *list)
{
	g_list_free (gnome_vfs_uri_list_unref (list));
}
