/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-xfer.h - File transfers in the GNOME Virtual File System.

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

#ifndef _GNOME_VFS_COMPLEX_OPS_H
#define _GNOME_VFS_COMPLEX_OPS_H


/* FIXME: These functions don't deal with recursive copying correctly yet.  */

GnomeVFSResult	gnome_vfs_xfer_uri	(GnomeVFSURI *source_dir_uri,
					 const GList *source_name_list,
					 GnomeVFSURI *target_dir_uri,
					 const GList *target_name_list,
					 GnomeVFSXferOptions xfer_options,
					 GnomeVFSXferErrorMode error_mode,
					 GnomeVFSXferOverwriteMode
					 	overwrite_mode,
					 GnomeVFSXferProgressCallback
					 	progress_callback,
					 gpointer data);

GnomeVFSResult	gnome_vfs_xfer		(const gchar *source_dir,
					 const GList *source_name_list,
					 const gchar *target_dir,
					 const GList *target_name_list,
					 GnomeVFSXferOptions xfer_options,
					 GnomeVFSXferErrorMode error_mode,
					 GnomeVFSXferOverwriteMode
					 	overwrite_mode,
					 GnomeVFSXferProgressCallback
					 	progress_callback,
					 gpointer data);

#endif /* _GNOME_VFS_COMPLEX_OPS_H */
