/*
 * bzip2-method.c - Bzip2 access method for the GNOME Virtual File
 *                  System.
 *
 * Copyright (C) 1999 Free Software Foundation
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite
 * 330, Boston, MA 02111-1307, USA.
 *
 * Author: Cody Russell <bratsche@dfw.net>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <bzlib.h>
#include <time.h>

#include "gnome-vfs-module.h"
#include "bzip2-method.h"

#define BZ_BUFSIZE   5000

struct _Bzip2MethodHandle {
	GnomeVFSURI      *uri;
	GnomeVFSHandle   *parent_handle;
	GnomeVFSOpenMode open_mode;

	BZFILE           *file;
	GnomeVFSResult   last_vfs_result;
	gint             last_bz_result;
	bz_stream        bzstream;
	guchar           *buffer;
};

typedef struct _Bzip2MethodHandle Bzip2MethodHandle;

static GnomeVFSResult do_open (GnomeVFSMethodHandle **method_handle,
			       GnomeVFSURI *uri,
			       GnomeVFSOpenMode mode,
			       GnomeVFSContext *context);

static GnomeVFSResult do_create (GnomeVFSMethodHandle **method_handle,
				 GnomeVFSURI *uri,
				 GnomeVFSOpenMode mode,
				 gboolean exclusive,
				 guint perm,
				 GnomeVFSContext *context);

static GnomeVFSResult do_close (GnomeVFSMethodHandle *method_handle,
				GnomeVFSContext *context);

static GnomeVFSResult do_read (GnomeVFSMethodHandle *method_handle,
			       gpointer buffer,
			       GnomeVFSFileSize num_bytes,
			       GnomeVFSFileSize *bytes_read,
			       GnomeVFSContext *context);

static GnomeVFSResult do_write (GnomeVFSMethodHandle *method_handle,
				gconstpointer buffer,
				GnomeVFSFileSize num_bytes,
				GnomeVFSFileSize *bytes_written,
				GnomeVFSContext *context);

static gboolean do_is_local (const GnomeVFSURI *uri);

static GnomeVFSMethod method = {
	do_open,
	do_create,
	do_close,
	do_read,
	do_write,		/* write           */
	NULL,		/* seek            */
	NULL,		/* tell            */
	NULL,		/* truncate FIXME  */
	NULL,		/* open_directory  */
	NULL,		/* close_directory */
	NULL,		/* read_directory  */
	NULL,		/* get_file_info   */
	NULL,		/* get_file_info_from_handle */
	do_is_local,
	NULL,		/* make_directory  */
	NULL,		/* remove_directory */
	NULL			/* rename */
};

#define RETURN_IF_FAIL(action)			\
G_STMT_START {					\
	GnomeVFSResult __tmp_result;		\
						\
	__tmp_result = (action);		\
	if (__tmp_result != GNOME_VFS_OK)	\
		return __tmp_result;		\
} G_STMT_END

static Bzip2MethodHandle *
bzip2_method_handle_new (GnomeVFSHandle *parent_handle,
			 GnomeVFSURI *uri,
			 GnomeVFSOpenMode open_mode)
{
	Bzip2MethodHandle *new;

	new = g_new (Bzip2MethodHandle, 1);

	new->parent_handle = parent_handle;
	new->uri = gnome_vfs_uri_ref (uri);
	new->open_mode = open_mode;

	new->buffer = NULL;

	return new;
}

static void
bzip2_method_handle_destroy (Bzip2MethodHandle *handle)
{
	gnome_vfs_uri_unref (handle->uri);
	g_free (handle->buffer);
	g_free (handle);
}

static gboolean
bzip2_method_handle_init_for_decompress (Bzip2MethodHandle *handle)
{
	handle->bzstream.bzalloc = NULL;
	handle->bzstream.bzfree  = NULL;
	handle->bzstream.opaque  = NULL;

	g_free (handle->buffer);

	handle->buffer = g_malloc (BZ_BUFSIZE);
	handle->bzstream.next_in = handle->buffer;
	handle->bzstream.avail_in = 0;

	/* FIXME: Make small, and possibly verbosity, configurable! */
	if (bzDecompressInit (&handle->bzstream, 0, 0) != BZ_OK) {
		g_free (handle->buffer);
		return FALSE;
	}

	handle->last_bz_result = BZ_OK;
	handle->last_vfs_result = GNOME_VFS_OK;

	return TRUE;
}

