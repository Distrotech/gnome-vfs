/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * gnome-vfs-mime-sniff-buffer.c
 * Utility for implementing gnome_vfs_mime_type_from_magic, and other
 * mime-type sniffing calls.
 *
 * Copyright (C) 2000 Eazel, Inc.
 * All rights reserved.
 *
 * This file is part of the Gnome Library.
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
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include "gnome-vfs-mime-sniff-buffer-private.h"
#include "gnome-vfs-ops.h"


static GnomeVFSResult
handle_seek_glue (gpointer context, GnomeVFSSeekPosition whence, 
	GnomeVFSFileOffset offset)
{
	GnomeVFSHandle *handle = (GnomeVFSHandle *)context;
	return gnome_vfs_seek (handle, whence, offset);
}

static GnomeVFSResult
handle_read_glue (gpointer context, gpointer buffer, 
	GnomeVFSFileSize bytes, GnomeVFSFileSize *bytes_read)
{
	GnomeVFSHandle *handle = (GnomeVFSHandle *)context;
	return gnome_vfs_read (handle, buffer, bytes, bytes_read);
}

GnomeVFSMimeSniffBuffer *
gnome_vfs_mime_sniff_buffer_new_from_handle (GnomeVFSHandle *file)
{
	GnomeVFSMimeSniffBuffer *result;

	result = g_new0 (GnomeVFSMimeSniffBuffer, 1);
	result->owning = TRUE;
	result->context = file;
	result->seek = handle_seek_glue;
	result->read = handle_read_glue;

	return result;
}

GnomeVFSMimeSniffBuffer	*
gnome_vfs_mime_sniff_buffer_new_generic (GnomeVFSSniffBufferSeekCall seek_callback, 
					 GnomeVFSSniffBufferReadCall read_callback,
					 gpointer context)
{
	GnomeVFSMimeSniffBuffer	* result;
	
	result = g_new0 (GnomeVFSMimeSniffBuffer, 1);

	result->owning = TRUE;
	result->seek = seek_callback;
	result->read = read_callback;
	result->context = context;

	return result;
}

GnomeVFSMimeSniffBuffer * 
gnome_vfs_mime_sniff_buffer_new_from_memory (const guchar *buffer, 
					     ssize_t buffer_length)
{
	GnomeVFSMimeSniffBuffer *result;

	result = g_new0 (GnomeVFSMimeSniffBuffer, 1);
	result->owning = TRUE;
	result->buffer = g_malloc (buffer_length);
	result->buffer_length = buffer_length;
	memcpy (result->buffer, buffer, buffer_length);

	return result;
}

GnomeVFSMimeSniffBuffer	*
gnome_vfs_mime_sniff_buffer_new_from_existing_data (const guchar *buffer, 
					 	    ssize_t buffer_length)
{
	GnomeVFSMimeSniffBuffer *result;

	result = g_new0 (GnomeVFSMimeSniffBuffer, 1);
	result->owning = FALSE;
	result->buffer = (guchar *)buffer;
	result->buffer_length = buffer_length;

	return result;
}

void
gnome_vfs_mime_sniff_buffer_free (GnomeVFSMimeSniffBuffer *buffer)
{
	if (buffer->owning)
		g_free (buffer->buffer);
	g_free (buffer);
}

enum {
	GNOME_VFS_SNIFF_BUFFER_INITIAL_CHUNK = 256,
	GNOME_VFS_SNIFF_BUFFER_MIN_CHUNK = 128
};

GnomeVFSResult
gnome_vfs_mime_sniff_buffer_get (GnomeVFSMimeSniffBuffer *buffer,
				 ssize_t size)
{
	GnomeVFSResult result;
	GnomeVFSFileSize bytes_read;

	if (buffer->buffer_length >= size) {
		/* we already have enough data read */
		return GNOME_VFS_OK;
	}

	if (!buffer->seek) {
		return GNOME_VFS_ERROR_EOF;
	}

	if (buffer->read_whole_file) {
		/* If we've read the whole file, don't read any more */
		return GNOME_VFS_OK;
	}

	if (size < GNOME_VFS_SNIFF_BUFFER_MIN_CHUNK) {
		/* don't bother to read less than this */
		size = GNOME_VFS_SNIFF_BUFFER_MIN_CHUNK;
	}

	if (size - buffer->buffer_length < GNOME_VFS_SNIFF_BUFFER_MIN_CHUNK) {
		/* don't bother to add less than this */
		size = buffer->buffer_length + GNOME_VFS_SNIFF_BUFFER_MIN_CHUNK;
	}

	/* make room in buffer for new data */
	buffer->buffer = g_realloc (buffer->buffer, size);

	/* seek to the spot we last took off */
	result = (* buffer->seek) (buffer->context, 
				   GNOME_VFS_SEEK_START,
				   buffer->buffer_length);
	
	if (result == GNOME_VFS_ERROR_EOF) {
		buffer->read_whole_file = TRUE;
		return result;
	}
	if (result != GNOME_VFS_OK) {
		return result;
	}

	/* read in more data */
	result = (* buffer->read) (buffer->context, 
				   buffer->buffer + buffer->buffer_length,
				   size - buffer->buffer_length,
				   &bytes_read);
	buffer->buffer_length += bytes_read;

	/* check to be sure we got enough data */
	if (size > buffer->buffer_length) {
	        buffer->read_whole_file = TRUE;
		result = GNOME_VFS_ERROR_EOF;
	}

	return result;
}
