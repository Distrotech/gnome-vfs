/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* http-method.c - The HTTP method implementation for the GNOME Virtual File
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

   Author: Ettore Perazzoli <ettore@gnu.org>, with some help from the
   friendly GNU Wget sources.  */

/* TODO:
   - Handle redirection.
   - Handle persistent connections.  */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "http-method.h"


#if 1				/* FIXME */
#include <stdio.h>
#define DEBUG_HTTP(x)				\
	do {					\
		fputs ("HTTP: ", stdout);	\
		printf x;			\
		putchar ('\n');			\
	} while (0);
#endif


/* What do we qualify ourselves as?  */
#define USER_AGENT_STRING 	"gnome-vfs/" VERSION /* FIXME */

/* Standard HTTP port.  */
#define DEFAULT_HTTP_PORT 	80

/* Value for I/O block size returned by the `get_file_info()' op.  Sort of
   bogus, yes.  */
#define IO_BLOCK_SIZE		4096


/* Some status code validation macros.  */
#define HTTP_20X(x)        (((x) >= 200) && ((x) < 300))
#define HTTP_PARTIAL(x)    ((x) == HTTP_STATUS_PARTIAL_CONTENTS)
#define HTTP_REDIRECTED(x) (((x) == HTTP_STATUS_MOVED_PERMANENTLY)	\
			    || ((x) == HTTP_STATUS_MOVED_TEMPORARILY))

/* HTTP/1.1 status codes from RFC2068, provided for reference.  */
/* Successful 2xx.  */
#define HTTP_STATUS_OK			200
#define HTTP_STATUS_CREATED		201
#define HTTP_STATUS_ACCEPTED		202
#define HTTP_STATUS_NON_AUTHORITATIVE	203
#define HTTP_STATUS_NO_CONTENT		204
#define HTTP_STATUS_RESET_CONTENT	205
#define HTTP_STATUS_PARTIAL_CONTENTS	206

/* Redirection 3xx.  */
#define HTTP_STATUS_MULTIPLE_CHOICES	300
#define HTTP_STATUS_MOVED_PERMANENTLY	301
#define HTTP_STATUS_MOVED_TEMPORARILY	302
#define HTTP_STATUS_SEE_OTHER		303
#define HTTP_STATUS_NOT_MODIFIED	304
#define HTTP_STATUS_USE_PROXY		305

/* Client error 4xx.  */
#define HTTP_STATUS_BAD_REQUEST		400
#define HTTP_STATUS_UNAUTHORIZED	401
#define HTTP_STATUS_PAYMENT_REQUIRED	402
#define HTTP_STATUS_FORBIDDEN		403
#define HTTP_STATUS_NOT_FOUND		404
#define HTTP_STATUS_METHOD_NOT_ALLOWED	405
#define HTTP_STATUS_NOT_ACCEPTABLE	406
#define HTTP_STATUS_PROXY_AUTH_REQUIRED 407
#define HTTP_STATUS_REQUEST_TIMEOUT	408
#define HTTP_STATUS_CONFLICT		409
#define HTTP_STATUS_GONE		410
#define HTTP_STATUS_LENGTH_REQUIRED	411
#define HTTP_STATUS_PRECONDITION_FAILED	412
#define HTTP_STATUS_REQENTITY_TOO_LARGE 413
#define HTTP_STATUS_REQURI_TOO_LARGE	414
#define HTTP_STATUS_UNSUPPORTED_MEDIA	415

/* Server errors 5xx.  */
#define HTTP_STATUS_INTERNAL		500
#define HTTP_STATUS_NOT_IMPLEMENTED	501
#define HTTP_STATUS_BAD_GATEWAY		502
#define HTTP_STATUS_UNAVAILABLE		503
#define HTTP_STATUS_GATEWAY_TIMEOUT	504
#define HTTP_STATUS_UNSUPPORTED_VERSION 505


