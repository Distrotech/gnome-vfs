/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* file-method.c - Local file access method for the GNOME Virtual File
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

   Author: Rajit Singh <endah@dircon.co.uk> */

/* TODO metadata? */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _LARGEFILE64_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include <gnome.h>

#include "gnome-vfs-mime.h"

#include "gnome-vfs-module.h"
#include "gnome-vfs-module-shared.h"
#include "efs-method.h"



/* This is to make sure the path starts with `/', so that at least we
   get a predictable behavior when the leading `/' is not present.  */
#define MAKE_ABSOLUTE(dest, src)			\
G_STMT_START{						\
	if ((src)[0] != '/') {				\
		(dest) = alloca (strlen (src) + 2);	\
		(dest)[0] = '/';			\
		strcpy ((dest), (src));			\
	} else {					\
		(dest) = (src);				\
	}						\
}G_STMT_END

#ifdef PATH_MAX
#define	GET_PATH_MAX()	PATH_MAX
#else
static int
GET_PATH_MAX (void)
{
	static unsigned int value;

	/* This code is copied from GNU make.  It returns the maximum
	   path length by using `pathconf'.  */

	if (value == 0) {
		long int x = pathconf("/", _PC_PATH_MAX);

		if (x > 0)
			value = x;
		else
			return MAXPATHLEN;
	}

	return value;
}
#endif

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


struct _FileHandle {
	GnomeVFSURI *uri;
	EFSDir *dir;
	EFSFile *file;
};
typedef struct _FileHandle FileHandle;

static FileHandle *
file_handle_new (GnomeVFSURI *uri,
		 EFSDir *dir, EFSFile *file)
{
	FileHandle *new;

	new = g_new (FileHandle, 1);

	new->uri = gnome_vfs_uri_ref (uri);
	new->dir = dir;
	new->file = file;

	return new;
}

static void
file_handle_destroy (FileHandle *handle)
{
	gnome_vfs_uri_unref (handle->uri);
	g_free (handle);
}


static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	EFSDir *dir;
	EFSFile *file;
	mode_t efs_mode;
	gchar *file_name;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL && (strcmp(uri->parent->method_string, "file") == 0));
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if (mode & GNOME_VFS_OPEN_READ) {
		if (mode & GNOME_VFS_OPEN_WRITE)
			efs_mode = EFS_RDWR | EFS_CREATE;
		else
			efs_mode = EFS_READ;
	} else {
		if (mode & GNOME_VFS_OPEN_WRITE)
			efs_mode = EFS_WRITE | EFS_CREATE;
		else
			return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	}
	
	MAKE_ABSOLUTE (file_name, uri->parent->text);

	dir = efs_open (file_name, efs_mode, default_permissions);

	if (dir == 0)
		return gnome_vfs_result_from_errno ();

	efs_mode |= EFS_CREATE;


	file = efs_file_open (dir, uri->text, efs_mode);

	file_handle = file_handle_new (uri, dir, file);
	
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
	FileHandle *file_handle;
	EFSDir *dir;
	EFSFile *file;
	mode_t efs_mode;
	gchar *file_name;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	efs_mode = EFS_CREATE;
	
	if (!(mode & GNOME_VFS_OPEN_WRITE))
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;

	if (mode & GNOME_VFS_OPEN_READ)
		efs_mode |= EFS_RDWR;
	else
		efs_mode |= EFS_WRITE;

	if (exclusive)
		efs_mode |= EFS_EXCL;

	MAKE_ABSOLUTE (file_name, uri->parent->text);

	dir = efs_open (file_name, efs_mode, default_permissions);

	if (dir == 0)
		return gnome_vfs_result_from_errno ();

	file = efs_file_open (dir, uri->text, efs_mode);

	file_handle = file_handle_new (uri, dir, file);

	*method_handle = (GnomeVFSMethodHandle *) file_handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gint close_retval, fs_close_retval;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	close_retval = efs_file_close (file_handle->file);

	fs_close_retval = efs_close (file_handle->dir);

	/* FIXME: Should do this even after a failure?  */
	file_handle_destroy (file_handle);

	if (close_retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
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

	read_val = efs_file_read (file_handle->file, buffer, num_bytes);

	if (read_val == -1) {
		*bytes_read = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_read = read_val;
		return GNOME_VFS_OK;
	}
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

	write_val = efs_file_write (file_handle->file, (void *) buffer, num_bytes);

	if (write_val == -1) {
		*bytes_written = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_written = write_val;
		return GNOME_VFS_OK;
	}
}


static gint
seek_position_to_unix (GnomeVFSSeekPosition position)
{
	switch (position) {
	case GNOME_VFS_SEEK_START:
		return SEEK_SET;
	case GNOME_VFS_SEEK_CURRENT:
		return SEEK_CUR;
	case GNOME_VFS_SEEK_END:
		return SEEK_END;
	default:
		g_warning (_("Unknown GnomeVFSSeekPosition %d"), position);
		return SEEK_SET; /* bogus */
	}
}