static gboolean
bzip2_method_handle_init_for_compress (Bzip2MethodHandle *handle)
{
	handle->bzstream.bzalloc = NULL;
	handle->bzstream.bzfree  = NULL;
	handle->bzstream.opaque  = NULL;

	g_free (handle->buffer);

	handle->buffer = g_malloc (BZ_BUFSIZE);
	handle->bzstream.next_out = handle->buffer;
	handle->bzstream.avail_out = BZ_BUFSIZE;

	/* FIXME: We want this to be user configurable.  */
	if (bzCompressInit (&handle->bzstream, 3, 0, 30) != BZ_OK) {
		g_free (handle->buffer);
		return FALSE;
	}

	handle->last_bz_result = BZ_OK;
	handle->last_vfs_result = GNOME_VFS_OK;

	return TRUE;
}

static GnomeVFSResult
result_from_bz_result (gint bz_result)
{
	switch (bz_result) {
	case BZ_OK:
	case BZ_STREAM_END:
		return GNOME_VFS_OK;

	case BZ_MEM_ERROR:
		return GNOME_VFS_ERROR_NOMEM;

	case BZ_PARAM_ERROR:
		return GNOME_VFS_ERROR_BADPARAMS;

	case BZ_DATA_ERROR:
		return GNOME_VFS_ERROR_CORRUPTEDDATA;

	case BZ_UNEXPECTED_EOF:
		return GNOME_VFS_ERROR_EOF;

	case BZ_SEQUENCE_ERROR:
		return GNOME_VFS_ERROR_NOTPERMITTED;

	default:
		return GNOME_VFS_ERROR_INTERNAL;
	}
}

static GnomeVFSResult
flush_write (Bzip2MethodHandle *bzip2_handle)
{
	GnomeVFSHandle *parent_handle;
	GnomeVFSResult result;
	gboolean done;
	bz_stream *bzstream;
	gint bz_result;

	bzstream = &bzip2_handle->bzstream;
	bzstream->avail_in = 0;
	parent_handle = bzip2_handle->parent_handle;

	done = FALSE;
	bz_result = BZ_OK;
	while (bz_result == BZ_OK || bz_result == BZ_STREAM_END) {
		GnomeVFSFileSize bytes_written;
		GnomeVFSFileSize len;

		len = BZ_BUFSIZE - bzstream->avail_out;

		result = gnome_vfs_write (parent_handle, bzip2_handle->buffer,
					  len, &bytes_written);
		RETURN_IF_FAIL (result);

		bzstream->next_out = bzip2_handle->buffer;
		bzstream->avail_out = BZ_BUFSIZE;

		if (done)
			break;

		bz_result = bzCompress (bzstream, BZ_FINISH);

		done = (bzstream->avail_out != 0 || bz_result == BZ_STREAM_END);
	}

	if (bz_result == BZ_OK || bz_result == BZ_STREAM_END)
		return GNOME_VFS_OK;
	else
		return result_from_bz_result (bz_result);
}

/* Open */
/* TODO: Check that there is no subpath. */

static GnomeVFSResult
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode open_mode,
	 GnomeVFSContext *context)
{
	GnomeVFSHandle *parent_handle;
	GnomeVFSURI *parent_uri;
	GnomeVFSResult result;
	Bzip2MethodHandle *bzip2_handle;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if (open_mode & GNOME_VFS_OPEN_WRITE)
		return GNOME_VFS_ERROR_INVALIDOPENMODE;

	/* We don't allow any paths in the bzip2 file. */
	if (uri->text != NULL && uri->text[0] != 0)
		return GNOME_VFS_ERROR_INVALIDURI;

	parent_uri = uri->parent;

	if (open_mode & GNOME_VFS_OPEN_RANDOM)
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	result = gnome_vfs_open_uri (&parent_handle, parent_uri, open_mode);
	RETURN_IF_FAIL (result);

	bzip2_handle = bzip2_method_handle_new (parent_handle, uri, open_mode);

	if (result != GNOME_VFS_OK) {
		gnome_vfs_close (parent_handle);
		bzip2_method_handle_destroy (bzip2_handle);
		return result;
	}

	if (!bzip2_method_handle_init_for_decompress (bzip2_handle)) {
		gnome_vfs_close (parent_handle);
		bzip2_method_handle_destroy (bzip2_handle);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	*method_handle = (GnomeVFSMethodHandle *) bzip2_handle;

	return GNOME_VFS_OK;
}

/* Create */

static GnomeVFSResult
do_create (GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm,
	   GnomeVFSContext *context)
{
	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

/* Close */

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	Bzip2MethodHandle *bzip2_handle;
	GnomeVFSResult result;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);

	bzip2_handle = (Bzip2MethodHandle *) method_handle;

	if (bzip2_handle->open_mode & GNOME_VFS_OPEN_WRITE)
		result = flush_write (bzip2_handle);
	else
		result = GNOME_VFS_OK;

	if (result == GNOME_VFS_OK)
		result = gnome_vfs_close (bzip2_handle->parent_handle);

	/* FIXME: How do we deal with close errors? */

	bzip2_method_handle_destroy (bzip2_handle);

	return result;
}

