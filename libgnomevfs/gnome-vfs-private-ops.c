/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-private-ops.c - Private synchronous operations for the GNOME
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

   Author: Ettore Perazzoli <ettore@gnu.org> */

/* This file provides private versions of the ops for internal use.  These are
   meant to be used within the GNOME VFS and its modules: they are not for
   public consumption through the external API.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

GnomeVFSResult
gnome_vfs_open_uri_cancellable (GnomeVFSHandle **handle,
				GnomeVFSURI *uri,
				GnomeVFSOpenMode open_mode,
				GnomeVFSCancellation *cancellation)
{
	GnomeVFSMethodHandle *method_handle;
	GnomeVFSResult result;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri->method != NULL, GNOME_VFS_ERROR_BADPARAMS);

	result = uri->method->open (&method_handle, uri, open_mode,
				    cancellation);

	/* FIXME */
	if ((open_mode & GNOME_VFS_OPEN_RANDOM)
	    && result == GNOME_VFS_ERROR_NOTSUPPORTED)
		result = uri->method->open (&method_handle, uri,
					    open_mode & ~GNOME_VFS_OPEN_RANDOM,
					    cancellation);

	if (result != GNOME_VFS_OK)
		return result;

	*handle = gnome_vfs_handle_new (uri, method_handle, open_mode);
	
	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_create_uri_cancellable (GnomeVFSHandle **handle,
				  GnomeVFSURI *uri,
				  GnomeVFSOpenMode open_mode,
				  gboolean exclusive,
				  guint perm,
				  GnomeVFSCancellation *cancellation)
{
	GnomeVFSMethodHandle *method_handle;
	GnomeVFSResult result;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	result = uri->method->create (&method_handle, uri, open_mode,
				      exclusive, perm, cancellation);
	if (result != GNOME_VFS_OK)
		return result;

	*handle = gnome_vfs_handle_new (uri, method_handle, open_mode);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_close_cancellable (GnomeVFSHandle *handle,
			     GnomeVFSCancellation *cancellation)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return gnome_vfs_handle_do_close (handle, cancellation);
}

GnomeVFSResult
gnome_vfs_read_cancellable (GnomeVFSHandle *handle,
			    gpointer buffer,
			    GnomeVFSFileSize bytes,
			    GnomeVFSFileSize *bytes_written,
			    GnomeVFSCancellation *cancellation)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return gnome_vfs_handle_do_read (handle, buffer, bytes, bytes_written,
					 cancellation);
}

GnomeVFSResult
gnome_vfs_write_cancellable (GnomeVFSHandle *handle,
			     gconstpointer buffer,
			     GnomeVFSFileSize bytes,
			     GnomeVFSFileSize *bytes_written,
			     GnomeVFSCancellation *cancellation)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return gnome_vfs_handle_do_write (handle, buffer, bytes,
					  bytes_written, cancellation);
}

GnomeVFSResult
gnome_vfs_seek_cancellable (GnomeVFSHandle *handle,
			    GnomeVFSSeekPosition whence,
			    GnomeVFSFileOffset offset,
			    GnomeVFSCancellation *cancellation)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return gnome_vfs_handle_do_seek (handle, whence, offset, cancellation);
}

GnomeVFSResult
gnome_vfs_get_file_info_uri_cancellable (GnomeVFSURI *uri,
					 GnomeVFSFileInfo *info,
					 GnomeVFSFileInfoOptions options,
					 gchar *meta_keys[],
					 GnomeVFSCancellation *cancellation)
{
	GnomeVFSResult result;
	GList *meta_list;

	meta_list = gnome_vfs_string_list_from_string_array (meta_keys);

	result = uri->method->get_file_info (uri, info, options, meta_list,
					     cancellation);

	gnome_vfs_free_string_list (meta_list);
	return result;
}

GnomeVFSResult
gnome_vfs_get_file_info_from_handle_cancellable (GnomeVFSHandle *handle,
						 GnomeVFSFileInfo *info,
						 GnomeVFSFileInfoOptions options,
						 gchar *meta_keys[],
						 GnomeVFSCancellation *cancellation)

{
	GnomeVFSResult result;
	GList *meta_list;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	meta_list = gnome_vfs_string_list_from_string_array (meta_keys);

	result =  gnome_vfs_handle_do_get_file_info (handle, info,
						     options, meta_list,
						     cancellation);

	gnome_vfs_free_string_list (meta_list);

	return result;
}

GnomeVFSResult
gnome_vfs_make_directory_for_uri_cancellable (GnomeVFSURI *uri,
					      guint perm,
					      GnomeVFSCancellation *cancellation)
{
	GnomeVFSResult result;

	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	if (uri->method->make_directory == NULL)
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	result = uri->method->make_directory (uri, perm, cancellation);
	return result;
}

GnomeVFSResult
gnome_vfs_remove_directory_from_uri_cancellable (GnomeVFSURI *uri,
						 GnomeVFSCancellation *cancellation)
{
	GnomeVFSResult result;

	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	if (uri->method->remove_directory == NULL)
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	result = uri->method->remove_directory (uri, cancellation);
	return result;
}

GnomeVFSResult
gnome_vfs_unlink_from_uri_cancellable (GnomeVFSURI *uri,
				       GnomeVFSCancellation *cancellation)
{
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	if (uri->method->unlink == NULL)
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	return uri->method->unlink (uri, cancellation);
}