struct _HttpFileHandle {
	GnomeVFSInetConnection *connection;
	GnomeVFSIOBuf *iobuf;
	gchar *uri_string;
	gchar *mime_type;
	gchar *location;

	time_t access_time;
	time_t last_modified;

	/* Expected size, as reported by the HTTP headers.  */
	GnomeVFSFileSize size;
	/* Whether the size is known from the headers.  */
	gboolean size_is_known : 1;

	/* Bytes read so far.  */
	GnomeVFSFileSize bytes_read;
};
typedef struct _HttpFileHandle HttpFileHandle;


static HttpFileHandle *
http_file_handle_new (GnomeVFSInetConnection *connection,
		      GnomeVFSIOBuf *iobuf,
		      const GnomeVFSURI *uri)
{
	HttpFileHandle *new;

	new = g_new (HttpFileHandle, 1);

	new->connection = connection;
	new->iobuf = iobuf;
	new->uri_string = gnome_vfs_uri_to_string (uri, 0); /* FIXME */

	new->location = NULL;
	new->mime_type = NULL;
	new->last_modified = 0;
	new->size = 0;
	new->size_is_known = FALSE;
	new->bytes_read = 0;

	return new;
}

static void
http_file_handle_destroy (HttpFileHandle *handle)
{
	g_free (handle->uri_string);
	g_free (handle->mime_type);
	g_free (handle);
}


/* The following comes from GNU Wget with minor changes by myself.
   Copyright (C) 1995, 1996, 1997, 1998 Free Software Foundation, Inc.  */
/* Parse the HTTP status line, which is of format:

   HTTP-Version SP Status-Code SP Reason-Phrase

   The function returns the status-code, or -1 if the status line is
   malformed.  The pointer to reason-phrase is returned in RP.  */
static gboolean
parse_status (const gchar *line,
	      guint *status_return)
{
	/* (the variables must not be named `major' and `minor', because
	   that breaks compilation with SunOS4 cc.)  */
	guint mjr, mnr;
	guint statcode;
	const gchar *p;

	/* The standard format of HTTP-Version is: `HTTP/X.Y', where X is
	   major version, and Y is minor version.  */
	if (strncmp (line, "HTTP/", 5) != 0)
		return FALSE;
	line += 5;

	/* Calculate major HTTP version.  */
	p = line;
	for (mjr = 0; isdigit (*line); line++)
		mjr = 10 * mjr + (*line - '0');
	if (*line != '.' || p == line)
		return FALSE;
	++line;

	/* Calculate minor HTTP version.  */
	p = line;
	for (mnr = 0; isdigit (*line); line++)
		mnr = 10 * mnr + (*line - '0');
	if (*line != ' ' || p == line)
		return -1;
	/* Wget will accept only 1.0 and higher HTTP-versions.  The value of
	   minor version can be safely ignored.  */
	if (mjr < 1)
		return FALSE;
	++line;

	/* Calculate status code.  */
	if (!(isdigit (*line) && isdigit (line[1]) && isdigit (line[2])))
		return -1;
	statcode = 100 * (*line - '0') + 10 * (line[1] - '0') + (line[2] - '0');

	*status_return = statcode;
	return TRUE;
}

static GnomeVFSResult
http_status_to_vfs_result (guint status)
{
	if (HTTP_20X (status))
		return GNOME_VFS_OK;

	/* FIXME TODO */
	switch (status) {
	default:
		return GNOME_VFS_ERROR_GENERIC;
	}
}


/* Header parsing routines.  */

static gboolean
header_value_to_number (const gchar *header_value,
			gulong *number)
{
	const gchar *p;
	gulong result;

	p = header_value;

	for (result = 0; isdigit (*p); p++)
		result = 10 * result + (*p - '0');
	if (*p)
		return FALSE;

	*number = result;

	return TRUE;
}