/* Read */

static GnomeVFSResult
fill_buffer (Bzip2MethodHandle *bzip2_handle,
	     GnomeVFSFileSize num_bytes)
{
	GnomeVFSResult result;
	GnomeVFSFileSize count;
	bz_stream *bzstream;

	bzstream = &bzip2_handle->bzstream;

	if (bzstream->avail_in > 0)
		return GNOME_VFS_OK;

	result = gnome_vfs_read (bzip2_handle->parent_handle,
				 bzip2_handle->buffer,
				 BZ_BUFSIZE,
				 &count);

	if (result != GNOME_VFS_OK) {
		if (bzstream->avail_out == num_bytes)
			return result;
		bzip2_handle->last_vfs_result = result;
	} else {
		bzstream->next_in = bzip2_handle->buffer;
		bzstream->avail_in = count;
	}

	return GNOME_VFS_OK;
}

/* TODO: Concatenated Bzip2 file handling. */

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	Bzip2MethodHandle *bzip2_handle;
	GnomeVFSResult result;
	bz_stream *bzstream;
	int bz_result;

	/* FIXME: Wrong. */
	*bytes_read = 0;

	bzip2_handle = (Bzip2MethodHandle *) method_handle;
	bzstream = &bzip2_handle->bzstream;

	if (bzip2_handle->last_bz_result != BZ_OK) {
		if (bzip2_handle->last_bz_result == BZ_STREAM_END)
			return GNOME_VFS_OK;
		else
			return result_from_bz_result (bzip2_handle->last_bz_result);
	} else if (bzip2_handle->last_vfs_result != GNOME_VFS_OK) {
		return bzip2_handle->last_vfs_result;
	}

	bzstream->next_out = buffer;
	bzstream->avail_out = num_bytes;

	/* FIXME: Clean up */
	while (bzstream->avail_out != 0) {
		result = fill_buffer (bzip2_handle, num_bytes);
		RETURN_IF_FAIL (result);

		bz_result = bzDecompress (&bzip2_handle->bzstream);

		if (bzip2_handle->last_bz_result != BZ_OK
		    && bzstream->avail_out == num_bytes) {
			return result_from_bz_result (bzip2_handle->last_bz_result);
		}

		*bytes_read = num_bytes - bzstream->avail_out;
	}

	return GNOME_VFS_OK;
}

/* Write. */

static GnomeVFSResult
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{
	Bzip2MethodHandle *bzip2_handle;
	GnomeVFSResult result;
	bz_stream *bzstream;
	gint bz_result;

	bzip2_handle = (Bzip2MethodHandle *) method_handle;
	bzstream = &bzip2_handle->bzstream;

	bzstream->next_in = (gpointer) buffer;
	bzstream->avail_in = num_bytes;

	result = GNOME_VFS_OK;

	while (bzstream->avail_in != 0 && result == GNOME_VFS_OK) {
		if (bzstream->avail_out == 0) {
			GnomeVFSFileSize written;

			bzstream->next_out = bzip2_handle->buffer;
			result = gnome_vfs_write (bzip2_handle->parent_handle,
						  bzip2_handle->buffer,
						  BZ_BUFSIZE, &written);
			if (result != GNOME_VFS_OK)
				break;

			bzstream->avail_out += written;
		}

		bz_result = bzCompress (bzstream, BZ_RUN);
		result = result_from_bz_result (bz_result);
	}

	*bytes_written = num_bytes - bzstream->avail_in;

	return result;
}

static gboolean
do_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);
	return gnome_vfs_uri_is_local (uri->parent);
}

GnomeVFSMethod *
vfs_module_init (void)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
	return;
}
