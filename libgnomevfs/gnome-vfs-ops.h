/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-ops.h - Synchronous operations for the GNOME Virtual File
   System.

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

#ifndef _GNOME_VFS_OPS_H
#define _GNOME_VFS_OPS_H

GnomeVFSResult	 gnome_vfs_open			(GnomeVFSHandle **handle,
						 const gchar *text_uri,
						 GnomeVFSOpenMode open_mode);

GnomeVFSResult	 gnome_vfs_open_from_uri	(GnomeVFSHandle **handle,
						 GnomeVFSURI *uri,
						 GnomeVFSOpenMode open_mode);

GnomeVFSResult	 gnome_vfs_create		(GnomeVFSHandle **handle,
						 const gchar *text_uri,
						 GnomeVFSOpenMode open_mode,
						 gboolean exclusive,
						 guint perm);

GnomeVFSResult	 gnome_vfs_create_for_uri	(GnomeVFSHandle **handle,
						 GnomeVFSURI *uri,
						 GnomeVFSOpenMode open_mode,
						 gboolean exclusive,
						 guint perm);

GnomeVFSResult 	 gnome_vfs_close 		(GnomeVFSHandle *handle);

GnomeVFSResult	 gnome_vfs_read			(GnomeVFSHandle *handle,
						 gpointer buffer,
						 gulong bytes,
						 gulong *bytes_read);

GnomeVFSResult	 gnome_vfs_write 		(GnomeVFSHandle *handle,
						 gconstpointer buffer,
						 gulong bytes,
						 gulong *bytes_written);

GnomeVFSResult	 gnome_vfs_seek			(GnomeVFSHandle *handle,
						 GnomeVFSSeekPosition whence,
						 gulong offset);

GnomeVFSResult	 gnome_vfs_tell			(GnomeVFSHandle *handle,
						 GnomeVFSSeekPosition whence,
						 gulong *offset_return);

GnomeVFSResult	 gnome_vfs_get_file_info	(const gchar *text_uri,
						 GnomeVFSFileInfo *info,
						 GnomeVFSFileInfoOptions
						 	options,
						 gchar *meta_keys[]);

GnomeVFSResult	 gnome_vfs_get_file_info_from_uri
						(GnomeVFSURI *uri,
						 GnomeVFSFileInfo *info,
						 GnomeVFSFileInfoOptions
						 	options,
						 gchar *meta_keys[]);

GnomeVFSResult	 gnome_vfs_make_directory_for_uri
						(GnomeVFSURI *uri, guint perm);
GnomeVFSResult	 gnome_vfs_make_directory	(const gchar *text_uri,
						 guint perm);

#endif /* _GNOME_VFS_OPS_H */
