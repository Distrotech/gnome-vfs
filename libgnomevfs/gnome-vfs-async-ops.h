/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-async-ops.h - Asynchronous operations in the GNOME Virtual File
   System.

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

#ifndef _GNOME_VFS_ASYNC_OPS_H
#define _GNOME_VFS_ASYNC_OPS_H

GnomeVFSResult	 gnome_vfs_async_cancel		(GnomeVFSAsyncHandle *handle);

void          	 gnome_vfs_async_open		(GnomeVFSAsyncHandle **handle_return,
						 const gchar *text_uri,
						 GnomeVFSOpenMode open_mode,
						 GnomeVFSAsyncOpenCallback
						 	callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_open_uri	(GnomeVFSAsyncHandle **handle_return,
						 GnomeVFSURI *uri,
						 GnomeVFSOpenMode open_mode,
						 GnomeVFSAsyncOpenCallback
						 	callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_open_as_channel
						(GnomeVFSAsyncHandle **handle_return,
						 const gchar *text_uri,
						 GnomeVFSOpenMode open_mode,
						 guint advised_block_size,
						 GnomeVFSAsyncOpenAsChannelCallback
						        callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_open_uri_as_channel
						(GnomeVFSAsyncHandle **handle_return,
						 GnomeVFSURI *uri,
						 GnomeVFSOpenMode open_mode,
						 guint advised_block_size,
						 GnomeVFSAsyncOpenAsChannelCallback
						        callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_create		(GnomeVFSAsyncHandle **handle_return,
						 const gchar *text_uri,
						 GnomeVFSOpenMode open_mode,
						 gboolean exclusive,
						 guint perm,
						 GnomeVFSAsyncOpenCallback
						 	callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_create_uri	(GnomeVFSAsyncHandle **handle_return,
						 GnomeVFSURI *uri,
						 GnomeVFSOpenMode open_mode,
						 gboolean exclusive,
						 guint perm,
						 GnomeVFSAsyncOpenCallback
						 	callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_create_as_channel
						(GnomeVFSAsyncHandle **handle_return,
						 const gchar *text_uri,
						 GnomeVFSOpenMode open_mode,
						 gboolean exclusive,
						 guint perm,
						 GnomeVFSAsyncCreateAsChannelCallback
	                                                 callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_create_uri_as_channel
						(GnomeVFSAsyncHandle **handle_return,
						 GnomeVFSURI *uri,
						 GnomeVFSOpenMode open_mode,
						 gboolean exclusive,
						 guint perm,
						 GnomeVFSAsyncCreateAsChannelCallback
	                                                 callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_close		(GnomeVFSAsyncHandle *handle,
						 GnomeVFSAsyncCloseCallback
						 	callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_read		(GnomeVFSAsyncHandle *handle,
						 gpointer buffer,
						 guint bytes,
						 GnomeVFSAsyncReadCallback
						 	callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_write		(GnomeVFSAsyncHandle *handle,
						 gconstpointer buffer,
						 guint bytes,
						 GnomeVFSAsyncWriteCallback
						 	callback,
						 gpointer callback_data);

void             gnome_vfs_async_get_file_info  (GnomeVFSAsyncHandle **handle_return,
						 GList *uri_list, /* GnomeVFSURI* items */
						 GnomeVFSFileInfoOptions options,
						 const gchar * const meta_keys[],
						 GnomeVFSAsyncGetFileInfoCallback callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_load_directory (GnomeVFSAsyncHandle **handle_return,
						 const gchar *text_uri,
						 GnomeVFSFileInfoOptions
						         options,
						 const gchar * const meta_keys[],
						 GnomeVFSDirectorySortRule
						 	sort_rules[],
						 gboolean reverse_order,
						 GnomeVFSDirectoryFilterType
						         filter_type,
						 GnomeVFSDirectoryFilterOptions
						         filter_options,
						 const gchar *filter_pattern,
						 guint items_per_notification,
						 GnomeVFSAsyncDirectoryLoadCallback
						         callback,
						 gpointer callback_data);

void          	 gnome_vfs_async_load_directory_uri
					        (GnomeVFSAsyncHandle **handle_return,
						 GnomeVFSURI *uri,
						 GnomeVFSFileInfoOptions
						         options,
						 const gchar *meta_keys[],
						 GnomeVFSDirectorySortRule
						 	sort_rules[],
						 gboolean reverse_order,
						 GnomeVFSDirectoryFilterType
						         filter_type,
						 GnomeVFSDirectoryFilterOptions
						         filter_options,
						 const gchar *filter_pattern,
						 guint items_per_notification,
						 GnomeVFSAsyncDirectoryLoadCallback
						         callback,
						 gpointer callback_data);

GnomeVFSResult	gnome_vfs_async_xfer		(GnomeVFSAsyncHandle **handle_return,
						 const gchar *source_dir,
						 const GList *source_name_list,
						 const gchar *target_dir,
						 const GList *target_name_list,
						 GnomeVFSXferOptions
						 	xfer_options,
						 GnomeVFSXferErrorMode
						 	error_mode,
						 GnomeVFSXferOverwriteMode
						 	overwrite_mode,
						 GnomeVFSAsyncXferProgressCallback
						 	progress_update_callback,
						 gpointer update_callback_data,
						 GnomeVFSXferProgressCallback
						 	progress_sync_callback,
						 gpointer sync_callback_data);


guint           gnome_vfs_async_add_status_callback
                                                (GnomeVFSAsyncHandle *handle,
						 GnomeVFSStatusCallback callback,
						 gpointer user_data);

void            gnome_vfs_async_remove_status_callback
                                                (GnomeVFSAsyncHandle *handle,
						 guint callback_id);

#endif /* _GNOME_VFS_ASYNC_OPS_H */
