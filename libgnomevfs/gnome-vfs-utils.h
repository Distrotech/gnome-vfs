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

#ifndef _GNOME_VFS_UTILS_H
#define _GNOME_VFS_UTILS_H

#include <glib.h>
#include "gnome-vfs-types.h"

gchar*          gnome_vfs_file_size_to_string   (GnomeVFSFileSize bytes);


typedef enum GnomeVFSURIEncoding {
 GNOME_VFS_URI_ENCODING_XALPHAS  = 0x1,  /* Escape all unsafe characters   */
 GNOME_VFS_URI_ENCODING_XPALPHAS = 0x2,  /* As URL_XALPHAS but allows '+'  */
 GNOME_VFS_URI_ENCODING_PATH     = 0x4,  /* As URL_XALPHAS but allows '/'  */
 GNOME_VFS_URI_ENCODING_DOSFILE  = 0x8   /* As URL_URLPATH but allows  ':' */
} GnomeVFSHTURIEncoding;

gchar  *gnome_vfs_escape_string        (const gchar *str, 
					GnomeVFSHTURIEncoding encoding);
gchar  *gnome_vfs_unescape_string      (gchar       *str);


#endif /* _GNOME_VFS_UTILS_H */
