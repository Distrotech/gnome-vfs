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

#include <glib.h>

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
	GnomeVFSHandle       *tmp_file;
	GnomeVFSOpenMode      open_mode;

	/* Each SeekableMethodHandle has a unique wrapper method */
	GnomeVFSMethod       *wrapper_method;
} SeekableMethodHandle;

#define CHECK_IF_SUPPORTED(method, what)		\
G_STMT_START{						\
	if (method->what == NULL)			\
		return GNOME_VFS_ERROR_NOTSUPPORTED;	\
}G_STMT_END

#define INVOKE_CHILD(result, method, what, params)	\
G_STMT_START{						\
	CHECK_IF_SUPPORTED (method->child_method, what);\
	(result) = method->child_method->what params;	\
}G_STMT_END

#define CHECK_INIT(handle)			\
G_STMT_START{					\
	if (!handle->tmp_file) {		\
		GnomeVFSResult result;		\
		result = init_seek (handle);	\
		if (result != GNOME_VFS_OK)	\
			return result;		\
	}					\
}G_STMT_END

static GnomeVFSResult
init_seek (SeekableMethodHandle *mh)
{
	GnomeVFSResult   result;
	char            *stem;
	char            *txt_uri;
	
	/* Create a temporary file name */
	if (!(stem = tmpnam (NULL)))
		return GNOME_VFS_ERROR_NOSPACE;

	txt_uri = g_strdup_printf ("file:%s", stem);

	g_warning ("Opening temp seekable file '%s'\n", txt_uri);
	
	/* Open the file */
	result = gnome_vfs_create (&mh->tmp_file, txt_uri, 
				   GNOME_VFS_OPEN_READ|GNOME_VFS_OPEN_WRITE|
				   GNOME_VFS_OPEN_RANDOM,
				   TRUE, S_IWUSR|S_IRUSR);

	g_free (txt_uri);

	if (result != GNOME_VFS_OK)
		return result;

	/* Fill the file */
	if (mh->open_mode & GNOME_VFS_OPEN_READ) {
#define BLK_SIZE 4096
		guint8           buffer[BLK_SIZE];
		GnomeVFSFileSize blk_read, blk_write;
		
		do {
			INVOKE_CHILD (result, mh, read, (mh->child_handle, buffer, BLK_SIZE, &blk_read));
			if (result != GNOME_VFS_OK)
				return result;
			result = gnome_vfs_write (mh->tmp_file, buffer, blk_read, &blk_write);
			if (result != GNOME_VFS_OK)
				return result;
			if (blk_write != blk_read)
				return GNOME_VFS_ERROR_NOSPACE;

		} while (blk_read == BLK_SIZE);
#undef  BLK_SIZE
		result = gnome_vfs_seek (mh->tmp_file, GNOME_VFS_SEEK_START, 0);
	}

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

	mh->child_handle   = child_handle;
	mh->child_method   = uri->method;
	mh->open_mode      = open_mode;
	mh->tmp_file       = NULL;
	mh->wrapper_method = m;

	uri->method        = m;

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

	if (mh->open_mode & GNOME_VFS_OPEN_WRITE) {
		g_warning ("Writeback unimplemented");
	}

	result = gnome_vfs_close (mh->tmp_file);

	g_warning ("FIXME: The temp file must be removed");

	mh->tmp_file = NULL;

	INVOKE_CHILD (result, mh, close, (mh->child_handle));

	/* Cover your back. */
	memset (mh->wrapper_method, 0xae, sizeof (GnomeVFSMethod));

	g_free (mh->wrapper_method);
	mh->wrapper_method = NULL;

	g_free (mh);

	return result;
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read)
{
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	return gnome_vfs_read (mh->tmp_file, buffer, num_bytes, bytes_read);
}

static GnomeVFSResult
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written)
{
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	return gnome_vfs_write (mh->tmp_file, buffer, num_bytes, bytes_written);
}

static GnomeVFSResult
do_seek (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset)
{
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	return gnome_vfs_seek (mh->tmp_file, whence, offset);
}

static GnomeVFSResult
do_tell (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	return gnome_vfs_tell (mh->tmp_file, offset_return);
}

static GnomeVFSResult
do_truncate (GnomeVFSMethodHandle *method_handle,
	     glong where)
{
	SeekableMethodHandle *mh = (SeekableMethodHandle *)method_handle;
	CHECK_INIT (mh);

	g_warning ("FIXME: truncate needs implementing");

	return GNOME_VFS_ERROR_NOTSUPPORTED;
}