static gboolean
set_content_length (HttpFileHandle *handle,
		    const gchar *value)
{
	gboolean result;
	gulong size;

	result = header_value_to_number (value, &size);
	if (! result)
		return FALSE;

	DEBUG_HTTP (("Expected size is %lu.", size));
	handle->size = size;
	handle->size_is_known = TRUE;
	return TRUE;
}

static gboolean
set_content_type (HttpFileHandle *handle,
		  const gchar *value)
{
	g_free (handle->mime_type);
	handle->mime_type = g_strdup (value);
	return TRUE;
}

static gboolean
set_location (HttpFileHandle *handle,
	      const gchar *value)
{
	g_free (handle->location);
	handle->location = g_strdup (value);
	return TRUE;
}

static gboolean
set_last_modified (HttpFileHandle *handle,
		   const gchar *value)
{
	time_t time;

	if (! gnome_vfs_atotm (value, &time))
		return FALSE;

	handle->last_modified = time;
	return TRUE;
}

static gboolean
set_access_time (HttpFileHandle *handle,
		 const gchar *value)
{
	time_t time;

	if (! gnome_vfs_atotm (value, &time))
		return FALSE;

	handle->access_time = time;
	return TRUE;
}

struct _Header {
	const gchar *name;
	gboolean (* set_func) (HttpFileHandle *handle, const gchar *value);
};
typedef struct _Header Header;

static Header headers[] = {
	{ "Content-Length", set_content_length },
	{ "Content-Type", set_content_type },
	{ "Location", set_location },
	{ "Last-Modified", set_last_modified },
	{ "Date", set_access_time },
	{ NULL, NULL }
};

static const gchar *
check_header (const gchar *header,
	      const gchar *name)
{
	const gchar *p, *q;

	for (p = header, q = name; *p != '\0' && *q != '\0'; p++, q++) {
		if (tolower (*p) != tolower (*q))
			break;
	}

	if (*q != '\0' || *p != ':')
		return NULL;

	p++;			/* Skip ':'.  */
	while (*p == ' ' || *p == '\t')
		p++;

	DEBUG_HTTP (("Valid header `%s' found; value `%s'\n", header, p));
	return p;
}

static gboolean
parse_header (HttpFileHandle *handle,
	      const gchar *header)
{
	guint i;

	for (i = 0; headers[i].name != NULL; i++) {
		const gchar *value;

		value = check_header (header, headers[i].name);
		if (value != NULL)
			return (* headers[i].set_func) (handle, value);
	}

	DEBUG_HTTP (("Unknown header `%s'", header));

	/* Simply ignore headers we don't know.  */
	return TRUE;
}


/* Header/status reading.  */

static GnomeVFSResult
get_header (GnomeVFSIOBuf *iobuf,
	    GString *s)
{
	GnomeVFSResult result;
	guint count;

	g_string_truncate (s, 0);

	count = 0;
	while (1) {
		gchar c;

		result = gnome_vfs_iobuf_read (iobuf, &c, 1, NULL);
		if (result != GNOME_VFS_OK)
			return result;

		if (c == '\n') {
			/* Handle continuation lines.  */
			if (count != 0 && (count != 1 || s->str[0] != '\r')) {
				gchar next;

				result = gnome_vfs_iobuf_peekc (iobuf, &next);
				if (result != GNOME_VFS_OK)
					return result;

				if (next == '\t' || next == ' ') {
					if (count > 0
					    && s->str[count - 1] == '\r')
						s->str[count - 1] = '\0';
					continue;
				}
			}

			if (count > 0 && s->str[count - 1] == '\r')
				s->str[count - 1] = '\0';
			break;
		} else {
			g_string_append_c (s, c);
		}

		count++;
	}

	return GNOME_VFS_OK;
}

