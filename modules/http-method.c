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

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "file-method.h"


GnomeVFSMethod *init (void);


static GnomeVFSResult	do_open		(GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode);
static GnomeVFSResult	do_close	(GnomeVFSMethodHandle *method_handle);
static GnomeVFSResult	do_read		(GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 gulong num_bytes,
					 gulong *bytes_read);
static GnomeVFSResult   do_seek		(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSSeekPosition whence,
					 glong offset);
static GnomeVFSResult	do_tell		(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSSeekPosition whence,
					 glong *offset_return);
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
	NULL,
	do_is_local,
	NULL,
	NULL
};



struct _FileHandle
{
	GnomeVFSURI   *uri;
	ghttp_request *fd;
	gboolean       open;
	glong          offset; /* FIXME: We need a nice long type */
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
      gulong num_bytes,
      gulong *bytes_read)
{
	FileHandle   *file_handle;
	const guint8 *data;
	glong         length;

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
	 glong offset)
{
	FileHandle *file_handle;
	glong       new_offset;

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
	 glong *offset_return)
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


static gboolean
do_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	/* We are always a native filesystem.  */
	return FALSE;
}

GnomeVFSMethod *
init (void)
{
	return &method;
}

