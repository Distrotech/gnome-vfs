/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-seekable.c - Emulation of seek / tell for non seekable filesystems.

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

   Author: Michael Meeks <michael@imaginator.com>
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

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
					 glong where);
/* Our method_handle */
typedef struct  {
	/* Child chaining info */
	GnomeVFSMethodHandle *child_handle;
	GnomeVFSMethod       *child_method;

	/* Housekeeping info */
	FILE                 *fd;
	GnomeVFSFileSize      length;
	GnomeVFSOpenMode      open_mode;
	gboolean              pre_read;
} SeekableMethodHandle;

#define CHECK_IF_SUPPORTED(method, what)		\
G_STMT_START{						\
	if (method->child_method->what == NULL)		\
		return GNOME_VFS_ERROR_NOTSUPPORTED;	\
}G_STMT_END

#define INVOKE(result, method, what, params)		\
G_STMT_START{						\
	CHECK_IF_SUPPORTED (method, what);		\
	(result) = method->child_method->what params;	\
}G_STMT_END

#define CHECK_INIT(handle)		\
G_STMT_START{				\
	if (!handle->pre_read) {       	\
		init_seek (handle);	\
		handle->pre_read = TRUE;\
	}				\
}G_STMT_END

static GnomeVFSResult
init_seek (SeekableMethodHandle *mh)
{
	GnomeVFSFileSize blk_read;
	GnomeVFSResult   result;
	guint8           buffer[4096];

	mh->length       = 0;
	do {
		INVOKE (result, mh, read, (mh->child_handle, buffer, 4096, &blk_read));
		mh->length+= blk_read;
	} while ((blk_read == 4096) &&
		 (result == GNOME_VFS_OK));

	return result;
}

GnomeVFSMethodHandle *
gnome_vfs_seek_emulate (GnomeVFSURI *uri, GnomeVFSMethodHandle *child_handle,
			GnomeVFSOpenMode open_mode)
{
	GnomeVFSMethod       *m  = g_new (GnomeVFSMethod, 1);
	SeekableMethodHandle *mh = g_new (SeekableMethodHandle, 1);
	
	g_return_val_if_fail (m != NULL, NULL);
	g_return_val_if_fail (mh != NULL, NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (uri->method != NULL, NULL);

	memcpy (m, uri->method, sizeof(GnomeVFSMethod));

        /*
	 *  This subset of method contains those operations that we need
	 * to wrap in order to extract the neccessary information for
	 * seek / tell.
	 */
	m->open     = do_open;
	m->create   = do_create;
	m->close    = do_close;
	m->read     = do_read;
	m->write    = do_write;
	m->seek     = do_seek;
	m->tell     = do_tell;
	m->truncate = do_truncate;

	mh->child_handle = child_handle;
	mh->child_method = uri->method;
	mh->open_mode    = open_mode;
	mh->fd           = tmpfile ();
	mh->pre_read     = FALSE;
	mh->length       = 0;
	uri->method      = m;

	return (GnomeVFSMethodHandle *)mh;
}

static GnomeVFSResult
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode)
{
	g_warning ("FIXME: Unhandled re-open");
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_create (GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm)
{
	g_warning ("FIXME: Unhandled re-create");
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle)
{
	GnomeVFSResult result;
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;

	/* FIXME: Chain the old method back into the URI / Handle !? */
	if (mh->fd)
		fclose (mh->fd);
	mh->fd = NULL;
	INVOKE (result, mh, close, (mh->child_handle));

	return result;
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read)
{
/*	GnomeVFSResult result;
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	gint read_val;

	CHECK_INIT (mh);

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
*/
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written)
{
/*	GnomeVFSResult result;
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	INVOKE (result, mh, write, (mh->child_handle, buffer, num_bytes, bytes_written));

	mh->offset += *bytes_written;

	return result;*/
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_seek (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset)
{
/*	GnomeVFSResult result;
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	GnomeVFSFileSize new_offset;
	CHECK_INIT (mh);
	
	switch (whence) {
	case GNOME_VFS_SEEK_START:
		new_offset = offset;
		break;
	case GNOME_VFS_SEEK_CURRENT:
		if (offset < 0 &&
		    offset > -mh->offset)
			new_offset = 0;
		else
			new_offset = mh->offset + offset;
		break;
	case GNOME_VFS_SEEK_END:
*//* FIXME: do a stat. *//*
		g_warning ("No access to the length: seekable:end unimplemented");
		return GNOME_VFS_ERROR_NOTSUPPORTED;
	default:
		return GNOME_VFS_ERROR_BAD_PARAMS;
	}

	if (new_offset >= mh->fd_size) {
		GnomeVFSFileSize extra;
		GnomeVFSFileSize bytes_read;
		guint8 *buffer;
		
		extra = new_offset - mh->fd_size;
		if (!fseek (mh->fd, SEEK_SET, mh->fd_size))
			return gnome_vfs_result_from_errno ();

		buffer = g_new (guint8, extra);
		if (!buffer)
			return GNOME_VFS_ERROR_TOOBIG;

			 *//* FIXME: need to block the read for pathalogical cases *//*
		INVOKE (result, mh, read, (mh->child_handle, buffer, extra, &bytes_read));

		if (result != GNOME_VFS_OK) {
			g_free (buffer);
			return result;
			} else if (bytes_read != extra) { *//* FIXME: should we just retry ? *//*
			g_free (buffer);
			return GNOME_VFS_ERROR_IO;
		}

		if (fwrite (buffer, 1, extra, mh->fd) != extra) {
			g_free (buffer);
			return GNOME_VFS_ERROR_NOSPACE;
		}
		g_free (buffer);
	}

	if (!fseek (mh->fd, SEEK_SET, new_offset))
		return gnome_vfs_result_from_errno ();

	mh->offset = offset;
											       */
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_tell (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
/*	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	g_return_val_if_fail (offset_return != NULL, GNOME_VFS_ERROR_BADPARAMS);

	*offset_return = mh->offset;
	*/
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethodHandle *method_handle,
	     glong where)
{
/*	GnomeVFSResult result;
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	INVOKE (result, mh, truncate, (mh->child_handle, where));

	return result;*/
	return GNOME_VFS_OK;
}

