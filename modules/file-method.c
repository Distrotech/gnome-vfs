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

/* TODO: Error handling throughout!  */

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

#include <gnome.h>

#include "gnome-vfs-module.h"

#include "file-method.h"


static GnomeVFSResult	do_open		(GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode);
static GnomeVFSResult	do_create 	(GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode,
					 gboolean exclusive,
					 guint perm);
static GnomeVFSResult	do_close	(GnomeVFSMethodHandle *method_handle);
static GnomeVFSResult	do_read		(GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read);
static GnomeVFSResult	do_write	(GnomeVFSMethodHandle *method_handle,
					 gconstpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_written);
static GnomeVFSResult   do_seek		(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSSeekPosition whence,
					 GnomeVFSFileOffset offset);
static GnomeVFSResult	do_tell		(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileOffset *offset_return);
static GnomeVFSResult	do_truncate 	(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileSize where);

static GnomeVFSResult	do_open_directory
					(GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 const GnomeVFSDirectoryFilter *filter);
static GnomeVFSResult	do_close_directory
					(GnomeVFSMethodHandle *method_handle);
static GnomeVFSResult	do_read_directory
					(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info);

static GnomeVFSResult	do_get_file_info
					(GnomeVFSURI *uri,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);

static GnomeVFSResult	do_get_file_info_from_handle
					(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);

static gboolean		do_is_local	(const GnomeVFSURI *uri);

static GnomeVFSResult	do_make_directory
                                        (GnomeVFSURI *uri,
					 guint perm);

static GnomeVFSResult	do_remove_directory
                                        (GnomeVFSURI *uri);
