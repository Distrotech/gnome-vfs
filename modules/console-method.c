/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* console-method.c - access method to standard streams for the GNOME Virtual
   File System.

   Copyright (C) 1999 Free Software Foundation
   Copyright (C) 2002 Giovanni Corriga

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

   Authors: 
   	Ettore Perazzoli <ettore@comm2000.it>
   	Pavel Cisler <pavel@eazel.com>
	Giovanni Corriga <valkadesh@libero.it>
 */

#include <config.h>

#include <libgnomevfs/gnome-vfs-cancellation.h>
#include <libgnomevfs/gnome-vfs-context.h>
#include <libgnomevfs/gnome-vfs-i18n.h>
#include <libgnomevfs/gnome-vfs-method.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-module.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>


#ifdef HAVE_OPEN64
#define OPEN open64
#else
#define OPEN open
#endif

#ifdef HAVE_LSEEK64
#define LSEEK lseek64
#define OFF_T off64_t
#else
#define LSEEK lseek
#define OFF_T off_t
#endif

static gchar *
get_path_from_uri (GnomeVFSURI const *uri)
{
	gchar *path;

	path = gnome_vfs_unescape_string (uri->text, 
		G_DIR_SEPARATOR_S);
		
	if (path == NULL) {
		return NULL;
	}

	if (path[0] != G_DIR_SEPARATOR) {
		g_free (path);
		return NULL;
	}

	return path;
}

static gchar *
get_base_from_uri (GnomeVFSURI const *uri)
{
	gchar *escaped_base, *base;

	escaped_base = gnome_vfs_uri_extract_short_path_name (uri);
	base = gnome_vfs_unescape_string (escaped_base, G_DIR_SEPARATOR_S);
	g_free (escaped_base);
	return base;
}

typedef struct {
	GnomeVFSURI *uri;
	gint fd;
} FileHandle;

static FileHandle *
file_handle_new (GnomeVFSURI *uri,
		 gint fd)
{
	FileHandle *result;
	result = g_new (FileHandle, 1);

	result->uri = gnome_vfs_uri_ref (uri);
	result->fd = fd;

	return result;
}

static void
file_handle_destroy (FileHandle *handle)
{
	gnome_vfs_uri_unref (handle->uri);
	g_free (handle);
}

static gint
get_console_channel_from_uri(GnomeVFSURI const *uri)
{
	gchar* path;
	gint result;
	
	path = get_path_from_uri(uri);
	if (!path)
		return -2;
	
	if (!strncmp(path, "/stdin", 6) && (strlen(path) == 6))
		result = 0;
	else if (!strncmp(path, "/stdout", 7) && (strlen(path) == 7))
		result = 1;
	else if (!strncmp(path, "/stderr", 7) && (strlen(path) == 7))
		result = 2;
	else
		result = -1;
	
	g_free(path);
	
	return result;
}

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	gint channel;
	gint fd;
	FileHandle* file_handle;
	
	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	channel = get_console_channel_from_uri(uri);
	
	if (channel == -2)
		return GNOME_VFS_ERROR_INVALID_URI;
	
	if (channel == -1)
		return GNOME_VFS_ERROR_NOT_FOUND;
	
	if (channel == 0)
		if (mode & GNOME_VFS_OPEN_READ)
		{
			if (mode & GNOME_VFS_OPEN_WRITE)
				return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
		}
		else
			return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	else /* stdout or stderr */
		if (mode & GNOME_VFS_OPEN_WRITE)
		{
			if (mode & GNOME_VFS_OPEN_READ)
				return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
		}
		else
			return GNOME_VFS_ERROR_INVALID_OPEN_MODE;

	
	do
		fd = dup(channel);
	while (fd == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));
	
	if (fd == -1)
		return gnome_vfs_result_from_errno();
	
	file_handle = file_handle_new (uri, fd);
	
	*method_handle = (GnomeVFSMethodHandle *) file_handle;
		
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_create (GnomeVFSMethod *method,
	   GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm,
	   GnomeVFSContext *context)
{
	gint channel;
	
	channel = get_console_channel_from_uri(uri);
	
	if (channel == -2)
		return GNOME_VFS_ERROR_INVALID_URI;
	
	if (channel == -1)
		return GNOME_VFS_ERROR_NOT_PERMITTED;

	/* standard streams already exist */
	return GNOME_VFS_ERROR_FILE_EXISTS;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gint close_retval;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	do
		close_retval = close (file_handle->fd);
	while (close_retval != 0
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));

	/* FIXME bugzilla.eazel.com 1163: Should do this even after a failure?  */
	file_handle_destroy (file_handle);

	if (close_retval != 0) {
		return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gint read_val;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	do {
		read_val = read (file_handle->fd, buffer, num_bytes);
	} while (read_val == -1
	         && errno == EINTR
	         && ! gnome_vfs_context_check_cancellation (context));

	if (read_val == -1) {
		*bytes_read = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_read = read_val;

		/* Getting 0 from read() means EOF! */
		if (read_val == 0) {
			return GNOME_VFS_ERROR_EOF;
		}
	}
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gint write_val;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	do
		write_val = write (file_handle->fd, buffer, num_bytes);
	while (write_val == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));

	if (write_val == -1) {
		*bytes_written = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_written = write_val;
		return GNOME_VFS_OK;
	}
}