static GnomeVFSResult
do_seek (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gint lseek_whence, retval;

	file_handle = (FileHandle *) method_handle;
	lseek_whence = seek_position_to_unix (whence);

	retval = efs_file_seek (file_handle->file, offset, lseek_whence);
	
	if (retval == -1)
	  return gnome_vfs_result_from_errno ();

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_tell (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
  	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}


static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSFileSize where,
		    GnomeVFSContext *context)
{
	FileHandle *file_handle;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	if (efs_file_trunc (file_handle->file, where) == 0)
		return GNOME_VFS_OK;
	else
	 	return GNOME_VFS_ERROR_GENERIC;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod *method,
	     GnomeVFSURI *uri,
	     GnomeVFSFileSize where,
	     GnomeVFSContext *context)
{
  	FileHandle *file_handle;
	GnomeVFSMethodHandle *method_handle;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL && (strcmp(uri->method_string, "file") == 0));
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if(do_open (method, &method_handle, uri, EFS_WRITE, context)) {
	 	file_handle = (FileHandle *) method_handle;
	 	if (efs_file_trunc (file_handle->file, where) == 0) {
		  	do_close (method, method_handle, context);
	   		return GNOME_VFS_OK;
	 	} else
	   		return GNOME_VFS_ERROR_GENERIC;
	} else
	  	return GNOME_VFS_ERROR_GENERIC;
}


struct _DirectoryHandle {
	GnomeVFSURI *uri;
	EFSDir *dir, *efs;
	GnomeVFSFileInfoOptions options;
	const GList *meta_keys;

	gchar *name_buffer;
	gchar *name_ptr;

	const GnomeVFSDirectoryFilter *filter;
};
typedef struct _DirectoryHandle DirectoryHandle;

static DirectoryHandle *
directory_handle_new (GnomeVFSURI *uri,
		      EFSDir *dir,
		      EFSDir *efs,
		      GnomeVFSFileInfoOptions options,
		      const GList *meta_keys,
		      const GnomeVFSDirectoryFilter *filter)
{
	DirectoryHandle *new;
	gchar *full_name;
	guint full_name_len;

	new = g_new (DirectoryHandle, 1);

	new->uri = gnome_vfs_uri_ref (uri);
	new->dir = dir;
	new->efs = efs;

	MAKE_ABSOLUTE (full_name, uri->text);
	full_name_len = strlen (full_name);

	new->name_buffer = g_malloc (full_name_len + GET_PATH_MAX () + 2);
	memcpy (new->name_buffer, full_name, full_name_len);
	
	if (full_name_len > 0 && full_name[full_name_len - 1] != '/')
		new->name_buffer[full_name_len++] = '/';

	new->name_ptr = new->name_buffer + full_name_len;

	new->options = options;
	new->meta_keys = meta_keys;
	new->filter = filter;

	return new;
}

static void
directory_handle_destroy (DirectoryHandle *directory_handle)
{
	gnome_vfs_uri_unref (directory_handle->uri);
	g_free (directory_handle->name_buffer);
	g_free (directory_handle);
}


static GnomeVFSResult
do_open_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   const GList *meta_keys,
		   const GnomeVFSDirectoryFilter *filter,
		   GnomeVFSContext *context)
{
	gchar *directory_name;
	EFSDir *dir;
	EFSDir *originaldir;

	MAKE_ABSOLUTE (directory_name, uri->parent->text);

	originaldir = efs_open (directory_name, EFS_RDWR, default_permissions);

	if (originaldir == 0)
	  	return gnome_vfs_result_from_errno ();

	dir = efs_dir_open (originaldir, uri->text, EFS_RDWR);
	if (!dir)
		return gnome_vfs_result_from_errno ();

	*method_handle
		= (GnomeVFSMethodHandle *) directory_handle_new (uri, dir, originaldir,
								 options,
								 meta_keys,
								 filter);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext *context)
{
	DirectoryHandle *directory_handle;

	directory_handle = (DirectoryHandle *) method_handle;

	efs_dir_close (directory_handle->dir);
	efs_close (directory_handle->efs);

	directory_handle_destroy (directory_handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *info,
		   GnomeVFSContext *context)
{
	DirectoryHandle *directory_handle;
	EFSDirEntry     *entry;

	directory_handle = (DirectoryHandle *) method_handle;
	if (!directory_handle || !directory_handle->dir)
		return GNOME_VFS_ERROR_INTERNAL;

	entry = efs_dir_read (directory_handle->dir);
	if (!entry)
		return GNOME_VFS_ERROR_EOF;

	info->name = g_strdup (entry->name);
	if (entry->type == EFS_DIR)
		info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
	else if (entry->type == EFS_FILE)
		info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	else
		info->type = GNOME_VFS_FILE_TYPE_UNKNOWN;

	info->size = entry->length;

	info->valid_fields =
		GNOME_VFS_FILE_INFO_FIELDS_TYPE |
		GNOME_VFS_FILE_INFO_FIELDS_SIZE;

	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
	
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}


static gboolean
do_is_local (GnomeVFSMethod *method,
	     const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	/* We are always a native filesystem.  */
	return TRUE;
}


static GnomeVFSResult
do_make_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_find_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *near_uri,
		   GnomeVFSFindDirectoryKind kind,
		   GnomeVFSURI **result_uri,
		   gboolean create_if_needed,
		   guint permissions,
		   GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *a,
		  GnomeVFSURI *b,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSMethod method = {
	do_open,
	do_create,
	do_close,
	do_read,
	do_write,
	do_seek,
	do_tell,	// not supported
	do_truncate_handle,
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,	// not supported
	do_get_file_info_from_handle,	// not supported
	do_is_local,
	do_make_directory,
	do_remove_directory,
	do_move,
	do_unlink,
	do_check_same_fs,
	do_set_file_info,
	do_truncate,
	do_find_directory
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