static GnomeVFSResult   do_unlink       (GnomeVFSURI *uri);

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
	NULL,
	do_unlink
};


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
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode)
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
	while (fd == -1 && errno == EINTR);

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
do_create (GnomeVFSMethodHandle **method_handle,
	GnomeVFSURI *uri,
	GnomeVFSOpenMode mode,
	gboolean exclusive,
	guint perm)
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
		fd = OPEN (uri->text, unix_mode, perm);
	while (fd == -1 && errno == EINTR);

	if (fd == -1)
		return gnome_vfs_result_from_errno ();

	file_handle = file_handle_new (uri, fd);

	*method_handle = (GnomeVFSMethodHandle *) file_handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle)
{
	FileHandle *file_handle;
	gint close_retval;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	do
		close_retval = close (file_handle->fd);
	while (close_retval != 0 && errno == EINTR);

	/* FIXME: Should do this even after a failure?  */
	file_handle_destroy (file_handle);

	if (close_retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read)
{
	FileHandle *file_handle;
	gint read_val;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	do
		read_val = read (file_handle->fd, buffer, num_bytes);
	while (read_val == -1 && errno == EINTR);

	if (read_val == -1) {
		*bytes_read = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_read = read_val;
		return GNOME_VFS_OK;
	}
}

static GnomeVFSResult
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written)
{
	FileHandle *file_handle;
	gint write_val;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	do
		write_val = write (file_handle->fd, buffer, num_bytes);
	while (write_val == -1 && errno == EINTR);

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
do_seek (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset)
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
do_tell (GnomeVFSMethodHandle *method_handle,
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
do_truncate (GnomeVFSMethodHandle *method_handle,
	     GnomeVFSFileSize where)
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


struct _DirectoryHandle
{
	GnomeVFSURI *uri;
	DIR *dir;
	GnomeVFSFileInfoOptions options;
	const GList *meta_keys;

	struct dirent current_entry;

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
	guint uri_text_len;

	new = g_new (DirectoryHandle, 1);

	new->uri = gnome_vfs_uri_ref (uri);
	new->dir = dir;

	uri_text_len = strlen (uri->text);

	new->name_buffer = g_malloc (uri_text_len + GET_PATH_MAX () + 2);
	memcpy (new->name_buffer, uri->text, uri_text_len);
	
	if (uri_text_len > 0 && uri->text[uri_text_len - 1] != '/')
		new->name_buffer[uri_text_len++] = '/';

	new->name_ptr = new->name_buffer + uri_text_len;

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


/* MIME detection code.  */

/* Hm, this is a bit messy.  */

static void
set_mime_type (GnomeVFSFileInfo *info,
	       const gchar *full_name,
	       GnomeVFSFileInfoOptions options,
	       struct stat *statbuf)
{
	const gchar *mime_type;

	if (options & GNOME_VFS_FILE_INFO_FASTMIMETYPE) {
		const gchar *mime_name;

		if ((options & GNOME_VFS_FILE_INFO_FOLLOWLINKS)
		    && info->type != GNOME_VFS_FILE_TYPE_BROKENSYMLINK
		    && info->symlink_name != NULL)
			mime_name = info->symlink_name;
		else
			mime_name = full_name;

		mime_type = gnome_mime_type_or_default (mime_name, NULL);

		if (mime_type == NULL)
			mime_type = gnome_vfs_mime_type_from_mode (statbuf->st_mode);
	} else {
		/* FIXME: This will also stat the file for us...  Which is
                   not good at all, as we already have the stat info when we
                   get here, but there is no other way to do this with the
                   current gnome-libs.  */
		/* FIXME: We actually *always* follow symlinks here.  It
                   needs fixing.  */
		mime_type = gnome_mime_type_from_magic (full_name);
	}

	info->mime_type = g_strdup (mime_type);
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
                if (read_size < size)
			return buffer;
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

	if (S_ISLNK (statptr->st_mode)) {
		file_info->is_symlink = TRUE;
		file_info->symlink_name = read_link (full_name);

		if (options & GNOME_VFS_FILE_INFO_FOLLOWLINKS) {
			if (stat (full_name, statptr) != 0)
				file_info->type
					= GNOME_VFS_FILE_TYPE_BROKENSYMLINK;
		}
	}

	gnome_vfs_stat_to_file_info (file_info, statptr);

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

	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_open_directory (GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   const GList *meta_keys,
		   const GnomeVFSDirectoryFilter *filter)
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
do_close_directory (GnomeVFSMethodHandle *method_handle)
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
		gboolean *skip)
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

	if (readdir_r (handle->dir,
		       &handle->current_entry,
		       &result) != 0)
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
do_read_directory (GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info)
{
	GnomeVFSResult result;
	gboolean skip;

	do {
		result = read_directory ((DirectoryHandle *) method_handle,
					 file_info, &skip);
		if (result != GNOME_VFS_OK)
			break;
		if (skip)
			gnome_vfs_file_info_clear (file_info);
	} while (skip);

	return result;
}


static GnomeVFSResult
do_get_file_info (GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys)
{
	GnomeVFSResult result;
	gchar *full_name;
	struct stat statbuf;

	full_name = uri->text;

	file_info->name = g_strdup (g_basename (full_name));

	result = get_stat_info (file_info, full_name, options, NULL);
	if (result != GNOME_VFS_OK)
		return result;

	if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
		set_mime_type (file_info, full_name, options, &statbuf);

	gnome_vfs_set_meta_for_list (file_info, full_name, meta_keys);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys)
{
	FileHandle *file_handle;
	gchar *full_name;
	struct stat statbuf;
	GnomeVFSResult result;

	file_handle = (FileHandle *) method_handle;

	MAKE_ABSOLUTE (full_name, file_handle->uri->text);
	file_info->name = g_strdup (g_basename (full_name));

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
do_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	/* We are always a native filesystem.  */
	return TRUE;
}


static GnomeVFSResult
do_make_directory (GnomeVFSURI *uri,
		   guint perm)
{
	gint retval;

	retval = mkdir (uri->text, perm);

	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}

static GnomeVFSResult
do_remove_directory (GnomeVFSURI *uri)
{
	gint retval;

	retval = rmdir (uri->text);

	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}

static GnomeVFSResult
do_unlink (GnomeVFSURI *uri)
{
	gint retval;

	retval = unlink (uri->text);
	if (retval == 0)
		return GNOME_VFS_OK;
	else
		return gnome_vfs_result_from_errno ();
}


GnomeVFSMethod *
vfs_module_init (void)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
