/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-utils.c - Private utility functions for the GNOME Virtual
   File System.

   Copyright (C) 1999 Free Software Foundation
   Copyright (C) 2000 Eazel, Inc.

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

   Authors: Ettore Perazzoli <ettore@comm2000.it>
   	    John Sullivan <sullivan@eazel.com> 
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"
#include <string.h>
#include <ctype.h>


gchar*
gnome_vfs_format_file_size_for_display (GnomeVFSFileSize bytes)
{
	if (bytes < (GnomeVFSFileSize) 1e3) {
		if (bytes == 1)
			return g_strdup (_("1 byte"));
		else
			return g_strdup_printf (_("%u bytes"),
						       (guint) bytes);
	} else {
		gdouble displayed_size;

		if (bytes < (GnomeVFSFileSize) 1e6) {
			displayed_size = (gdouble) bytes / 1.0e3;
			return g_strdup_printf (_("%.1fK"),
						       displayed_size);
		} else if (bytes < (GnomeVFSFileSize) 1e9) {
			displayed_size = (gdouble) bytes / 1.0e6;
			return g_strdup_printf (_("%.1fM"),
						       displayed_size);
		} else {
			displayed_size = (gdouble) bytes / 1.0e9;
			return g_strdup_printf (_("%.1fG"),
						       displayed_size);
		}
	}
}

/*  Below modified from libwww HTEscape.c */

#define HEX_ESCAPE '%'

/*  Escape undesirable characters using %
 *  -------------------------------------
 *
 * This function takes a pointer to a string in which
 * some characters may be unacceptable unescaped.
 * It returns a string which has these characters
 * represented by a '%' character followed by two hex digits.
 *
 * This routine returns a g_malloced string.
 */

typedef enum {
	UNSAFE_ALL        = 0x1,  /* Escape all unsafe characters   */
	UNSAFE_ALLOW_PLUS = 0x2,  /* Allows '+'  */
	UNSAFE_PATH       = 0x4,  /* Allows '/' and '?' and '&' and '='  */
	UNSAFE_DOS_PATH   = 0x8,  /* Allows '/' and '?' and '&' and '=' and ':' */
	UNSAFE_HOST       = 0x10, /* Allows '/' and ':' and '@' */
	UNSAFE_SLASHES    = 0x20  /* Allows all characters except for '/' and '%' */
} UnsafeCharacterSet;

static const guchar acceptable[96] =
{ /* X0   X1   X2   X3   X4   X5   X6   X7   X8   X9   XA   XB   XC   XD   XE   XF */
    0x00,0x3F,0x20,0x20,0x20,0x00,0x2C,0x3F,0x3F,0x3F,0x3F,0x22,0x20,0x3F,0x3F,0x1C, /* 2X  !"#$%&'()*+,-./   */
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x38,0x20,0x20,0x2C,0x20,0x2C, /* 3X 0123456789:;<=>?   */
    0x30,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F, /* 4X @ABCDEFGHIJKLMNO   */
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x20,0x20,0x20,0x20,0x3F, /* 5X PQRSTUVWXYZ[\]^_   */
    0x20,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F, /* 6X `abcdefghijklmno   */
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x20,0x20,0x20,0x3F,0x20  /* 7X pqrstuvwxyz{\}~DEL */
};

static const gchar hex[16] = "0123456789ABCDEF";