/* FIXME rename */
static GnomeVFSResult
create_handle (HttpFileHandle **handle_return,
	       GnomeVFSURI *uri,
	       GnomeVFSInetConnection *connection,
	       GnomeVFSIOBuf *iobuf,
	       GnomeVFSCancellation *cancellation)
{
	GString *header_string;
	GnomeVFSResult result;
	guint server_status;

	*handle_return = http_file_handle_new (connection, iobuf, uri);
	header_string = g_string_new (NULL);

	/* This is the status report string, which is the first header.  */
	result = get_header (iobuf, header_string);
	if (result != GNOME_VFS_OK)
		goto error;

	if (! parse_status (header_string->str, &server_status)) {
		result = GNOME_VFS_ERROR_NOTFOUND; /* FIXME */
		goto error;
	}

	if (! HTTP_20X (server_status)) {
		result = http_status_to_vfs_result (server_status);
		goto error;
	}

	/* Header fetching loop.  */
	while (1) {
		result = get_header (iobuf, header_string);
		if (result != GNOME_VFS_OK)
			break;

		/* Empty header ends header section.  */
		if (header_string->str[0] == '\0')
			break;

		if (! parse_header (*handle_return, header_string->str)) {
			g_warning (_("Invalid header `%s'"), header_string->str);
			result = GNOME_VFS_ERROR_NOTFOUND; /* FIXME */
			break;
		}
	}

	if (result != GNOME_VFS_OK)
		goto error;

	g_string_free (header_string, TRUE);
	return GNOME_VFS_OK;

 error:
	http_file_handle_destroy (*handle_return);
	*handle_return = NULL;
	g_string_free (header_string, TRUE);
	return result;
}

static GnomeVFSResult
make_request (HttpFileHandle **handle_return,
	      GnomeVFSURI *uri,
	      const gchar *method,
	      GnomeVFSCancellation *cancellation)
{
	GnomeVFSInetConnection *connection;
	GnomeVFSIOBuf *iobuf;
	GnomeVFSResult result;
	GnomeVFSFileSize bytes_written;
	GnomeVFSToplevelURI *toplevel_uri;
	GString *request;
	gchar *uri_string;
	guint len;
	guint host_port;

	toplevel_uri = (GnomeVFSToplevelURI *) uri;

	if (toplevel_uri->host_port == 0)
		host_port = DEFAULT_HTTP_PORT;
	else
		host_port = toplevel_uri->host_port;

	result = gnome_vfs_inet_connection_create (&connection,
						   toplevel_uri->host_name,
						   host_port,
						   cancellation);

	if (result != GNOME_VFS_OK)
		return result;

	iobuf = gnome_vfs_inet_connection_get_iobuf (connection);

	uri_string = gnome_vfs_uri_to_string (uri, 0); /* FIXME */

	/* Request line.  */
	request = g_string_new (method);
	g_string_append (request, " ");
	g_string_append (request, uri_string);
	g_string_append (request, " HTTP/1.1\r\n");

	/* `Host:' header.  */
	g_string_sprintfa (request, "Host: %s:%d\r\n",
			   toplevel_uri->host_name, toplevel_uri->host_port);

	/* `Accept:' header.  */
	g_string_append (request, "Accept: */*\r\n");

	/* `User-Agent:' header.  */
	g_string_sprintfa (request, "User-Agent: %s\r\n", USER_AGENT_STRING);

	/* Empty line ends header section.  */
	g_string_append (request, "\r\n");

	/* Send the request.  */
	len = strlen (request->str);
	result = gnome_vfs_iobuf_write (iobuf, request->str, len,
					&bytes_written);
	g_string_free (request, TRUE);

	if (result == GNOME_VFS_OK)
		result = gnome_vfs_iobuf_flush (iobuf);
	if (result != GNOME_VFS_OK)
		goto error;

	/* Read the headers and create our internal HTTP file handle.  */
	result = create_handle (handle_return, uri, connection, iobuf,
				cancellation);

	if (result != GNOME_VFS_OK)
		goto error;

	return result;

 error:
	gnome_vfs_iobuf_destroy (iobuf);
	gnome_vfs_inet_connection_destroy (connection, NULL);
	g_free (uri_string);
	return result;
}