/* MIME detection code.  */
static void
get_mime_type (GnomeVFSFileInfo *info,
	       const char *full_name,
	       GnomeVFSFileInfoOptions options,
	       struct stat *stat_buffer)
{
	info->mime_type = g_strdup ("x-special/device-char");
	info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
}

static GnomeVFSResult
get_stat_info_from_handle (GnomeVFSFileInfo *file_info,
			   FileHandle *handle,
			   GnomeVFSFileInfoOptions options,
			   struct stat *statptr)
{
	struct stat statbuf;

	if (statptr == NULL) {
		statptr = &statbuf;
	}

	if (fstat (handle->fd, statptr) != 0) {
		return gnome_vfs_result_from_errno ();
	}
	
	gnome_vfs_stat_to_file_info (file_info, statptr);
	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gchar *full_name;
	struct stat statbuf;
	GnomeVFSResult result;

	file_handle = (FileHandle *) method_handle;

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	full_name = get_path_from_uri (file_handle->uri);
	if (full_name == NULL) {
		return GNOME_VFS_ERROR_INVALID_URI;
	}

	file_info->name = get_base_from_uri (file_handle->uri);
	g_assert (file_info->name != NULL);

	result = get_stat_info_from_handle (file_info, file_handle,
					    options, &statbuf);
	if (result != GNOME_VFS_OK) {
		g_free (full_name);
		return result;
	}

	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
		get_mime_type (file_info, full_name, options, &statbuf);
	}

	g_free (full_name);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  GnomeVFSContext *context)
{
	int channel;
	FileHandle *handle;
	GnomeVFSResult result;
		
	channel = get_console_channel_from_uri(uri);
	
	if (channel == -2)
		return GNOME_VFS_ERROR_INVALID_URI;
	
	if (channel == -1)
		return GNOME_VFS_ERROR_NOT_FOUND;
	
	handle = file_handle_new(uri, channel);
	
	result = do_get_file_info_from_handle(method, (GnomeVFSMethodHandle*) handle,
											file_info, options, context);
	file_handle_destroy(handle);
	
	return result;
}

static gboolean
do_is_local (GnomeVFSMethod *method,
	     const GnomeVFSURI *uri)
{
	return TRUE;
}

static GnomeVFSMethod method = {
	sizeof (GnomeVFSMethod),
	do_open,
	do_create,
	do_close,
	do_read,
	do_write,
	NULL, /* do_seek */
	NULL, /* do_tell */
	NULL, /* do_truncate_handle */
	NULL, /* do_open_directory */
	NULL, /* do_close_directory */
	NULL, /* do_read_directory */
	do_get_file_info,
	do_get_file_info_from_handle,
	do_is_local,
	NULL, /* do_make_directory */
	NULL, /* do_remove_directory */
	NULL, /* do_move */
	NULL, /* do_unlink */
	NULL, /* do_check_same_fs */
	NULL, /* do_set_file_info */
	NULL, /* do_truncate */
	NULL, /* do_find_directory */
	NULL, /* do_create_symbolic_link */
	NULL, /* do_monitor_add */
	NULL  /* do_monitor_cancel */
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
