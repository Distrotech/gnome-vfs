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

   Authors: 
   	Ettore Perazzoli <ettore@comm2000.it>
   	Pavel Cisler <pavel@eazel.com>
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
#include "gnome-vfs-utils.h"
#include "gnome-vfs-module-shared.h"
#include "file-method.h"


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
		long int x = pathconf(G_DIR_SEPARATOR_S, _PC_PATH_MAX);

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

static gchar *
get_path_from_uri (GnomeVFSURI *uri)
{
	gchar *path, *longer_path;

	path = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (path == NULL)
		return NULL;

	/* This is to make sure the path starts with a "/", so that at
	 * least we get a predictable behavior when the
	 * leading "/" is not present.
	 */
	if (path[0] == G_DIR_SEPARATOR)
		return path;
	longer_path = g_strconcat (G_DIR_SEPARATOR_S, path, NULL);
	g_free (path);
	return longer_path;
}

static gchar *
get_base_from_uri (GnomeVFSURI *uri)
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
			return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	}

	if (! (mode & GNOME_VFS_OPEN_RANDOM) && (mode & GNOME_VFS_OPEN_WRITE))
		mode |= O_TRUNC;

	file_name = get_path_from_uri (uri);
	if (file_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	do
		fd = OPEN (file_name, unix_mode);
	while (fd == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));

	g_free (file_name);

	if (fd == -1)
		return gnome_vfs_result_from_errno ();

	if (fstat (fd, &statbuf) != 0)
		return gnome_vfs_result_from_errno ();

	if (S_ISDIR (statbuf.st_mode)) {
		close (fd);
		return GNOME_VFS_ERROR_IS_DIRECTORY;
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
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;

	if (mode & GNOME_VFS_OPEN_READ)
		unix_mode |= O_RDWR;
	else
		unix_mode |= O_WRONLY;

	if (exclusive)
		unix_mode |= O_EXCL;

	file_name = get_path_from_uri (uri);
	if (file_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	do
		fd = OPEN (file_name, unix_mode, perm);
	while (fd == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));

	g_free (file_name);

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

	do
		read_val = read (file_handle->fd, buffer, num_bytes);
	while (read_val == -1
	       && errno == EINTR
	       && ! gnome_vfs_context_check_cancellation (context));

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
	       && ! gnome_vfs_context_check_cancellation (context));

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
			return GNOME_VFS_ERROR_NOT_SUPPORTED;
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
			return GNOME_VFS_ERROR_NOT_SUPPORTED;
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
			return GNOME_VFS_ERROR_READ_ONLY;
		case EINVAL:
			return GNOME_VFS_ERROR_NOT_SUPPORTED;
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
	gchar *path;

	path = get_path_from_uri (uri);
	if (path == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (truncate (path, where) == 0) {
		g_free (path);
		return GNOME_VFS_OK;
	} else {
		g_free (path);
		switch (errno) {
		case EBADF:
		case EROFS:
			return GNOME_VFS_ERROR_READ_ONLY;
		case EINVAL:
			return GNOME_VFS_ERROR_NOT_SUPPORTED;
		default:
			return GNOME_VFS_ERROR_GENERIC;
		}
	}
}

typedef struct {
	GnomeVFSURI *uri;
	DIR *dir;
	GnomeVFSFileInfoOptions options;

	struct dirent *current_entry;

	gchar *name_buffer;
	gchar *name_ptr;

	const GnomeVFSDirectoryFilter *filter;
} DirectoryHandle;

