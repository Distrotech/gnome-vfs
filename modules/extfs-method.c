/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* extfs-method.c - Integrated support for various archiving methods via
   helper scripts.

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

   Author: Ettore Perazzoli <ettore@comm2000.it>
   Based on the ideas from the extfs system implemented in the GNU Midnight
   Commander.  */

/* TODO metadata? */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-module.h"

#include "extfs-method.h"


#define EXTFS_COMMAND_DIR	PREFIX "/lib/vfs/extfs"


/* Our private handle struct.  */
struct _ExtfsHandle {
	GnomeVFSOpenMode open_mode;
	gchar *local_path;
	gint fd;
};
typedef struct _ExtfsHandle ExtfsHandle;

/* List of current handles, for cleaning up in `vfs_module_shutdown()'.  */
static GList *handle_list;
G_LOCK_DEFINE_STATIC (handle_list);


static void
extfs_handle_close (ExtfsHandle *handle)
{
	g_free (handle->local_path);
	close (handle->fd);
	g_free (handle);
}


static GnomeVFSResult
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSCancellation *cancellation)
{
	GnomeVFSResult result;
	GnomeVFSProcessResult process_result;
	gchar *script_path;
	const gchar *p;
	gchar *args[5];
	gchar *temp_name;
	ExtfsHandle *handle;
	gboolean cleanup;
	gint temp_fd;
	gint process_exit_value;

	/* TODO: Support archives on non-local file systems.  Although I am not
           that sure it's such a terrific idea anymore.  */
	if (! gnome_vfs_uri_is_local (uri->parent))
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	/* TODO: Support write mode.  */
	if (mode & GNOME_VFS_OPEN_WRITE)
		return GNOME_VFS_ERROR_READONLYFS;

	if (uri->text == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	if (uri->method_string == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	p = uri->text;
	while (*p == G_DIR_SEPARATOR)
		p++;

	if (uri->text[0] == '\0')
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_create_temp ("extfs", &temp_name, &temp_fd);
	if (result != GNOME_VFS_OK)
		return result;

	handle = g_new (ExtfsHandle, 1);
	handle->fd = temp_fd;
	handle->open_mode = mode;
	handle->local_path = temp_name;

	script_path = g_strconcat (EXTFS_COMMAND_DIR, "/", uri->method_string,
				   NULL);

	args[0] = uri->method_string;
	args[1] = "copyout";
	args[2] = uri->text;
	args[3] = temp_name;
	args[4] = NULL;
	
	/* FIXME args */
	process_result = gnome_vfs_process_run_cancellable
		(script_path, args, GNOME_VFS_PROCESS_CLOSEFDS, cancellation,
		 &process_exit_value);

	switch (process_result) {
	case GNOME_VFS_PROCESS_RUN_OK:
		result = GNOME_VFS_OK;
		cleanup = FALSE;
		break;
	case GNOME_VFS_PROCESS_RUN_CANCELLED:
		result = GNOME_VFS_ERROR_CANCELLED;
		cleanup = TRUE;
		break;
	case GNOME_VFS_PROCESS_RUN_SIGNALED:
		result = GNOME_VFS_ERROR_INTERRUPTED;
		cleanup = TRUE;
		break;
	case GNOME_VFS_PROCESS_RUN_STOPPED:
		result = GNOME_VFS_ERROR_INTERRUPTED;
		cleanup = TRUE;
		break;
	case GNOME_VFS_PROCESS_RUN_ERROR:
	default:
		/* If we get `GNOME_VFS_PROCESS_RUN_ERRO', it means we could
		   not run the executable for some reason.*/
		result = GNOME_VFS_ERROR_INTERNAL;
		cleanup = TRUE;
		break;
	}

	if (cleanup) {
		extfs_handle_close (handle);
		unlink (temp_name);
	} else {
		*method_handle = (GnomeVFSMethodHandle *) handle;
		G_LOCK (handle_list);
		handle_list = g_list_prepend (handle_list, handle);
		G_UNLOCK (handle_list);
	}

	g_free (temp_name);
	g_free (script_path);
	return result;
}

static GnomeVFSResult
do_create (GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm,
	   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_READONLYFS;
}

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle,
	  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_seek (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset,
	 GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_tell (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethodHandle *method_handle,
	     GnomeVFSFileSize where,
	     GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}


static GnomeVFSResult
do_open_directory (GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   const GList *meta_keys,
		   const GnomeVFSDirectoryFilter *filter,
		   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethodHandle *method_handle,
		    GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}


static gboolean
do_is_local (const GnomeVFSURI *uri)
{
	return FALSE;
}

static GnomeVFSResult
do_make_directory (GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSURI *uri,
		     GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_move (GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}


static GnomeVFSResult
do_unlink (GnomeVFSURI *uri,
	   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSURI *a,
		  GnomeVFSURI *b,
		  gboolean *same_fs_return,
		  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}



static GnomeVFSMethod method = {
	do_open,
	do_create,
	do_close,
	do_read,
	do_write,
	do_seek,
	do_tell,
	do_truncate,
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,
	do_get_file_info_from_handle,
	do_is_local,
	do_make_directory,
	do_remove_directory,
	do_move,
	do_unlink,
	do_check_same_fs
};

GnomeVFSMethod *
vfs_module_init (void)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
