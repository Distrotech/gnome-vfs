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

   Author: Michael Meeks <michael@imaginator.com> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome.h>

#include <ghttp.h>

#include "gnome-vfs-module.h"

#include "file-method.h"


GnomeVFSMethod *init (void);


static GnomeVFSResult	do_open		(GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode);
static GnomeVFSResult	do_close	(GnomeVFSMethodHandle *method_handle);
static GnomeVFSResult	do_read		(GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read);
static GnomeVFSResult   do_seek		(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSSeekPosition whence,
					 GnomeVFSFileOffset offset);
static GnomeVFSResult	do_tell		(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSSeekPosition whence,
					 GnomeVFSFileOffset *offset_return);
static GnomeVFSResult	do_get_file_info
					(GnomeVFSURI *uri,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);
static gboolean		do_is_local	(const GnomeVFSURI *uri);

static GnomeVFSMethod method = {
	do_open,
	NULL,
	do_close,
	do_read,
	NULL,
	do_seek,
	do_tell,
	NULL,
	NULL,
	NULL,
	NULL,
	do_get_file_info,
	do_is_local,
	NULL,
	NULL
};



struct _FileHandle
{
	GnomeVFSURI   *uri;
	ghttp_request *fd;
	gboolean       open;
	GnomeVFSFileOffset          offset; /* FIXME: We need a nice long type */
};
typedef struct _FileHandle FileHandle;

static int
file_fetch (FileHandle *fh)
{
	if (!fh || !fh->fd)
		return 0;

	ghttp_prepare (fh->fd);
	ghttp_process (fh->fd);

	return 1;
}

#define HANDLE_DATA(fh)     ((const guint8 *)ghttp_get_body ((fh)->fd))
#define HANDLE_LENGTH(fh)   (ghttp_get_body_len ((fh)->fd))
#define HANDLE_VALIDATE(fh)		\
G_STMT_START {				\
	if ((fh) && !(fh)->open)	\
		file_fetch (fh);	\
}G_STMT_END

static FileHandle *
file_handle_new (GnomeVFSURI *uri,
		 ghttp_request *fd)
{
	FileHandle *new;

	new = g_new (FileHandle, 1);

	new->uri    = gnome_vfs_uri_ref (uri);
	new->fd     = fd;
	new->open   = FALSE;
	new->offset = 0;

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
	FileHandle    *new;
	ghttp_request *fd = NULL;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if (mode & GNOME_VFS_OPEN_READ) {
		fd = ghttp_request_new ();
		if (ghttp_set_uri (fd, uri->text) < 0) {
			ghttp_request_destroy (fd);
			return GNOME_VFS_ERROR_INVALIDURI;
		}
		ghttp_set_header (fd, http_hdr_Connection, "close");
	} else
		return GNOME_VFS_ERROR_INVALIDOPENMODE;
	
	new = file_handle_new (uri, fd);
	*method_handle = (GnomeVFSMethodHandle *) new;
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle)
{
	FileHandle *file_handle;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	ghttp_request_destroy (file_handle->fd);

	file_handle_destroy (file_handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
      gpointer buffer,
      GnomeVFSFileSize num_bytes,
      GnomeVFSFileSize *bytes_read)
{
	FileHandle   *file_handle;
	const guint8 *data;
	GnomeVFSFileOffset         length;

	g_return_val_if_fail (buffer        != NULL, GNOME_VFS_ERROR_INTERNAL);
	g_return_val_if_fail (bytes_read    != NULL, GNOME_VFS_ERROR_INTERNAL);
	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;
	HANDLE_VALIDATE (file_handle);

	data = HANDLE_DATA (file_handle);
	data+= file_handle->offset;

	if (file_handle->offset >= HANDLE_LENGTH (file_handle))
		return GNOME_VFS_ERROR_EOF;

	length = num_bytes;
	if (file_handle->offset + length < HANDLE_LENGTH (file_handle))
		length = HANDLE_LENGTH (file_handle) - file_handle->offset;

	memcpy (buffer, data, length);
	*bytes_read = length;

	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_seek (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset)
{
	FileHandle *file_handle;
	GnomeVFSFileOffset       new_offset;

	g_return_val_if_fail (method_handle != NULL,
			      GNOME_VFS_ERROR_INTERNAL);
	file_handle = (FileHandle *) method_handle;

	HANDLE_VALIDATE (file_handle);

	switch (whence) {
	case GNOME_VFS_SEEK_START:
		new_offset = offset;
		break;
	case GNOME_VFS_SEEK_CURRENT:
		new_offset+= offset;
		break;
	case GNOME_VFS_SEEK_END:
		new_offset = offset;
		break;
	default:
		return GNOME_VFS_ERROR_NOTSUPPORTED;
	}
	
	if (new_offset < 0 ||
	    new_offset > HANDLE_LENGTH (file_handle))
		return GNOME_VFS_ERROR_GENERIC;

	file_handle->offset = new_offset;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_tell (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset *offset_return)
{
	FileHandle *file_handle;

	file_handle = (FileHandle *) method_handle;
	g_return_val_if_fail (file_handle != NULL,
			      GNOME_VFS_ERROR_INTERNAL);
	g_return_val_if_fail (offset_return != NULL,
			      GNOME_VFS_ERROR_INTERNAL);
	
	*offset_return = file_handle->offset;

	return GNOME_VFS_OK;
}

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
			mime_type = mime_type_from_mode (statbuf->st_mode);
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

	if (S_ISDIR (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
	else if (S_ISCHR (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_CHARDEVICE;
	else if (S_ISBLK (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_BLOCKDEVICE;
	else if (S_ISFIFO (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_FIFO;
	else if (S_ISSOCK (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_SOCKET;
	else if (S_ISREG (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	else if (! file_info->is_symlink)
		file_info->type = GNOME_VFS_FILE_TYPE_UNKNOWN;

	file_info->permissions
		= statptr->st_mode & (S_IRUSR | S_IWUSR | S_IXUSR
				     | S_IRGRP | S_IWGRP | S_IXGRP
				     | S_IROTH | S_IWOTH | S_IXOTH);

	file_info->device = statptr->st_dev;
	file_info->inode = statptr->st_ino;

	file_info->link_count = statptr->st_nlink;

	file_info->uid = statptr->st_uid;
	file_info->gid = statptr->st_gid;

	file_info->size = statptr->st_size;
	file_info->block_count = statptr->st_blocks;
	file_info->io_block_size = statptr->st_blksize;

	file_info->atime = statptr->st_atime;
	file_info->ctime = statptr->st_ctime;
	file_info->mtime = statptr->st_mtime;

	file_info->is_local = TRUE;
	file_info->is_suid = (statptr->st_mode & S_ISUID) ? TRUE : FALSE;
	file_info->is_sgid = (statptr->st_mode & S_ISGID) ? TRUE : FALSE;

#ifdef S_ISVTX
	file_info->has_sticky_bit
		= (statptr->st_mode & S_ISVTX) ? TRUE : FALSE;
#else
	file_info->has_sticky_bit = FALSE;
#endif

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys)
{
	GnomeVFSResult result;

	result = get_stat_info (file_info, uri->text, options, &statbuf);
	if (result != GNOME_VFS_OK)
		return result;

	if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
		set_mime_type (file_info, full_name, options, &statbuf);

	set_meta_for_list (file_info, full_name, meta_keys);

	return GNOME_VFS_OK;
}

static gboolean
do_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	/* We are always a native filesystem.  */
	return FALSE;
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