static gchar *
gnome_vfs_escape_string_internal (const gchar *string, 
				  UnsafeCharacterSet mask)
{
#define ACCEPTABLE(a) ((a)>=32 && (a)<128 && (acceptable[(a)-32] & use_mask))

	const gchar *p;
	gchar *q;
	gchar *result;
	guchar c;
	gint unacceptable;
	UnsafeCharacterSet use_mask;

	g_return_val_if_fail (mask == UNSAFE_ALL
			      || mask == UNSAFE_ALLOW_PLUS
			      || mask == UNSAFE_PATH
			      || mask == UNSAFE_DOS_PATH
			      || mask == UNSAFE_HOST
			      || mask == UNSAFE_SLASHES, NULL);

	if (string == NULL) {
		return NULL;
	}
	
	unacceptable = 0;
	use_mask = mask;
	for (p = string; *p != '\0'; p++) {
		c = *p;
		if (!ACCEPTABLE (c)) {
			unacceptable++;
		}
		if ((use_mask == UNSAFE_HOST) && 
		    (unacceptable || (c == '/'))) {
			/* when escaping a host, if we hit something that needs to be escaped, or we finally
			 * hit a path separator, revert to path mode (the host segment of the url is over).
			 */
			use_mask = UNSAFE_PATH;
		}
	}
	
	result = g_malloc (p - string + unacceptable * 2 + 1);

	use_mask = mask;
	for (q = result, p = string; *p != '\0'; p++){
		c = *p;
		
		if (!ACCEPTABLE (c)) {
			*q++ = HEX_ESCAPE; /* means hex coming */
			*q++ = hex[c >> 4];
			*q++ = hex[c & 15];
		} else {
			*q++ = c;
		}
		if ((use_mask == UNSAFE_HOST) &&
		    (!ACCEPTABLE (c) || (c == '/'))) {
			use_mask = UNSAFE_PATH;
		}
	}
	
	*q = '\0';
	
	return result;
}

gchar *
gnome_vfs_escape_string (const gchar *file_name)
{
	return gnome_vfs_escape_string_internal (file_name, UNSAFE_ALL);
}

gchar *
gnome_vfs_escape_path_string (const gchar *path)
{
	return gnome_vfs_escape_string_internal (path, UNSAFE_PATH);
}

gchar *
gnome_vfs_escape_host_and_path_string (const gchar *path)
{
	return gnome_vfs_escape_string_internal (path, UNSAFE_HOST);
}

gchar *
gnome_vfs_escape_slashes (const gchar *string)
{
	return gnome_vfs_escape_string_internal (string, UNSAFE_SLASHES);
}

static int
hex_to_int (gchar c)
{
	return  c >= '0' && c <= '9' ? c - '0'
		: c >= 'A' && c <= 'F' ? c - 'A' + 10
		: c >= 'a' && c <= 'f' ? c - 'a' + 10
		: -1;
}

/*  Decode %xx escaped characters
**  -----------------------------
**
** This function takes a pointer to a string in which some
** characters may have been encoded in %xy form, where xy is
** the ASCII hex code for character 16x+y.
*/

gchar *
gnome_vfs_unescape_string (const gchar *escaped, const gchar *illegal_characters)
{
	const gchar *in;
	gchar *out, *result;
	gint i;
	gchar c;
	
	if (escaped == NULL) {
		return NULL;
	}

	result = g_malloc (strlen (escaped) + 1);
	
	out = result;
	for (in = escaped; *in != '\0'; ) {
		c = *in++;

		if (c == HEX_ESCAPE) {
			/* Get the first hex digit. */
			i = hex_to_int (*in++);
			if (i < 0) {
				goto error;
			}
			c = i << 4;

			/* Get the second hex digit. */
			i = hex_to_int (*in++);
			if (i < 0) {
				goto error;
			}
			c |= i;

			/* Check for an illegal character. */
			if (c == '\0'
			    || (illegal_characters != NULL
				&& strchr (illegal_characters, c) != NULL)) {
				goto error;
			}
		}

		*out++ = c;
	}
	
	*out = '\0';
	g_assert (out - result <= strlen (escaped));
	return result;
	
 error:
	g_free (result);
	return NULL;
}

/**
 * gnome_vfs_unescape_for_display:
 * @escaped: The string encoded with escaped sequences
 * 
 * Similar to gnome_vfs_unescape_string, but it returns something
 * semi-intelligable to a user even upon receiving traumatic input
 * such as %00 or URIs in bad form.
 * 
 * See also: gnome_vfs_unescape_string.
 * 
 * Return value: A pointer to a g_malloc'd string with all characters
 *               replacing their escaped hex values
 **/
