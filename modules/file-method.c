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

   Author: Ettore Perazzoli <ettore@comm2000.it> */

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
#include "file-method.h"


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
	gint fd;
};
typedef struct _FileHandle FileHandle;

static FileHandle *
file_handle_new (GnomeVFSURI *uri,
		 gint fd)
{
	FileHandle *new;

	new = g_new (FileHandle, 1);

	new->uri = gnome_vfs_uri_ref (uri);
	new->fd = fd;

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
	gint fd;
	mode_t unix_mode;
	gchar *file_name;
	struct stat statbuf;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if (mode & GNOME_VFS_OPEN_READ) {
		if (mode & GNOME_VFS_OPEN_WRITE)
			unix_mode = O_RDWR;
		else
			unix_mode = O_RDONLY;
	} else {
		if (mode & GNOME_VFS_OPEN_WRITE)
			unix_mode = O_WRONLY;
		else
			return GNOME_VFS_ERROR_INVALIDOPENMODE;
	}

	if (! (mode & GNOME_VFS_OPEN_RANDOM) && (mode & GNOME_VFS_OPEN_WRITE))
		mode |= O_TRUNC;

	MAKE_ABSOLUTE (file_name, uri->text);

	do
		fd = OPEN (file_name, unix_mode);
	while (fd == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation(context));

	if (fd == -1)
		return gnome_vfs_result_from_errno ();

	if (fstat (fd, &statbuf) != 0)
		return gnome_vfs_result_from_errno ();

	if (S_ISDIR (statbuf.st_mode)) {
		close (fd);
		return GNOME_VFS_ERROR_ISDIRECTORY;
	}

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
	FileHandle *file_handle;
	gint fd;
	mode_t unix_mode;
	gchar *file_name;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	unix_mode = O_CREAT | O_TRUNC;
	
	if (!(mode & GNOME_VFS_OPEN_WRITE))
		return GNOME_VFS_ERROR_INVALIDOPENMODE;

	if (mode & GNOME_VFS_OPEN_READ)
		unix_mode |= O_RDWR;
	else
		unix_mode |= O_WRONLY;

	if (exclusive)
		unix_mode |= O_EXCL;

	MAKE_ABSOLUTE (file_name, uri->text);

	do
		fd = OPEN (file_name, unix_mode, perm);
	while (fd == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));

	if (fd == -1)
		return gnome_vfs_result_from_errno ();

	file_handle = file_handle_new (uri, fd);

	*method_handle = (GnomeVFSMethodHandle *) file_handle;

	return GNOME_VFS_OK;
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
	       && ! gnome_vfs_context_check_cancellation(context));

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

	do
		read_val = read (file_handle->fd, buffer, num_bytes);
	while (read_val == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation(context));

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

	do
		write_val = write (file_handle->fd, buffer, num_bytes);
	while (write_val == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation(context));

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
	gint lseek_whence;

	file_handle = (FileHandle *) method_handle;
	lseek_whence = seek_position_to_unix (whence);

	if (LSEEK (file_handle->fd, offset, lseek_whence) == -1) {
		if (errno == ESPIPE)
			return GNOME_VFS_ERROR_NOTSUPPORTED;
		else
			return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_tell (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
	FileHandle *file_handle;
	OFF_T offset;

	file_handle = (FileHandle *) method_handle;

	offset = LSEEK (file_handle->fd, 0, SEEK_CUR);
	if (offset == -1) {
		if (errno == ESPIPE)
			return GNOME_VFS_ERROR_NOTSUPPORTED;
		else
			return gnome_vfs_result_from_errno ();
	}

	*offset_return = offset;
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

	if (ftruncate (file_handle->fd, where) == 0) {
		return GNOME_VFS_OK;
	} else {
		switch (errno) {
		case EBADF:
		case EROFS:
			return GNOME_VFS_ERROR_READONLY;
		case EINVAL:
			return GNOME_VFS_ERROR_NOTSUPPORTED;
		default:
			return GNOME_VFS_ERROR_GENERIC;
		}
	}
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod *method,
	     GnomeVFSURI *uri,
	     GnomeVFSFileSize where,
	     GnomeVFSContext *context)
{
	if (truncate (uri->text, where) == 0) {
		return GNOME_VFS_OK;
	} else {
		switch (errno) {
		case EBADF:
		case EROFS:
			return GNOME_VFS_ERROR_READONLY;
		case EINVAL:
			return GNOME_VFS_ERROR_NOTSUPPORTED;
		default:
			return GNOME_VFS_ERROR_GENERIC;
		}
	}
}


struct _DirectoryHandle
{
	GnomeVFSURI *uri;
	DIR *dir;
	GnomeVFSFileInfoOptions options;
	const GList *meta_keys;

	struct dirent *current_entry;

	gchar *name_buffer;
	gchar *name_ptr;

	const GnomeVFSDirectoryFilter *filter;
};
typedef struct _DirectoryHandle DirectoryHandle;

static DirectoryHandle *
directory_handle_new (GnomeVFSURI *uri,
		      DIR *dir,
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

	/* Reserve extra space for readdir_r, see man page */
	new->current_entry = g_malloc (sizeof (struct dirent) + GET_PATH_MAX() + 1);

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
	g_free (directory_handle->current_entry);
	g_free (directory_handle);
}


/* MIME detection code.  */

/* Hm, this is a bit messy.  */

static void
set_mime_type (GnomeVFSFileInfo *info,
	       const gchar *full_name,
	       GnomeVFSFileInfoOptions options,
	       struct stat *statbuf)
{
	const gchar *mime_type, *mime_name;

	mime_type = NULL;
	if ((options & GNOME_VFS_FILE_INFO_FASTMIMETYPE) == 0) {
		/* FIXME: This will also stat the file for us...  Which is
                   not good at all, as we already have the stat info when we
                   get here, but there is no other way to do this with the
                   current gnome-libs.  */
		/* FIXME: We actually *always* follow symlinks here.  It
                   needs fixing.  */
		mime_type = gnome_vfs_mime_type_from_magic (full_name);
	}

	if (mime_type == NULL) {
		if ((options & GNOME_VFS_FILE_INFO_FOLLOWLINKS)
		    && info->type != GNOME_VFS_FILE_TYPE_BROKENSYMLINK
		    && info->symlink_name != NULL)
			mime_name = info->symlink_name;
		else
			mime_name = full_name;

		mime_type = gnome_vfs_mime_type_or_default (mime_name, NULL);

		if (mime_type == NULL)
			mime_type = gnome_vfs_mime_type_from_mode (statbuf->st_mode);
	}

	info->mime_type = g_strdup (mime_type);
	info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
}


static gchar *
read_link (const gchar *full_name)
{
	gchar *buffer;
	guint size;

	size = 256;
	buffer = g_malloc (size);
          
	while (1) {
		guint read_size;

                read_size = readlink (full_name, buffer, size);
                if (read_size < size) {
			buffer[read_size] = 0;
			return buffer;
		}
                size *= 2;
		buffer = g_realloc (buffer, size);
	}
}

static GnomeVFSResult
get_stat_info (GnomeVFSFileInfo *file_info,
	       const gchar *full_name,
	       GnomeVFSFileInfoOptions options,
	       struct stat *statptr)
{
	struct stat statbuf;

	if (statptr == NULL)
		statptr = &statbuf;

	if (lstat (full_name, statptr) != 0)
		return gnome_vfs_result_from_errno ();

	if (!S_ISLNK (statptr->st_mode)) {
		GNOME_VFS_FILE_INFO_SET_SYMLINK (file_info, FALSE);
	} else {
		GNOME_VFS_FILE_INFO_SET_SYMLINK (file_info, TRUE);
		file_info->symlink_name = read_link (full_name);
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME;

		if (options & GNOME_VFS_FILE_INFO_FOLLOWLINKS) {
			if (stat (full_name, statptr) != 0)
				file_info->type
					= GNOME_VFS_FILE_TYPE_BROKENSYMLINK;
		}
	}

	gnome_vfs_stat_to_file_info (file_info, statptr);
	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
get_stat_info_from_handle (GnomeVFSFileInfo *file_info,
			   FileHandle *handle,
			   GnomeVFSFileInfoOptions options,
			   struct stat *statptr)
{
	struct stat statbuf;

	if (statptr == NULL)
		statptr = &statbuf;

	if (fstat (handle->fd, statptr) != 0)
		return gnome_vfs_result_from_errno ();

	gnome_vfs_stat_to_file_info (file_info, statptr);
	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

	return GNOME_VFS_OK;
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
	DIR *dir;

	MAKE_ABSOLUTE (directory_name, uri->text);

	dir = opendir (directory_name);
	if (dir == NULL)
		return gnome_vfs_result_from_errno ();

	*method_handle
		= (GnomeVFSMethodHandle *) directory_handle_new (uri, dir,
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

	closedir (directory_handle->dir);

	directory_handle_destroy (directory_handle);

	return GNOME_VFS_OK;
}

inline static GnomeVFSResult
read_directory (DirectoryHandle *handle,
		GnomeVFSFileInfo *info,
		gboolean *skip,
		GnomeVFSContext *context)
{
	const GnomeVFSDirectoryFilter *filter;
	GnomeVFSDirectoryFilterNeeds filter_needs;
	struct dirent *result;
	struct stat statbuf;
	gchar *full_name;
	gboolean filter_called;

	/* This makes sure we do try to filter the file more than
           once.  */
	filter_called = FALSE;

	filter = handle->filter;
	if (filter != NULL) {
		filter_needs = gnome_vfs_directory_filter_get_needs (filter);
	} else {
		/* Shut up stupid compiler.  */
		filter_needs = GNOME_VFS_DIRECTORY_FILTER_NEEDS_NOTHING;
	}

	if (readdir_r (handle->dir, handle->current_entry, &result) != 0)
		return gnome_vfs_result_from_errno ();

	if (result == NULL)
		return GNOME_VFS_ERROR_EOF;

	info->name = g_strdup (result->d_name);

	if (filter != NULL
	    && ! filter_called
	    && ! (filter_needs
		  & (GNOME_VFS_DIRECTORY_FILTER_NEEDS_TYPE
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_STAT
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA))) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}

		filter_called = TRUE;
	}

	strcpy (handle->name_ptr, result->d_name);
	full_name = handle->name_buffer;

	/* FIXME: Correct?  */
	if (get_stat_info (info, full_name, handle->options, &statbuf)
	    != GNOME_VFS_OK)
		return GNOME_VFS_ERROR_INTERNAL;

	if (filter != NULL
	    && ! filter_called
	    && ! (filter_needs
		  & (GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA))) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	if (handle->options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
		set_mime_type (info, full_name, handle->options, &statbuf);

	if (filter != NULL
	    && ! filter_called
	    && ! (filter_needs & GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA)) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	gnome_vfs_set_meta_for_list (info, full_name, handle->meta_keys);

	if (filter != NULL && ! filter_called) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	*skip = FALSE;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	GnomeVFSResult result;
	gboolean skip;

	do {
		result = read_directory ((DirectoryHandle *) method_handle,
					 file_info, &skip, context);
		if (result != GNOME_VFS_OK)
			break;
		if (skip)
			gnome_vfs_file_info_clear (file_info);
	} while (skip);

	return result;
}


static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	gchar *full_name;
	struct stat statbuf;

	MAKE_ABSOLUTE (full_name, uri->text);

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	file_info->name = gnome_vfs_uri_extract_short_name (uri);

	result = get_stat_info (file_info, full_name, options, &statbuf);
	if (result != GNOME_VFS_OK)
		return result;

	if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
		set_mime_type (file_info, full_name, options, &statbuf);

	gnome_vfs_set_meta_for_list (file_info, full_name, meta_keys);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gchar *full_name;
	struct stat statbuf;
	GnomeVFSResult result;

	file_handle = (FileHandle *) method_handle;

	MAKE_ABSOLUTE (full_name, file_handle->uri->text);

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	file_info->name = gnome_vfs_uri_extract_short_name (file_handle->uri);

	result = get_stat_info_from_handle (file_info, file_handle,
					    options, &statbuf);
	if (result != GNOME_VFS_OK)
		return result;

	if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
		set_mime_type (file_info, full_name, options, &statbuf);

	gnome_vfs_set_meta_for_list (file_info, full_name, meta_keys);

	return GNOME_VFS_OK;
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
	gint retval;
	gchar *full_name;

	MAKE_ABSOLUTE (full_name, uri->text);
	retval = mkdir (full_name, perm);

	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	gchar *full_name;
	gint retval;

	MAKE_ABSOLUTE (full_name, uri->text);
	retval = rmdir (full_name);

	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
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
	gint retval;
	char *full_name_near;
	struct stat near_item_stat;
	struct stat home_volume_stat;
	const char *home_directory;
	char *target_directory;

	target_directory = NULL;
	*result_uri = NULL;
	full_name_near = NULL;

	MAKE_ABSOLUTE (full_name_near, near_uri->text);
	home_directory = g_get_home_dir();

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	retval = stat (full_name_near, &near_item_stat);
	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	retval = stat (home_directory, &home_volume_stat);
	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	if (near_item_stat.st_dev != home_volume_stat.st_dev)
		/* FIXME:
		 */
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	switch (kind) {
	case GNOME_VFS_DIRECTORY_KIND_TRASH:
		target_directory = g_strconcat(home_directory, G_DIR_SEPARATOR_S, 
			"Trash", NULL);
		break;
		
	case GNOME_VFS_DIRECTORY_KIND_DESKTOP:
		target_directory = g_strconcat(home_directory, G_DIR_SEPARATOR_S, 
			"Desktop", NULL);
		break;

	default:
		break;
	}

	if (target_directory == NULL)
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	if (create_if_needed && !g_file_exists (target_directory))
		mkdir (target_directory, permissions);

	if (!g_file_exists (target_directory)) {
		g_free (target_directory);
		return GNOME_VFS_ERROR_NOTFOUND;
	}
	*result_uri = gnome_vfs_uri_new (target_directory);
	g_free (target_directory);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
rename_helper (const gchar *old_full_name,
	       const gchar *new_full_name,
	       gboolean force_replace,
	       GnomeVFSContext *context)
{
	gboolean old_exists;
	struct stat statbuf;
	gint retval;

	retval = stat (new_full_name, &statbuf);
	if (retval == 0) {
		/* If we are not allowed to replace an existing file, return an
                   error.  */
		if (! force_replace)
			return GNOME_VFS_ERROR_FILEEXISTS;
		old_exists = TRUE;
	} else {
		old_exists = FALSE;
	}

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	retval = rename (old_full_name, new_full_name);

	/* FIXME?  The following assumes that, if `new_uri' and `old_uri' are
           on different file systems, `rename()' will always return `EXDEV'
           instead of `EISDIR', even if the old file is not a directory while
           the new one is.  If this is not the case, we have to stat() both
	   the old and new file.  */

	if (retval != 0 && errno == EISDIR && force_replace && old_exists) {
		/* The Unix version of `rename()' fails if the original file is
		   not a directory, while the new one is.  But we have been
		   explicitly asked to replace the destination name, so if the
		   new name points to a directory, we remove it manually.  */
		if (S_ISDIR (statbuf.st_mode)) {
			if (gnome_vfs_context_check_cancellation(context))
				return GNOME_VFS_ERROR_CANCELLED;
			retval = rmdir (new_full_name);
			if (retval != 0) {
				/* FIXME: Maybe we could be more accurate
				   here?  */
				return GNOME_VFS_ERROR_DIRECTORYNOTEMPTY;
			}

			if (gnome_vfs_context_check_cancellation(context))
				return GNOME_VFS_ERROR_CANCELLED;

			retval = rename (old_full_name, new_full_name);
		}
	}

	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	gchar *old_full_name;
	gchar *new_full_name;

	MAKE_ABSOLUTE (new_full_name, new_uri->text);
	MAKE_ABSOLUTE (old_full_name, old_uri->text);

	return rename_helper (old_full_name, new_full_name, force_replace,
			      context);
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	gchar *full_name;
	gint retval;

	MAKE_ABSOLUTE (full_name, uri->text);
	retval = unlink (full_name);
	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *a,
		  GnomeVFSURI *b,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{
	gchar *full_name_a, *full_name_b;
	struct stat sa, sb;
	gint retval;

	MAKE_ABSOLUTE (full_name_a, a->text);
	MAKE_ABSOLUTE (full_name_b, b->text);

	retval = stat (full_name_a, &sa);
	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	retval = stat (full_name_b, &sb);

	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	*same_fs_return = (sa.st_dev == sb.st_dev);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{
	gchar *full_name;

	MAKE_ABSOLUTE (full_name, uri->text);

	if (mask & GNOME_VFS_SET_FILE_INFO_NAME) {
		GnomeVFSResult result;
		gchar *dir;
		gchar *new, *old;

		old = full_name;

		dir = gnome_vfs_uri_extract_dirname (uri);
		new = g_concat_dir_and_file (dir, info->name);

		result = rename_helper (old, new, FALSE, context);

		g_free (dir);
		g_free (new);

		if (result != GNOME_VFS_OK)
			return result;
	}

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	if (mask & GNOME_VFS_SET_FILE_INFO_PERMISSIONS) {
		if (chmod (full_name, info->permissions) != 0)
			return gnome_vfs_result_from_errno ();
	}

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	if (mask & GNOME_VFS_SET_FILE_INFO_OWNER) {
		if (chown (full_name, info->uid, info->gid) != 0)
			return gnome_vfs_result_from_errno ();
	}

	if (gnome_vfs_context_check_cancellation(context))
		return GNOME_VFS_ERROR_CANCELLED;

	if (mask & GNOME_VFS_SET_FILE_INFO_TIME) {
		struct utimbuf utimbuf;

		utimbuf.actime = info->atime;
		utimbuf.modtime = info->mtime;

		if (utime (full_name, &utimbuf) != 0)
			return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
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
	do_get_file_info_from_handle,
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
