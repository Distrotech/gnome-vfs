/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-file-info.h - Handling of file information for the GNOME
   Virtual File System.

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

   Author: Ettore Perazzoli <ettore@comm2000.it> */

#ifndef _GNOME_VFS_FILE_INFO_H
#define _GNOME_VFS_FILE_INFO_H

#include "gnome-vfs.h"


GnomeVFSFileInfo *
		 gnome_vfs_file_info_new 	(void);
void		 gnome_vfs_file_info_init	(GnomeVFSFileInfo *info);
void		 gnome_vfs_file_info_clear	(GnomeVFSFileInfo *info);
void 		 gnome_vfs_file_info_destroy 	(GnomeVFSFileInfo *info);
gboolean 	 gnome_vfs_file_info_get_metadata
						(GnomeVFSFileInfo *info,
						 const gchar *key,
						 gconstpointer *value,
						 guint *value_size);
gboolean 	 gnome_vfs_file_info_set_metadata
						(GnomeVFSFileInfo *info,
						 const gchar *key,
						 gpointer value,
						 guint value_size);
gboolean	 gnome_vfs_file_info_unset_metadata
						(GnomeVFSFileInfo *info,
						 const gchar *key);
const gchar	*gnome_vfs_file_info_get_mime_type
						(GnomeVFSFileInfo *info);

void		 gnome_vfs_file_info_copy 	(GnomeVFSFileInfo *dest,
						 const GnomeVFSFileInfo *src);

#endif                          /* _GNOME_VFS_FILE_INFO_H */
