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

   Authors: Rajit Singh   <endah@dircon.co.uk>
            Michael Meeks <mmeeks@gnu.org>
*/

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
open_efs_file (EFSDir **dir, GnomeVFSURI *uri, gint mode)
{
	GnomeVFSResult result;
	char          *fname, *bname;

	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri->parent != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri->parent->text != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (strcmp (uri->parent->method_string, "file") == 0);

	bname = uri->parent->text;
	if (bname [0] != '/')
		fname = g_strconcat ("/", bname, NULL);
	else
		fname = g_strdup (bname);

	*dir = efs_open (fname, mode, default_permissions);

	if (!(*dir))
		result = gnome_vfs_result_from_errno ();
	else
		result = GNOME_VFS_OK;

	g_free (fname);

	return result;
}

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *file_handle;
	EFSDir *dir;
	EFSFile *file;
	mode_t efs_mode;

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
	
	result = open_efs_file (&dir, uri, efs_mode);
	if (result != GNOME_VFS_OK)
		return result;

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
	GnomeVFSResult result;
	FileHandle *file_handle;
	EFSDir *dir;
	EFSFile *file;
	mode_t efs_mode;

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

	result = open_efs_file (&dir, uri, efs_mode);
	if (result != GNOME_VFS_OK)
		return result;

	if (!uri->text ||
	    strlen  (uri->text) == 0 ||
	    !strcmp (uri->text, "/")) {
		/* FIXME: so it seems we need to do something painful here */
		file = NULL;
	} else {
		file = efs_file_open (dir, uri->text, efs_mode);
		if (!file) {
			efs_close (dir);
			return GNOME_VFS_ERROR_GENERIC;
		}
	}

	file_handle = file_handle_new (uri, dir, file);

	*method_handle = (GnomeVFSMethodHandle *) file_handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle    *file_handle;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	if (efs_file_close (file_handle->file) < 0)
		result = gnome_vfs_result_from_errno ();

	else if (efs_commit (file_handle->dir) < 0)
		result = gnome_vfs_result_from_errno ();

	else if (efs_close (file_handle->dir) < 0)
		result = gnome_vfs_result_from_errno ();

	else
		result = GNOME_VFS_OK;

	file_handle_destroy (file_handle);

	return result;
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
do_tell (GnomeVFSMethod       *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset   *offset_return)
{
	FileHandle *file_handle;
	gint        retval;

	file_handle = (FileHandle *) method_handle;

	retval = efs_file_seek (file_handle->file, 0, SEEK_CUR);

	*offset_return = retval;

	if (retval == -1)
		return gnome_vfs_result_from_errno ();

	return GNOME_VFS_OK;
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

	if (do_open (method, &method_handle, uri, EFS_WRITE, context)) {
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

	if (uri->text [0] != '/')
		full_name = g_strconcat ("/", uri->text, NULL);
	else
		full_name = g_strdup (uri->text);
	full_name_len = strlen (full_name);

	new->name_buffer = g_malloc (full_name_len + GET_PATH_MAX () + 2);
	memcpy (new->name_buffer, full_name, full_name_len);
	
	if (full_name_len > 0 && full_name[full_name_len - 1] != '/')
		new->name_buffer[full_name_len++] = '/';

	new->name_ptr = new->name_buffer + full_name_len;

	new->options = options;
	new->meta_keys = meta_keys;
	new->filter = filter;

	g_free (full_name);

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
	GnomeVFSResult result;
	EFSDir        *dir;
	EFSDir        *originaldir;

	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri->text != NULL);

	result = open_efs_file (&originaldir, uri, EFS_READ);
	if (result != GNOME_VFS_OK)
		return result;

	dir = efs_dir_open (originaldir, uri->text, EFS_READ);
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

static void
transfer_dir_to_info (GnomeVFSFileInfo *info, EFSDirEntry *entry)
{
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

	transfer_dir_to_info (info, entry);

	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
	char *dir_name, *fname;
	EFSDir *originaldir;
	EFSDir *dir;
	GnomeVFSResult result;

	/*
	 * FIXME: Much of this code should be done once centraly
	 * for methods that support an embedded tree.
	 */

	/*
	 * 1. Get the directory / file names split.
	 */
	dir_name = g_strdup (uri->text?uri->text:"");
	if ((fname = strrchr (dir_name, '/'))) {
		*fname = '\0';
		fname++;
	} else {
		fname = dir_name;
		dir_name = g_strconcat ("/ ", dir_name, NULL);
		g_free (fname);
		fname = dir_name + 2;
		dir_name [1] = '\0';
	}
	
	/*
	 * 2. If we are just looking for root then; return parent data.
	 */
	if (strlen  (fname) == 0 ||
	    strlen  (dir_name) == 0 ||
	    !strcmp (dir_name, "/")) {
		g_free (dir_name);

		if (uri->parent->method->get_file_info == NULL)
			return GNOME_VFS_ERROR_NOT_SUPPORTED;

		result = uri->parent->method->get_file_info
			(uri->parent->method, uri->parent,
			 info, options, meta_keys, context);
		if (result != GNOME_VFS_OK)
			return result;

		/*
		 * Fiddle the info so it looks like a directory.
		 */
		if (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_TYPE) {
			if (info->type != GNOME_VFS_FILE_TYPE_DIRECTORY)
				info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
			else { 
				/*
				 * Ug, we can't put an efs file in a directory.
				 */ 
				return GNOME_VFS_ERROR_IS_DIRECTORY;
			}
		} else {
			info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;
			info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		}
		return GNOME_VFS_OK;
	}

	/*
	 * 3. Open the efs directory.
	 */
	result = open_efs_file (&originaldir, uri, EFS_READ);
	if (result != GNOME_VFS_OK)
		return result;

	if (!originaldir) {
		g_free (dir_name);
	  	return gnome_vfs_result_from_errno ();
	}

	dir = efs_dir_open (originaldir, dir_name, EFS_READ);
	if (!dir) {
		efs_close (originaldir);
		g_free (dir_name);
		return gnome_vfs_result_from_errno ();
	}

	/*
	 * 4. Iterate over the files
	 */
	result = GNOME_VFS_ERROR_NOT_FOUND;
	while (1) {
		EFSDirEntry     *entry;
		
		entry = efs_dir_read (dir);
		if (!entry)
			break;

		if (!strcmp (fname, entry->name)) {
			transfer_dir_to_info (info, entry);
			result = GNOME_VFS_OK;
			break;
		}
	}
	efs_dir_close (dir);
	efs_close (originaldir);
	g_free (dir_name);

	return result;
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
	GnomeVFSResult result;
	EFSDir        *dir;

	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri->text != NULL);

	result = open_efs_file (&dir, uri, EFS_RDWR | EFS_CREATE);
	if (result != GNOME_VFS_OK)
		return result;

	if (!efs_dir_open (dir, uri->text, EFS_CREATE|EFS_EXCL))
		result = GNOME_VFS_ERROR_FILE_EXISTS;
	else
		result = GNOME_VFS_OK;

	if (efs_commit (dir) < 0)
		result = GNOME_VFS_ERROR_INTERNAL;

	else if (efs_close (dir) < 0)
		result = GNOME_VFS_ERROR_INTERNAL;

	return result;
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
	GnomeVFSResult result;
	EFSDir        *dir;

	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri->text != NULL);

	result = open_efs_file (&dir, uri, EFS_RDWR);
	if (result != GNOME_VFS_OK)
		return result;

	if (!efs_erase (dir, uri->text))
		result = gnome_vfs_result_from_errno ();

	else if (efs_commit (dir) < 0)
		result = gnome_vfs_result_from_errno ();

	else if (efs_close (dir) < 0)
		result = gnome_vfs_result_from_errno ();

	else
		result = GNOME_VFS_OK;

	return result;
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
	do_tell,
	do_truncate_handle,
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,
	NULL,
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
