/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-utils.h - Public utility functions for the GNOME Virtual
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

#ifndef GNOME_VFS_UTILS_H
#define GNOME_VFS_UTILS_H

#include <glib.h>
#include "gnome-vfs-types.h"

typedef enum {
	GNOME_VFS_URI_UNSAFE_ALL        = 0x1,  /* Escape all unsafe characters   */
	GNOME_VFS_URI_UNSAFE_ALLOW_PLUS = 0x2,  /* Allows '+'  */
	GNOME_VFS_URI_UNSAFE_PATH       = 0x4,  /* Allows '/'  */
	GNOME_VFS_URI_UNSAFE_DOS_PATH   = 0x8   /* Allows '/' and ':' */
} GnomeVFSURIUnsafeCharacterSet;

/* Attempts to make a human-readable string. */
gchar *gnome_vfs_file_size_to_string (GnomeVFSFileSize               bytes);

/* Converts unsafe characters to % sequences.
 * Parameter defines what unsafe means.
 * FIXME: Divide into four separate calls for clarity.
 */
gchar *gnome_vfs_escape_string       (const gchar                   *string,
				      GnomeVFSURIUnsafeCharacterSet  encoding);

/* Returns NULL if any of the illegal character appear in escaped form.
 * If the illegal characters are in there unescaped, that's OK.
 * Typically you pass "/" for illegal characters when converting to a Unix path.
 * ASCII 0 is always illegal due to the limitations of NUL-terminated strings.
 */
gchar *gnome_vfs_unescape_string     (const gchar                   *string,
				      const gchar                   *illegal_characters);

#endif /* GNOME_VFS_UTILS_H */
