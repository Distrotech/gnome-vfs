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


gchar*
gnome_vfs_file_size_to_string (GnomeVFSFileSize bytes)
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
 * Unlike gnome_vfs_unescape(), this routine returns a g_malloced string.
 */

static const guchar acceptable[96] =
{ /* X0  X1  X2  X3  X4  X5  X6  X7  X8  X9  XA  XB  XC  XD  XE  XF */
    0x0,0xF,0x0,0x0,0x0,0x0,0x0,0xF,0xF,0xF,0xF,0x2,0x0,0xF,0xF,0xC, /* 2X  !"#$%&'()*+,-./   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0x8,0x0,0x0,0x0,0x0,0x0, /* 3X 0123456789:;<=>?   */
    0x0,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF, /* 4X @ABCDEFGHIJKLMNO   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0x0,0x0,0x0,0x0,0xF, /* 5X PQRSTUVWXYZ[\]^_   */
    0x0,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF, /* 6X `abcdefghijklmno   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0x0,0x0,0x0,0xF,0x0  /* 7X pqrstuvwxyz{\}~DEL */
};

static const gchar hex[16] = "0123456789ABCDEF";

gchar *
gnome_vfs_escape_string (const gchar *string, 
			 GnomeVFSURIUnsafeCharacterSet mask)
{
#define ACCEPTABLE(a) ((a)>=32 && (a)<128 && (acceptable[(a)-32] & mask))

	const gchar *p;
	gchar *q;
	gchar *result;
	guchar c;
	gint unacceptable;

	g_return_val_if_fail (string != NULL, NULL);
	g_return_val_if_fail (mask == GNOME_VFS_URI_UNSAFE_ALL
			      || mask == GNOME_VFS_URI_UNSAFE_ALLOW_PLUS
			      || mask == GNOME_VFS_URI_UNSAFE_PATH
			      || mask == GNOME_VFS_URI_UNSAFE_DOS_PATH, NULL);
	
	unacceptable = 0;
	for (p = string; *p != '\0'; p++) {
		c = *p;
		if (!ACCEPTABLE (c)) {
			unacceptable++;
		}
	}
	
	result = g_malloc (p - string + unacceptable * 2 + 1);
	
	for (q = result, p = string; *p != '\0'; p++){
		c = *p;
		
		if (!ACCEPTABLE (c)) {
			*q++ = HEX_ESCAPE; /* means hex coming */
			*q++ = hex[c >> 4];
			*q++ = hex[c & 15];
		} else {
			*q++ = c;
		}
	}
	
	*q = '\0';
	
	return result;
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
	
	g_return_val_if_fail (escaped != NULL, NULL);

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
	
	return result;
	
 error:
	g_free (result);
	return NULL;
}