gchar *
gnome_vfs_unescape_string_for_display (const gchar *escaped)
{
	const gchar *in, *start_escape;
	gchar *out, *result;
	gint i,j;
	gchar c;
	gint invalid_escape;

	if (escaped == NULL) {
		return NULL;
	}

	result = g_malloc (strlen (escaped) + 1);
	
	out = result;
	for (in = escaped; *in != '\0'; ) {
		start_escape = in;
		c = *in++;
		invalid_escape = 0;
		
		if (c == HEX_ESCAPE) {
			/* Get the first hex digit. */
			i = hex_to_int (*in++);
			if (i < 0) {
				invalid_escape = 1;
				in--;
			}
			c = i << 4;
			
			if (invalid_escape == 0) {
				/* Get the second hex digit. */
				i = hex_to_int (*in++);
				if (i < 0) {
					invalid_escape = 2;
					in--;
				}
				c |= i;
			}
			if (invalid_escape == 0) {
				/* Check for an illegal character. */
				if (c == '\0') {
					invalid_escape = 3;
				}
			}
		}
		if (invalid_escape != 0) {
			for (j = 0; j < invalid_escape; j++) {
				*out++ = *start_escape++;
			}
		} else {
			*out++ = c;
		}
	}
	
	*out = '\0';
	g_assert (out - result <= strlen (escaped));
	return result;
}

void
gnome_vfs_list_deep_free (GList *list)
{
	GList *p;

	if (list == NULL)
		return;

	for (p = list; p != NULL; p = p->next) {
		g_free (p->data);
	}
	g_list_free (list);
}

/* Stolen from Nautilus. This belongs in glib. */
static gboolean
istr_has_prefix (const char *haystack, const char *needle)
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
		if (isupper (hc)) {
			hc = tolower (hc);
		}
		if (isupper (nc)) {
			nc = tolower (nc);
		}
	} while (hc == nc);
	return FALSE;
}

/**
 * gnome_vfs_get_local_path_from_uri:
 * 
 * Return a local path for a file:// URI.
 *
 * Return value: the local path or NULL on error.
 **/
char *
gnome_vfs_get_local_path_from_uri (const char *uri)
{
	char *result, *unescaped_uri;

	if (uri == NULL) {
		return NULL;
	}

	unescaped_uri = gnome_vfs_unescape_string (uri, "/");
	if (unescaped_uri == NULL) {
		return NULL;
	}

	if (istr_has_prefix (unescaped_uri, "file://")) {
		result = g_strdup (unescaped_uri + 7);
	} else if (unescaped_uri[0] == '/') {
		result = g_strdup (unescaped_uri);
	} else {
		result = NULL;
	}

	g_free (unescaped_uri);

	return result;
}

/**
 * gnome_vfs_get_uri_from_local_path:
 * 
 * Return a file:// URI for a local path.
 *
 * Return value: the URI (NULL for some bad errors).
 **/
char *
gnome_vfs_get_uri_from_local_path (const char *local_path)
{
	char *escaped_path, *result;
	
	if (local_path == NULL) {
		return NULL;
	}

	g_return_val_if_fail (local_path[0] == '/', NULL);

	escaped_path = gnome_vfs_escape_path_string (local_path);
	result = g_strconcat ("file://", escaped_path, NULL);
	g_free (escaped_path);
	return result;
}

static gboolean
str_has_prefix (const char *haystack, const char *needle)
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


GnomeVFSResult
gnome_vfs_get_volume_free_space (const GnomeVFSURI *vfs_uri, GnomeVFSFileSize *size)
{	
	//size_t total_blocks, block_size;
	//struct statfs statfs_buffer;
        //int statfs_result;
	const char *base_name;

 	*size = 0;
		
        /* We only handle the file: scheme for now */
	if (!str_has_prefix (gnome_vfs_uri_get_scheme (vfs_uri), "file://")) {
		g_message ("Bad scheme: %s", gnome_vfs_uri_get_scheme (vfs_uri));
		//return GNOME_VFS_ERROR_GENERIC;
	}

	base_name = gnome_vfs_uri_get_basename (vfs_uri);
	g_message ("Base name: %s", base_name);
	
	//statfs_result = statfs (root_directory, &statfs_buffer);
	//g_return_val_if_fail (statfs_result == 0, FALSE);
	//block_size = statfs_buffer.f_bsize; 
	//total_blocks_to_index = statfs_buffer.f_blocks;
        
	return GNOME_VFS_OK;
}