static void
http_handle_close (HttpFileHandle *handle,
		   GnomeVFSCancellation *cancellation)
{
	gnome_vfs_iobuf_flush (handle->iobuf);
	gnome_vfs_iobuf_destroy (handle->iobuf);
	gnome_vfs_inet_connection_destroy (handle->connection, cancellation);

	http_file_handle_destroy (handle);
}


static GnomeVFSResult
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSCancellation *cancellation)
{
	HttpFileHandle *handle;
	GnomeVFSResult result;

	g_return_val_if_fail (uri->parent == NULL, GNOME_VFS_ERROR_INVALIDURI);
	g_return_val_if_fail (mode == GNOME_VFS_OPEN_READ,
			      GNOME_VFS_ERROR_INVALIDOPENMODE);

	result = make_request (&handle, uri, "GET", cancellation);

	if (result == GNOME_VFS_OK)
		*method_handle = (GnomeVFSMethodHandle *) handle;

	return result;
}

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle,
	  GnomeVFSCancellation *cancellation)
{
	HttpFileHandle *handle;

	handle = (HttpFileHandle *) method_handle;
	http_handle_close (handle, cancellation);

	return GNOME_VFS_OK;
}
	
static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSCancellation *cancellation)
{
	HttpFileHandle *handle;
	GnomeVFSResult result;

	handle = (HttpFileHandle *) method_handle;

	if (handle->size_is_known) {
		GnomeVFSFileSize max_bytes;

		max_bytes = handle->size - handle->bytes_read;
		num_bytes = MIN (max_bytes, num_bytes);
	}

	result = gnome_vfs_iobuf_read (handle->iobuf, buffer, num_bytes,
				       bytes_read);

	handle->bytes_read += *bytes_read;

	return result;
}


/* File info handling.  */

static GnomeVFSResult
get_file_info_from_http_handle (HttpFileHandle *handle,
				GnomeVFSFileInfo *file_info,
				GnomeVFSFileInfoOptions options)
{
	file_info->name = g_strdup (g_basename (handle->uri_string));
	if (file_info->name == NULL)
		file_info->name = g_strdup ("");
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->permissions = 0444;
	file_info->is_local = FALSE;
	file_info->link_count = 0;
	file_info->uid = getuid ();
	file_info->gid = getgid ();
	file_info->size = handle->size;
	file_info->block_count = (file_info->size / 512) + 1;
	file_info->io_block_size = IO_BLOCK_SIZE;
	file_info->atime = handle->access_time;
	file_info->mtime = handle->last_modified;
	file_info->ctime = file_info->mtime;
	file_info->symlink_name = NULL;
	file_info->mime_type = g_strdup (handle->mime_type);
	file_info->metadata_list = NULL;
	file_info->is_local = FALSE;
	file_info->is_suid = FALSE;
	file_info->is_sgid = FALSE;
	file_info->has_sticky_bit = FALSE;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSCancellation *cancellation)
{
	HttpFileHandle *handle;
	GnomeVFSResult result;

	result = make_request (&handle, uri, "HEAD", cancellation);
	if (result != GNOME_VFS_OK)
		return result;

	result = get_file_info_from_http_handle (handle, file_info, options);
	http_handle_close (handle, cancellation);

	return result;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSCancellation *cancellation)
{
	HttpFileHandle *handle;

	handle = (HttpFileHandle *) method_handle;
	return get_file_info_from_http_handle (handle, file_info, options);
}

static gboolean
do_is_local (const GnomeVFSURI *uri)
{
	return FALSE;
}


static GnomeVFSMethod method = {
	do_open,
	NULL,
	do_close,
	do_read,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	do_get_file_info,
	do_get_file_info_from_handle,
	do_is_local,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

GnomeVFSMethod *
vfs_module_init (void)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
