/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-private-types.h - Declaration of private types for the
   GNOME Virtual File System.

   Copyright (C) 1999 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Ettore Perazzoli <ettore@comm2000.it> */

#ifndef _GNOME_VFS_PRIVATE_TYPES_H
#define _GNOME_VFS_PRIVATE_TYPES_H


typedef gpointer GnomeVFSMethodHandle;

typedef GnomeVFSResult (* GnomeVFSMethodOpenFunc)
					(GnomeVFSMethodHandle
			       	 	**method_handle_return,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode);

typedef GnomeVFSResult (* GnomeVFSMethodCreateFunc)
					(GnomeVFSMethodHandle
			       	 	**method_handle_return,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode,
					 gboolean exclusive,
					 guint perm);

typedef GnomeVFSResult (* GnomeVFSMethodCloseFunc)
					(GnomeVFSMethodHandle *method_handle);

typedef GnomeVFSResult (* GnomeVFSMethodReadFunc)
					(GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read_return);

typedef GnomeVFSResult (* GnomeVFSMethodWriteFunc)
					(GnomeVFSMethodHandle *method_handle,
					 gconstpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_written_return);

typedef GnomeVFSResult (* GnomeVFSMethodSeekFunc)
					(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSSeekPosition  whence,
					 GnomeVFSFileOffset    offset);

typedef GnomeVFSResult (* GnomeVFSMethodTellFunc)
					(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileOffset *offset_return);

typedef GnomeVFSResult (* GnomeVFSMethodTruncateFunc)
					(GnomeVFSMethodHandle *method_handle,
					 glong where);

typedef GnomeVFSResult (* GnomeVFSMethodOpenDirectoryFunc)
					(GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 const GnomeVFSDirectoryFilter *filter);

typedef GnomeVFSResult (* GnomeVFSMethodCloseDirectoryFunc)
					(GnomeVFSMethodHandle *method_handle);

typedef GnomeVFSResult (* GnomeVFSMethodReadDirectoryFunc)
					(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info);

typedef GnomeVFSResult (* GnomeVFSMethodGetFileInfoFunc)
					(GnomeVFSURI *uri,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);

typedef gboolean       (* GnomeVFSMethodIsLocalFunc)
					(const GnomeVFSURI *uri);

typedef GnomeVFSResult (* GnomeVFSMethodRenameFunc)
					(GnomeVFSURI *uri,
					 const gchar *new_name);

typedef GnomeVFSResult (* GnomeVFSMethodMakeDirectoryFunc)
					(GnomeVFSURI *uri,
					 guint perm);

typedef GnomeVFSResult (* GnomeVFSMethodRemoveDirectoryFunc)
					(GnomeVFSURI *uri);


/* Structure defining an access method.	 This is also defined as an
   opaque type in `gnome-vfs-types.h'.	*/
struct _GnomeVFSMethod {
	GnomeVFSMethodOpenFunc open;
	GnomeVFSMethodCreateFunc create;
	GnomeVFSMethodCloseFunc close;
	GnomeVFSMethodReadFunc read;
	GnomeVFSMethodWriteFunc write;
	GnomeVFSMethodSeekFunc seek;
	GnomeVFSMethodTellFunc tell;
	GnomeVFSMethodTruncateFunc truncate;
	GnomeVFSMethodOpenDirectoryFunc open_directory;
	GnomeVFSMethodCloseDirectoryFunc close_directory;
	GnomeVFSMethodReadDirectoryFunc read_directory;
	GnomeVFSMethodGetFileInfoFunc get_file_info;
	GnomeVFSMethodIsLocalFunc is_local;
	GnomeVFSMethodMakeDirectoryFunc make_directory;
	GnomeVFSMethodRemoveDirectoryFunc remove_directory;
	GnomeVFSMethodRenameFunc rename;
};


typedef struct _GnomeVFSShellpatternFilter GnomeVFSShellpatternFilter;
typedef struct _GnomeVFSRegexpFilter GnomeVFSRegexpFilter;

#endif /* _GNOME_VFS_PRIVATE_TYPES_H */