static DirectoryHandle *
directory_handle_new (GnomeVFSURI *uri,
		      DIR *dir,
		      GnomeVFSFileInfoOptions options,
		      const GnomeVFSDirectoryFilter *filter)
{
	DirectoryHandle *result;
	gchar *full_name;
	guint full_name_len;

	result = g_new (DirectoryHandle, 1);

	result->uri = gnome_vfs_uri_ref (uri);
	result->dir = dir;

	/* Reserve extra space for readdir_r, see man page */
	result->current_entry = g_malloc (sizeof (struct dirent) + GET_PATH_MAX() + 1);

	full_name = get_path_from_uri (uri);
	g_assert (full_name != NULL); /* already done by caller */
	full_name_len = strlen (full_name);

	result->name_buffer = g_malloc (full_name_len + GET_PATH_MAX () + 2);
	memcpy (result->name_buffer, full_name, full_name_len);
	
	if (full_name_len > 0 && full_name[full_name_len - 1] != '/')
		result->name_buffer[full_name_len++] = '/';

	result->name_ptr = result->name_buffer + full_name_len;

	g_free (full_name);

	result->options = options;
	result->filter = filter;

	return result;
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
static void
get_mime_type (GnomeVFSFileInfo *info,
	       const char *full_name,
	       GnomeVFSFileInfoOptions options,
	       struct stat *stat_buffer)
{
	const char *mime_type;

	mime_type = NULL;
	if ((options & GNOME_VFS_FILE_INFO_FOLLOW_LINKS) == 0
		&& (info->type == GNOME_VFS_FILE_TYPE_SYMBOLIC_LINK)) {
		/* we are a symlink and aren't asked to follow -
		 * return the type for a symlink
		 */
		mime_type = "x-special/symlink";
	} else {
		mime_type = gnome_vfs_get_file_mime_type (full_name,
			stat_buffer, (options & GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE) != 0);
	}

	g_assert (mime_type);
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
		int read_size;

                read_size = readlink (full_name, buffer, size);
		if (read_size < 0) {
			return NULL;
		}
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
	gboolean followed_symlink;

	if (statptr == NULL) {
		statptr = &statbuf;
	}

	if (lstat (full_name, statptr) != 0) {
		return gnome_vfs_result_from_errno ();
	}

	if ((options & GNOME_VFS_FILE_INFO_FOLLOW_LINKS) && S_ISLNK (statptr->st_mode)) {
		if (stat (full_name, statptr) != 0) {
			if (errno == ENOENT) {
				/* its a broken symlink, revert to the lstat */
				lstat (full_name, statptr);
				followed_symlink = TRUE;
			} else {
				/* go straight to jail, do not pass GO, do not collect $200 */
				return gnome_vfs_result_from_errno();
			}
		} else {
			followed_symlink = TRUE;
		}		
	} else {
		/* follow links wasn't on, or this wasn't a symlink */
		followed_symlink = FALSE;
	}

	gnome_vfs_stat_to_file_info (file_info, statptr);
	GNOME_VFS_FILE_INFO_SET_SYMLINK (file_info, followed_symlink);

	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

	if (S_ISLNK (statptr->st_mode)) {
		file_info->type = GNOME_VFS_FILE_TYPE_SYMBOLIC_LINK;
		file_info->symlink_name = read_link (full_name);
		if (file_info->symlink_name == NULL) {
			return gnome_vfs_result_from_errno ();
		}
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME;
	}

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
		   const GnomeVFSDirectoryFilter *filter,
		   GnomeVFSContext *context)
{
	gchar *directory_name;
	DIR *dir;

	directory_name = get_path_from_uri (uri);
	if (directory_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	dir = opendir (directory_name);
	g_free (directory_name);
	if (dir == NULL)
		return gnome_vfs_result_from_errno ();

	*method_handle
		= (GnomeVFSMethodHandle *) directory_handle_new (uri, dir,
								 options,
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

	/* This makes sure we don't try to filter the file more than
           once.  */
	filter_called = FALSE;

	filter = handle->filter;
	if (filter != NULL) {
		filter_needs = gnome_vfs_directory_filter_get_needs (filter);
	} else {
		filter_needs = GNOME_VFS_DIRECTORY_FILTER_NEEDS_NOTHING;
	}

	if (readdir_r (handle->dir, handle->current_entry, &result) != 0) {
		return gnome_vfs_result_from_errno ();
	}
	
	if (result == NULL) {
		return GNOME_VFS_ERROR_EOF;
	}

	info->name = g_strdup (result->d_name);

	if (filter != NULL
	    && !filter_called
	    && (filter_needs
		  & (GNOME_VFS_DIRECTORY_FILTER_NEEDS_TYPE
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_STAT
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE)) == 0){
		if (!gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}

		filter_called = TRUE;
	}

	strcpy (handle->name_ptr, result->d_name);
	full_name = handle->name_buffer;

	/* FIXME bugzilla.eazel.com 1223: Correct?  */
	if (get_stat_info (info, full_name, handle->options, &statbuf) != GNOME_VFS_OK)
		return GNOME_VFS_ERROR_INTERNAL;

	if (filter != NULL
	    && !filter_called
	    && (filter_needs
		  & GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE) == 0) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	if (handle->options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
		get_mime_type (info, full_name, handle->options, &statbuf);
	}

	if (filter != NULL
	    && !filter_called) {
		if (!gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	if (filter != NULL && !filter_called) {
		if (!gnome_vfs_directory_filter_apply (filter, info)) {
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
		  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	gchar *full_name;
	struct stat statbuf;

	full_name = get_path_from_uri (uri);
	if (full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	file_info->name = get_base_from_uri (uri);
	g_assert (file_info->name != NULL);

	result = get_stat_info (file_info, full_name, options, &statbuf);
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

	full_name = get_path_from_uri (file_handle->uri);
	if (full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	file_info->name = get_base_from_uri (file_handle->uri);
	g_assert (file_info->name != NULL);

	result = get_stat_info_from_handle (file_info, file_handle,
					    options, &statbuf);
	if (result != GNOME_VFS_OK) {
		g_free (full_name);
		return result;
	}

	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
		get_mime_type (file_info, full_name, options, &statbuf);

	g_free (full_name);

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

	full_name = get_path_from_uri (uri);
	if (full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	retval = mkdir (full_name, perm);

	g_free (full_name);

	if (retval != 0) {
		return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	gchar *full_name;
	gint retval;

	full_name = get_path_from_uri (uri);
	if (full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	retval = rmdir (full_name);

	g_free (full_name);

	if (retval != 0) {
		return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
}

#define TRASH_DIRECTORY_NAME_BASE ".Trash"
#define MAX_TRASH_SEARCH_DEPTH 5


static char *
append_to_path (const char *path, const char *name)
{
	return g_strconcat (path, G_DIR_SEPARATOR_S, name, NULL);
}

static char *
append_trash_path (const char *path)
{	
	/* When creating trash outside of /home/pavel, create it in the form:
	 * .Trash-pavel to allow sharing the name space for several users.
	 */
	return g_strconcat (path, G_DIR_SEPARATOR_S, g_get_user_name (), 
		"-", TRASH_DIRECTORY_NAME_BASE, NULL);
}

static char *
find_trash_in_one_hierarchy_level (const char *current_directory, dev_t near_device_id, 
	GList **directory_list)
{
	char *trash_path;
	struct stat stat_buffer;
	DIR *directory;
	struct dirent *item_buffer;
	struct dirent *item;

	/* check if there is a trash in this directory */
	trash_path = append_trash_path (current_directory);
	if (lstat (trash_path, &stat_buffer) == 0 && S_ISDIR (stat_buffer.st_mode)) {
		/* found it, we are done */
		g_assert (near_device_id == stat_buffer.st_dev);
		return trash_path;
	}
	g_free (trash_path);


	/* Trash not in this directory.
	 * Collect the list of all the directories in this directory to visit later.
	 */
	directory = opendir (current_directory);
	if (directory == NULL) {
		return NULL;
	}

	item = g_malloc (sizeof (struct dirent) + GET_PATH_MAX() + 1);
	for (;;) {
		if (readdir_r (directory, item_buffer, &item) != 0 || item == NULL) {
			break;
		}
		
		if (lstat (trash_path, &stat_buffer) == 0 
			&& S_ISDIR (stat_buffer.st_mode)
			&& near_device_id == stat_buffer.st_dev) {

			/* directory -- put it on the list to search, as long as it is on the same device */
			*directory_list = g_list_prepend (*directory_list, append_to_path (current_directory, 
				item->d_name));
		}
	}

	closedir (directory);
	g_free (item_buffer);
	return NULL;
}

/* Do a width-first search of the directory hierarchy starting at start_dir,
 * looking for the trash directory. 
 * Not doing a traditional depth-first search here to prevent descending too deep in
 * the hierarchy -- we expect the Trash to be in a reasonably "shallow" location.
 * 
 * We only look MAX_TRASH_SEARCH_DEPTH deep, if the Trash is deeper in the hierarchy,
 * we will fail to find it.
 */
static char *
find_trash_in_hierarchy (const char *start_dir, dev_t near_device_id)
{
	GList *current_directory_list;
	GList *next_directory_list;
	GList *list_iterator;
	char *result;
	int level;

	next_directory_list = NULL;
	current_directory_list = NULL;

	result = find_trash_in_one_hierarchy_level (start_dir, near_device_id, &next_directory_list);

	for (level = 1; result == NULL && level < MAX_TRASH_SEARCH_DEPTH; level++) {
		gnome_vfs_list_deep_free (current_directory_list);

		current_directory_list = next_directory_list;
		next_directory_list = NULL;

		if (current_directory_list == NULL) {
			/* shallow hierarchy, bail */
			break;
		}
		for (list_iterator = current_directory_list; list_iterator->next == NULL;
			list_iterator = list_iterator->next) {
			result = find_trash_in_one_hierarchy_level (list_iterator->data, 
				near_device_id, &next_directory_list);

			if (result != NULL) {
				break;
			}
		}
	}

	gnome_vfs_list_deep_free (current_directory_list);
	gnome_vfs_list_deep_free (next_directory_list);

	return result;
}

typedef struct {
	char *path;
	dev_t device_id;
} TrashDirectoryCachedItem;

static GList *cached_trash_directories;

typedef struct {
	dev_t device_id;
} FindByDeviceIDParameters;

static int
match_trash_item_by_device_id (gconstpointer item, gconstpointer data)
{
	TrashDirectoryCachedItem *cached_item;
	FindByDeviceIDParameters *parameters;

	cached_item = (TrashDirectoryCachedItem *)item;
	parameters = (FindByDeviceIDParameters *)data;
	
	return cached_item->device_id == parameters->device_id ? 0 : -1;
}

static char *
try_creating_trash_in (const char *path, guint permissions)
{
	char *trash_path;

	trash_path = append_trash_path (path);
	if (mkdir (trash_path, permissions) == 0) {
		return trash_path;
	}

	g_free (trash_path);
	return NULL;
}

static const char *
find_trash_directory (const char *full_name_near, dev_t near_device_id, 
	gboolean create_if_needed, guint permissions, GnomeVFSContext *context)
{
	char *disk_top_directory;
	struct stat stat_buffer;
	char *current_directory;
	char *trash_path;
	TrashDirectoryCachedItem *new_cached_item;
	const char *scanner;
	int depth;
	
	disk_top_directory = g_strdup (full_name_near);

	/* Walk up in the hierarchy, finding the top-most point that still
	 * matches our device ID -- the root directory of the volume.
	 */
	for (;;) {
		char *previous_search_directory;
		char *last_slash;
		
		previous_search_directory = g_strdup (disk_top_directory);
		last_slash = strrchr (disk_top_directory, '/');
		if (last_slash == NULL) {
			g_free (previous_search_directory);
			break;
		}
		
		*last_slash = '\0';
		if (lstat (last_slash, &stat_buffer) < 0) {
			g_free (previous_search_directory);
			break;
		}

		if (stat_buffer.st_dev != near_device_id) {
			/* we ran past the root of the disk we are exploring */
			g_free (disk_top_directory);
			disk_top_directory = previous_search_directory;
			break;
		}
	}

	trash_path = find_trash_in_hierarchy (disk_top_directory, near_device_id);

	if (trash_path == NULL && create_if_needed) {
		scanner = full_name_near + strlen (disk_top_directory);
		if (*scanner != '\0') {
			/* Try creating the Trash as high up in the volume hierarchy as possible
			 * this makes it faster to find and less likely that the user will try
			 * to delete it as a part of throwing something into the Trash.
			 */
			for (depth = 1; depth < MAX_TRASH_SEARCH_DEPTH; depth++) {
				g_assert (*scanner == G_DIR_SEPARATOR);

				scanner = strchr (scanner + 1, G_DIR_SEPARATOR);
				if (scanner == NULL) {
					break;
				}
				
				current_directory = g_strdup (full_name_near);
				current_directory[scanner - full_name_near] = '\0';

				trash_path = try_creating_trash_in (current_directory, permissions);
				g_free (current_directory);
				if (trash_path != NULL) {
					break;
				}
			}
		}
		if (trash_path == NULL) {
			/* full_name_near must be the top of the disk hierarchy, we have to create a Trash in it
			 */
			 trash_path = try_creating_trash_in (full_name_near, permissions);
		}
	}

	if (trash_path != NULL) {
		/* we found or created a new trash item, save it's location for next time */
		new_cached_item = g_new (TrashDirectoryCachedItem, 1);
		new_cached_item->path = trash_path;
		new_cached_item->device_id = near_device_id;

		cached_trash_directories = g_list_prepend (cached_trash_directories, new_cached_item);
	}

	g_free (disk_top_directory);

	return trash_path;
}


static GnomeVFSResult
do_find_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *near_uri,
		   GnomeVFSFindDirectoryKind kind,
		   GnomeVFSURI **result_uri,
		   gboolean create_if_needed,
		   gboolean find_if_needed,
		   guint permissions,
		   GnomeVFSContext *context)
{
	gint retval;
	char *full_name_near;
	struct stat near_item_stat;
	struct stat home_volume_stat;
	const char *home_directory;
	char *target_directory;

	GList *matching_item;

	target_directory = NULL;
	*result_uri = NULL;

	full_name_near = get_path_from_uri (near_uri);
	if (full_name_near == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	home_directory = g_get_home_dir();

	if (gnome_vfs_context_check_cancellation (context)) {
		g_free (full_name_near);
		return GNOME_VFS_ERROR_CANCELLED;
	}

	retval = lstat (full_name_near, &near_item_stat);
	g_free (full_name_near);
	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	if (gnome_vfs_context_check_cancellation (context))
		return GNOME_VFS_ERROR_CANCELLED;

	retval = lstat (home_directory, &home_volume_stat);
	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	if (gnome_vfs_context_check_cancellation (context))
		return GNOME_VFS_ERROR_CANCELLED;


	switch (kind) {
	case GNOME_VFS_DIRECTORY_KIND_TRASH:
		if (near_item_stat.st_dev != home_volume_stat.st_dev) {
			/* This volume does not contain our home, we have to find/create the Trash
			 * elsewhere on the volume. Use a heuristic to find a good place.
			 */
			if (FALSE) {
				/* under construction */
				FindByDeviceIDParameters tmp;
				tmp.device_id = near_item_stat.st_dev;
				
				matching_item = g_list_find_custom (cached_trash_directories, 
					&tmp, match_trash_item_by_device_id);

				if (matching_item != NULL) {
					target_directory = g_strdup 
						(((TrashDirectoryCachedItem *)matching_item->data)->path);
				} else if (find_if_needed) {
					if (gnome_vfs_context_check_cancellation (context))
						return GNOME_VFS_ERROR_CANCELLED;
					target_directory = g_strdup (find_trash_directory (full_name_near,  
						near_item_stat.st_dev, create_if_needed,
						permissions, context));
				}
			}			
		} else  {
			/* volume with a home directory, just create a trash in home */
			target_directory = append_to_path (home_directory, TRASH_DIRECTORY_NAME_BASE);
		}
		break;
		
	case GNOME_VFS_DIRECTORY_KIND_DESKTOP:
		if (near_item_stat.st_dev != home_volume_stat.st_dev) {
			/* unsupported */
			break;
		}
		target_directory = append_to_path (home_directory, "Desktop");
		break;

	default:
		break;
	}


	if (target_directory == NULL)
		return GNOME_VFS_ERROR_NOT_SUPPORTED;

	if (create_if_needed && !g_file_exists (target_directory))
		mkdir (target_directory, permissions);

	if (!g_file_exists (target_directory)) {
		g_free (target_directory);
		return GNOME_VFS_ERROR_NOT_FOUND;
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
			return GNOME_VFS_ERROR_FILE_EXISTS;
		old_exists = TRUE;
	} else {
		old_exists = FALSE;
	}

	if (gnome_vfs_context_check_cancellation (context))
		return GNOME_VFS_ERROR_CANCELLED;

	retval = rename (old_full_name, new_full_name);

	/* FIXME bugzilla.eazel.com 1186: The following assumes that,
	 * if `new_uri' and `old_uri' are on different file systems,
	 * `rename()' will always return `EXDEV' instead of `EISDIR',
	 * even if the old file is not a directory while the new one
	 * is. If this is not the case, we have to stat() both the
	 * old and new file.
	 */
	if (retval != 0 && errno == EISDIR && force_replace && old_exists) {
		/* The Unix version of `rename()' fails if the original file is
		   not a directory, while the new one is.  But we have been
		   explicitly asked to replace the destination name, so if the
		   new name points to a directory, we remove it manually.  */
		if (S_ISDIR (statbuf.st_mode)) {
			if (gnome_vfs_context_check_cancellation (context))
				return GNOME_VFS_ERROR_CANCELLED;
			retval = rmdir (new_full_name);
			if (retval != 0) {
				/* FIXME bugzilla.eazel.com 1185:
				 * Maybe we could be more accurate here?
				 */
				return GNOME_VFS_ERROR_DIRECTORY_NOT_EMPTY;
			}

			if (gnome_vfs_context_check_cancellation (context))
				return GNOME_VFS_ERROR_CANCELLED;

			retval = rename (old_full_name, new_full_name);
		}
	}

	if (retval != 0) {
		return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
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
	GnomeVFSResult result;

	old_full_name = get_path_from_uri (old_uri);
	if (old_full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	new_full_name = get_path_from_uri (new_uri);
	if (new_full_name == NULL) {
		g_free (old_full_name);
		return GNOME_VFS_ERROR_INVALID_URI;
	}

	result = rename_helper (old_full_name, new_full_name,
				force_replace, context);

	g_free (old_full_name);
	g_free (new_full_name);

	return result;
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	gchar *full_name;
	gint retval;

	full_name = get_path_from_uri (uri);
	if (full_name == NULL) {
		return GNOME_VFS_ERROR_INVALID_URI;
	}

	retval = unlink (full_name);

	g_free (full_name);

	if (retval != 0) {
		return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod *method,
			 GnomeVFSURI *uri,
			 const char *target_reference,
			 GnomeVFSContext *context)
{
	const char *link_scheme, *target_scheme;
	char *link_full_name, *target_full_name;
	GnomeVFSResult result;
	GnomeVFSURI *target_uri;

	g_assert (target_reference != NULL);
	g_assert (uri != NULL);
	
	/* what we actually want is a function that takes a gchar* and tells whether it
	   is a valid URI...does such a beast exist? */
	target_uri = gnome_vfs_uri_new (target_reference);
	g_assert (target_uri != NULL);

	link_scheme = gnome_vfs_uri_get_scheme (uri);
	g_assert (link_scheme != NULL);

	target_scheme = gnome_vfs_uri_get_scheme (target_uri);
	if (target_scheme == NULL) {
		target_scheme = "file";
	}
	
	if ((strcmp (link_scheme, "file") == 0) && (strcmp (target_scheme, "file") == 0)) {
		/* symlink between two places on the local filesystem */
		if (strncmp (target_reference, "file", 4) != 0)
			/* target_reference wasn't a full URI */
			target_full_name = strdup (target_reference); 
		else 
			target_full_name = get_path_from_uri (target_uri);
		link_full_name = get_path_from_uri (uri);

		if (symlink (target_full_name, link_full_name) != 0) {
			result = gnome_vfs_result_from_errno ();
		} else {
			result = GNOME_VFS_OK;
		}

		g_free (target_full_name);
		g_free (link_full_name);
	} else {
		/* FIXME: do a URI link */
		result = GNOME_VFS_ERROR_NOT_SUPPORTED;
	}

	gnome_vfs_uri_unref (target_uri);

	return result;
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

	full_name_a = get_path_from_uri (a);
	retval = lstat (full_name_a, &sa);
	g_free (full_name_a);

	if (retval != 0)
		return gnome_vfs_result_from_errno ();

	if (gnome_vfs_context_check_cancellation (context))
		return GNOME_VFS_ERROR_CANCELLED;

	full_name_b = get_path_from_uri (b);
	retval = lstat (full_name_b, &sb);
	g_free (full_name_b);

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

	full_name = get_path_from_uri (uri);
	if (full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (mask & GNOME_VFS_SET_FILE_INFO_NAME) {
		GnomeVFSResult result;
		gchar *dir, *encoded_dir;
		gchar *new_name;

		encoded_dir = gnome_vfs_uri_extract_dirname (uri);
		dir = gnome_vfs_unescape_string (encoded_dir, G_DIR_SEPARATOR_S);
		g_free (encoded_dir);
		g_assert (dir != NULL);

		new_name = g_concat_dir_and_file (dir, info->name);

		result = rename_helper (full_name, new_name, FALSE, context);

		g_free (dir);
		g_free (new_name);

		if (result != GNOME_VFS_OK) {
			g_free (full_name);
			return result;
		}
	}

	if (gnome_vfs_context_check_cancellation (context)) {
		g_free (full_name);
		return GNOME_VFS_ERROR_CANCELLED;
	}

	if (mask & GNOME_VFS_SET_FILE_INFO_PERMISSIONS) {
		if (chmod (full_name, info->permissions) != 0) {
			g_free (full_name);
			return gnome_vfs_result_from_errno ();
		}
	}

	if (gnome_vfs_context_check_cancellation (context)) {
		g_free (full_name);
		return GNOME_VFS_ERROR_CANCELLED;
	}

	if (mask & GNOME_VFS_SET_FILE_INFO_OWNER) {
		if (chown (full_name, info->uid, info->gid) != 0) {
			g_free (full_name);
			return gnome_vfs_result_from_errno ();
		}
	}

	if (gnome_vfs_context_check_cancellation (context)) {
		g_free (full_name);
		return GNOME_VFS_ERROR_CANCELLED;
	}

	if (mask & GNOME_VFS_SET_FILE_INFO_TIME) {
		struct utimbuf utimbuf;

		utimbuf.actime = info->atime;
		utimbuf.modtime = info->mtime;

		if (utime (full_name, &utimbuf) != 0) {
			g_free (full_name);
			return gnome_vfs_result_from_errno ();
		}
	}

	g_free (full_name);

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
	do_find_directory,
	do_create_symbolic_link
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
