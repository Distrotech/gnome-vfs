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


gchar*
gnome_vfs_file_size_to_string   (GnomeVFSFileSize bytes)
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
#define ACCEPTABLE(a) ( a>=32 && a<128 && ((gnome_vfs_is_acceptable[a-32]) & mask))

/* Macros for converting characters. */

#ifndef TOASCII
 #define TOASCII(c) (c)
 #define FROMASCII(c) (c)
#endif


/*
 *  Not BOTH static AND const at the same time in gcc :-(, Henrik 18/03-94
 *  code gen error in gcc when making random access to static const table(!!)
 */

/*
 * Bit 0  xalpha  -- see HTFile.h
 * Bit 1  xpalpha  -- as xalpha but with plus.
 * Bit 2 ... path  -- as xpalpha but with /
 */

guchar gnome_vfs_is_acceptable[96] =
{/* 0x0 0x1 0x2 0x3 0x4 0x5 0x6 0x7 0x8 0x9 0xA 0xB 0xC 0xD 0xE 0xF */
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xF,0xE,0x0,0xF,0xF,0xC, /* 2x !"#$%&'()*+,-./   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0x8,0x0,0x0,0x0,0x0,0x0, /* 3x 0123456789:;<=>?   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF, /* 4x @ABCDEFGHIJKLMNO   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0x0,0x0,0x0,0x0,0xF, /* 5X PQRSTUVWXYZ[\]^_   */
    0x0,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF, /* 6x `abcdefghijklmno   */
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,0x0,0x0,0x0,0x0,0x0  /* 7X pqrstuvwxyz{\}~DEL */
};

gchar *hex = "0123456789ABCDEF";

/* ------------------------------------------------------------------------- */

/*  Escape undesirable characters using %
 *  -------------------------------------
 *
 * This function takes a pointer to a string in which
 * some characters may be unacceptable unescaped.
 * It returns a string which has these characters
 * represented by a '%' character followed by two hex digits.
 *
 * In the tradition of being conservative in what you do and liberal
 * in what you accept, we encode some characters which in fact are
 * allowed in URLs unencoded -- so DON'T use the table below for
 * parsing!
 *
 * Unlike gnome_vfs_ht_escape(), this routine returns a g_malloced string.
 *
 */

gchar *
gnome_vfs_escape_string (const gchar *str, 
			 GnomeVFSHTURIEncoding mask)
{
    const gchar * p;
    gchar * q;
    gchar * result;
    gint unacceptable = 0;

    if (!str)
     return NULL;

    for (p=str; *p; p++)
	    if (!ACCEPTABLE ((unsigned char) TOASCII (*p)))
		    unacceptable++;

    if ((result = (char  *) g_malloc (p-str + unacceptable+ unacceptable +1)) == NULL)
	    g_assert (result == NULL);

    for (q=result, p=str; *p; p++){
	    unsigned char a = TOASCII (*p);

	    if (!ACCEPTABLE (a)) {
		    *q++ = HEX_ESCAPE; /* Means hex commming */
		    *q++ = hex[a >> 4];
		    *q++ = hex[a & 15];
	    }
	    else *q++ = *p;
    }

    *q++ = 0;   /* Terminate */
    
    return result;
}


static gchar 
gnome_vfs_ht_ascii_hex_to_char (gchar c)
{
    return  c >= '0' && c <= '9' ?  c - '0'
	    : c >= 'A' && c <= 'F'? c - 'A' + 10
	    : c - 'a' + 10; /* accept small letters just in case */
}

/*  Decode %xx escaped characters
**  -----------------------------
**
** This function takes a pointer to a string in which some
** characters may have been encoded in %xy form, where xy is
** the acsii hex code for character 16x+y.
** The string is converted in place, as it will never grow.
*/

gchar *
gnome_vfs_unescape_string (gchar * str)
{
	gchar * p = str;
	gchar * q = str;
	
	if (!str) {           /* Just for safety ;-) */
		g_warning ("gnome_vfs_unescape_string (): Called with NULL argument'.");
		return NULL;
	}
	
	while(*p) {
		if (*p == HEX_ESCAPE) {
			p++;
			
			if (*p)
				*q = gnome_vfs_ht_ascii_hex_to_char(*p++) * 16;
#if 1
			/* Suggestion from Markku Savela */
			if (*p)
				*q = FROMASCII(*q + gnome_vfs_ht_ascii_hex_to_char(*p)), ++p;
			
			*q++;
#else
			if (*p)
				*q = FROMASCII(*q + gnome_vfs_ht_ascii_hex_to_char(*p));
			
			*p++, q++;
#endif
		} else {
			*q++ = *p++;
		}
	}
	
	*q++ = 0;
	return str;
}

