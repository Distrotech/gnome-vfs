/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-ops.c - Synchronous operations for the GNOME Virtual File
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

   Author: Ettore Perazzoli <ettore@gnu.org> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


GnomeVFSResult
gnome_vfs_open (GnomeVFSHandle **handle,
		const gchar *text_uri,
		GnomeVFSOpenMode open_mode)
{
	GnomeVFSURI *uri;
	GnomeVFSResult result;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_open_uri (handle, uri, open_mode);

	gnome_vfs_uri_unref (uri);

	return result;
}

GnomeVFSResult
gnome_vfs_open_uri (GnomeVFSHandle **handle,
		    GnomeVFSURI *uri,
		    GnomeVFSOpenMode open_mode)
{
	return gnome_vfs_open_uri_cancellable (handle, uri, open_mode, NULL);
}

GnomeVFSResult
gnome_vfs_create (GnomeVFSHandle **handle,
		  const gchar *text_uri,
		  GnomeVFSOpenMode open_mode,
		  gboolean exclusive,
		  guint perm)
{
	GnomeVFSURI *uri;
	GnomeVFSResult result;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_create_uri (handle, uri, open_mode, exclusive, perm);

	gnome_vfs_uri_unref (uri);

	return result;
}

GnomeVFSResult
gnome_vfs_create_uri (GnomeVFSHandle **handle,
		      GnomeVFSURI *uri,
		      GnomeVFSOpenMode open_mode,
		      gboolean exclusive,
		      guint perm)
{
	return gnome_vfs_create_uri_cancellable (handle, uri, open_mode,
						 exclusive, perm, NULL);
}

GnomeVFSResult
gnome_vfs_close (GnomeVFSHandle *handle)
{
	return gnome_vfs_close_cancellable (handle, NULL);
}

GnomeVFSResult
gnome_vfs_read (GnomeVFSHandle *handle,
		gpointer buffer,
		GnomeVFSFileSize bytes,
		GnomeVFSFileSize *bytes_written)
{
	return gnome_vfs_read_cancellable (handle, buffer, bytes, bytes_written,
					   NULL);
}

GnomeVFSResult
gnome_vfs_write (GnomeVFSHandle *handle,
		 gconstpointer buffer,
		 GnomeVFSFileSize bytes,
		 GnomeVFSFileSize *bytes_written)
{
	return gnome_vfs_write_cancellable (handle, buffer, bytes,
					    bytes_written, NULL);
}

GnomeVFSResult
gnome_vfs_seek (GnomeVFSHandle *handle,
		GnomeVFSSeekPosition whence,
		GnomeVFSFileOffset offset)
{
	return gnome_vfs_seek_cancellable (handle, whence, offset, NULL);
}

GnomeVFSResult
gnome_vfs_tell (GnomeVFSHandle *handle,
		GnomeVFSFileSize *offset_return)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return gnome_vfs_handle_do_tell (handle, offset_return);
}


GnomeVFSResult
gnome_vfs_get_file_info (const gchar *text_uri,
			 GnomeVFSFileInfo *info,
			 GnomeVFSFileInfoOptions options,
			 gchar *meta_keys[])
{
	GnomeVFSURI *uri;
	GnomeVFSResult result;
	GList *meta_list;

	uri = gnome_vfs_uri_new (text_uri);
	meta_list = gnome_vfs_string_list_from_string_array (meta_keys);

	result = uri->method->get_file_info (uri, info, options, meta_list,
					     NULL);

	gnome_vfs_free_string_list (meta_list);
	gnome_vfs_uri_unref (uri);

	return result;
}

GnomeVFSResult
gnome_vfs_get_file_info_uri (GnomeVFSURI *uri,
			     GnomeVFSFileInfo *info,
			     GnomeVFSFileInfoOptions options,
			     gchar *meta_keys[])
{
	return gnome_vfs_get_file_info_uri_cancellable (uri, info, options,
							meta_keys, NULL);
}

GnomeVFSResult
gnome_vfs_get_file_info_from_handle (GnomeVFSHandle *handle,
				     GnomeVFSFileInfo *info,
				     GnomeVFSFileInfoOptions options,
				     gchar *meta_keys[])

{
	return gnome_vfs_get_file_info_from_handle_cancellable (handle, info,
								options,
								meta_keys,
								NULL);
}


GnomeVFSResult
gnome_vfs_make_directory_for_uri (GnomeVFSURI *uri,
				  guint perm)
{
	return gnome_vfs_make_directory_for_uri_cancellable (uri, perm, NULL);
}

GnomeVFSResult
gnome_vfs_make_directory (const gchar *text_uri,
			  guint perm)
{
	GnomeVFSResult result;
	GnomeVFSURI *uri;

	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_make_directory_for_uri (uri, perm);

	gnome_vfs_uri_unref (uri);

	return result;
}


GnomeVFSResult
gnome_vfs_remove_directory_from_uri (GnomeVFSURI *uri)
{
	return gnome_vfs_remove_directory_from_uri_cancellable (uri, NULL);
}

GnomeVFSResult
gnome_vfs_remove_directory (const gchar *text_uri)
{
	GnomeVFSResult result;
	GnomeVFSURI *uri;

	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_remove_directory_from_uri (uri);

	gnome_vfs_uri_unref (uri);

	return result;
}


GnomeVFSResult
gnome_vfs_unlink_from_uri (GnomeVFSURI *uri)
{
	return gnome_vfs_unlink_from_uri_cancellable (uri, NULL);
}

GnomeVFSResult
gnome_vfs_unlink (const gchar *text_uri)
{
	GnomeVFSResult result;
	GnomeVFSURI *uri;

	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_unlink_from_uri (uri);

	gnome_vfs_uri_unref (uri);

	return result;
}
