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

/* Makes a human-readable string. */
gchar *gnome_vfs_format_file_size_for_display (GnomeVFSFileSize  size);

/* Converts unsafe characters to % sequences so the string can be
 * used as a piece of a URI. Escapes all reserved URI characters.
 */
gchar *gnome_vfs_escape_string                (const gchar      *string);

/* Converts unsafe characters to % sequences so the path can be
 * used as a piece of a URI. Escapes all reserved URI characters
 * except for "/".
 */
gchar *gnome_vfs_escape_path_string           (const gchar      *path);

/* Returns NULL if any of the illegal character appear in escaped
 * form. If the illegal characters are in there unescaped, that's OK.
 * Typically you pass "/" for illegal characters when converting to a
 * Unix path, since pieces of Unix paths can't contain "/". ASCII 0
 * is always illegal due to the limitations of NUL-terminated strings.
 */
gchar *gnome_vfs_unescape_string              (const gchar      *string,
					       const gchar      *illegal_characters);

/* Prepare an escaped string for display. Unlike gnome_vfs_unescape_string,
 * this doesn't return NULL if an illegal sequences appears in the string,
 * instead doing its best to provide a useful result.
 */
gchar *gnome_vfs_unescape_string_for_display  (const gchar      *escaped);


#endif /* GNOME_VFS_UTILS_H */
