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

   Authors: 
		 Ettore Perazzoli <ettore@gnu.org> (core HTTP)
		 Ian McKellar <yakk@yakk.net> (WebDAV/PUT)
		 The friendly GNU Wget sources
	*/

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

#include <stdlib.h> /* for atoi */

#include <gnome-xml/parser.h>
#include <gnome-xml/tree.h>

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
	GnomeVFSURI *uri;
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

	/* Bytes to be written... */
	GByteArray *to_be_written;

	/* Files from a directory listing */
	GList *files;
};
typedef struct _HttpFileHandle HttpFileHandle;


static HttpFileHandle *
http_file_handle_new (GnomeVFSInetConnection *connection,
		      GnomeVFSIOBuf *iobuf,
		      GnomeVFSURI *uri)
{
	HttpFileHandle *new;

	new = g_new (HttpFileHandle, 1);

	new->connection = connection;
	new->iobuf = iobuf;
	new->uri_string = gnome_vfs_uri_to_string (uri, 0); /* FIXME */
	new->uri = uri;

	new->location = NULL;
	new->mime_type = NULL;
	new->last_modified = 0;
	new->size = 0;
	new->size_is_known = FALSE;
	new->bytes_read = 0;
	new->to_be_written = NULL;
	new->files = NULL;

	return new;
}

static void
http_file_handle_destroy (HttpFileHandle *handle)
{
	g_free (handle->uri_string);
	g_free (handle->mime_type);
	if(handle->to_be_written)
		g_byte_array_free(handle->to_be_written, TRUE);
	/* this should work... */
	g_list_foreach(handle->files, (GFunc)gnome_vfs_file_info_unref, NULL);
	g_list_free(handle->files);
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
	case 401:
	case 407:
		return GNOME_VFS_ERROR_ACCESSDENIED;
	case 404:
		return GNOME_VFS_ERROR_NOTFOUND;
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
	       GnomeVFSContext *context)
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

	if (! HTTP_20X (server_status) && !HTTP_REDIRECTED(server_status)) {
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

	if ((*handle_return)->size_is_known) {
		gchar* msg;
		gchar* sz;

		sz = gnome_vfs_file_size_to_string ((*handle_return)->size);

		msg = g_strdup_printf(_("%s to retrieve"), sz);

		gnome_vfs_context_emit_message(context, msg);
		
		g_free(sz);
		g_free(msg);
	}
	
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
				GByteArray *data,
				gchar *extra_headers,
	      GnomeVFSContext *context)
{
	GnomeVFSInetConnection *connection;
	GnomeVFSIOBuf *iobuf;
	GnomeVFSResult result;
	GnomeVFSFileSize bytes_written;
	GnomeVFSToplevelURI *toplevel_uri;
	GString *request;
	gchar *uri_string;
	guint host_port;

	toplevel_uri = (GnomeVFSToplevelURI *) uri;

	if (toplevel_uri->host_port == 0)
		host_port = DEFAULT_HTTP_PORT;
	else
		host_port = toplevel_uri->host_port;

	result = gnome_vfs_inet_connection_create (&connection,
						   toplevel_uri->host_name,
						   host_port,
						   context ? gnome_vfs_context_get_cancellation(context) : NULL);

	if (result != GNOME_VFS_OK)
		return result;

	iobuf = gnome_vfs_inet_connection_get_iobuf (connection);

	uri_string = gnome_vfs_uri_to_string (uri,
					      GNOME_VFS_URI_HIDE_USER_NAME
					      |GNOME_VFS_URI_HIDE_PASSWORD
					      |GNOME_VFS_URI_HIDE_HOST_NAME
					      |GNOME_VFS_URI_HIDE_HOST_PORT
					      |GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);

	/* Request line.  */
	request = g_string_new (method);
	g_string_append (request, " ");
	g_string_append (request, uri_string);
	/* Our code doesn't handle the chunked transfer-encoding that mod_dav 
	 * uses for HTTP/1.1 requests. */
	g_string_append (request, " HTTP/1.0\r\n");

	/* `Host:' header.  */
	g_string_sprintfa (request, "Host: %s:%d\r\n",
			   toplevel_uri->host_name, toplevel_uri->host_port);

	/* `Accept:' header.  */
	g_string_append (request, "Accept: */*\r\n");

	/* `Content-Length' header.  */
	if(data)
		g_string_sprintfa (request, "Content-Length: %d\r\n", data->len);

	/* `User-Agent:' header.  */
	g_string_sprintfa (request, "User-Agent: %s\r\n", USER_AGENT_STRING);

	/* Extra headers. */
	if(extra_headers)
		g_string_append(request, extra_headers);

	/* Empty line ends header section.  */
	g_string_append (request, "\r\n");

	/* Send the request headers.  */
	result = gnome_vfs_iobuf_write (iobuf, request->str, request->len,
					&bytes_written);
	g_string_free (request, TRUE);

	if (result != GNOME_VFS_OK)
		goto error;

	if(data) {
#if 0
		g_print("sending data...\n");
#endif
		result = gnome_vfs_iobuf_write (iobuf, data->data, data->len,
						&bytes_written);
	}
	if (result == GNOME_VFS_OK)
		result = gnome_vfs_iobuf_flush (iobuf);
	if (result != GNOME_VFS_OK)
		goto error;

	/* Read the headers and create our internal HTTP file handle.  */
	result = create_handle (handle_return, uri, connection, iobuf,
				context);

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
		   GnomeVFSContext *context)
{
	gnome_vfs_iobuf_flush (handle->iobuf);
	gnome_vfs_iobuf_destroy (handle->iobuf);
	gnome_vfs_inet_connection_destroy (handle->connection,
					   context ? gnome_vfs_context_get_cancellation(context) : NULL);

	if (handle->uri_string) {
		gchar* msg;

		msg = g_strdup_printf(_("Closing connection to %s"),
				      handle->uri_string);

		gnome_vfs_context_emit_message(context, msg);

		g_free(msg);
	}
	
	http_file_handle_destroy (handle);
}


static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	HttpFileHandle *handle;
	GnomeVFSResult result = GNOME_VFS_OK;

	g_return_val_if_fail (uri->parent == NULL, GNOME_VFS_ERROR_INVALIDURI);
	g_return_val_if_fail (mode == GNOME_VFS_OPEN_READ || 
						mode == GNOME_VFS_OPEN_WRITE,
			      GNOME_VFS_ERROR_INVALIDOPENMODE);

	if(mode == GNOME_VFS_OPEN_READ) {
		result = make_request (&handle, uri, "GET", NULL, NULL,
			       	context);
	} else {
		handle = http_file_handle_new(NULL, NULL, uri); /* shrug */
	}
	if (result == GNOME_VFS_OK)
		*method_handle = (GnomeVFSMethodHandle *) handle;

	return result;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	HttpFileHandle *handle;
	GnomeVFSResult result = GNOME_VFS_OK;

	handle = (HttpFileHandle *) method_handle;

	/* if the handle was opened in write mode then:
	 * 1) there won't be a connection open, and
	 * 2) there will be data to_be_written...
	 */
	if(handle->to_be_written) { /* != NULL */
		GnomeVFSURI *uri = handle->uri;
		GByteArray *bytes = handle->to_be_written;

		http_file_handle_destroy(handle);

		result = make_request(&handle, uri, "PUT", bytes, NULL, context);

		if(result != GNOME_VFS_OK) {
			http_handle_close (handle, context);
			return result;
		}

		result = gnome_vfs_iobuf_write (handle->iobuf, bytes->data,
				bytes->len, NULL /* we don't care how big it was */);
		if(result != GNOME_VFS_OK) {
			return result;
		}

		g_byte_array_free(bytes, TRUE);
		g_free(uri);

	}
	http_handle_close (handle, context);

	return result;
}
	
static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	HttpFileHandle *handle;

	handle = (HttpFileHandle *) method_handle;

	if(handle->to_be_written == NULL) {
		handle->to_be_written = g_byte_array_new();
	}
	handle->to_be_written = g_byte_array_append(handle->to_be_written, buffer, num_bytes);
	*bytes_read = num_bytes;

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

	{
		gchar *msg;
		gchar *read_str = NULL;
		gchar *total_str = NULL;

		read_str = gnome_vfs_file_size_to_string(handle->bytes_read);

		if (handle->size_is_known) {
			total_str = gnome_vfs_file_size_to_string(handle->size);
		}

		if (total_str)
			msg = g_strdup_printf(_("%s of %s read"),
					      read_str, total_str);
		else
			msg = g_strdup_printf(_("%s read"), read_str);

		gnome_vfs_context_emit_message(context, msg);

		g_free(msg);
		g_free(read_str);
		if (total_str)
			g_free(total_str);
	}
	
	return result;
}


/* Directory handling - WebDAV servers only */

static void
process_propfind_propstat(xmlNodePtr node, GnomeVFSFileInfo *file_info)
{
	xmlNodePtr l;

	while(node != NULL) {
		if(strcmp((char *)node->name, "prop")) {
			/* node name != "prop" - prop is all we care about */
			node = node->next;
			continue;
		}
		/* properties of the file */
		l = node->childs;
		while(l != NULL) {
			if(!strcmp((char *)l->name, "getcontenttype")) {
				file_info->valid_fields |= 
					GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
				if(!file_info->mime_type)
					file_info->mime_type = 
						g_strdup(xmlNodeGetContent(l));

#if 0
				g_print("found content-type: %s\n", 
					xmlNodeGetContent(l));
#endif

			} else if(!strcmp((char *)l->name, "getcontentlength")){
				file_info->valid_fields |= 
					GNOME_VFS_FILE_INFO_FIELDS_SIZE;
				file_info->size = atoi(xmlNodeGetContent(l));

#if 0
				g_print("found content-length: %s\n", 
					xmlNodeGetContent(l));
#endif

			} else if(!strcmp((char *)l->name, "resourcetype")) {
				file_info->valid_fields |= 
					GNOME_VFS_FILE_INFO_FIELDS_TYPE;
				file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;

				if(l->childs && l->childs->name && 
					  !strcmp((char *)l->childs->name, 
					  "collection")) {
					file_info->type = 
						GNOME_VFS_FILE_TYPE_DIRECTORY;
					/* mjs wants me to set the mime-type to special/webdav-directory */
					if(file_info->mime_type) g_free(file_info->mime_type);
					file_info->mime_type = g_strdup("special/webdav-directory");
				}
			}
			/* TODO: all date related properties:
			 * creationdate
			 * getlastmodified
			 */
			l = l->next;
		}
		node = node->next;
	}
}

static GnomeVFSFileInfo *
process_propfind_response(xmlNodePtr n, gchar *uri_string)
{
	GnomeVFSFileInfo *file_info = gnome_vfs_file_info_new();

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	gnome_vfs_file_info_init(file_info);
	while(n != NULL) {
		if(!strcmp((char *)n->name, "href")) {
			gchar *nodecontent = xmlNodeGetContent(n);
#if 0
			g_print("  found href=\"%s\"\n", nodecontent);
#endif
			if(!strncmp(uri_string, nodecontent, 
					strlen(uri_string))) {
				/* our DAV server is prepending the 
				 * current path
				 */
				if(strcmp(uri_string, nodecontent))
					file_info->name = 
						g_strdup(nodecontent+
						strlen(uri_string));
			} else {
				file_info->name = 
					g_strdup(xmlNodeGetContent(n));
			}
		} else if(!strcmp((char *)n->name, "propstat")) {
			//g_print("  got <propstat>\n");
			process_propfind_propstat(n->childs, file_info);
		} else {
		/*
			g_print("  <%s>\n", n->name);
			m = n->childs;
			while(m != NULL) {
				g_print("    <%s>\n", m->name);
				m = m->next;
			}
		*/
		}
		n = n->next;
	}
	return file_info;
}



static GnomeVFSResult
make_propfind_request (HttpFileHandle **handle_return,
	GnomeVFSURI *uri,
	gint depth,
	GnomeVFSContext *context)
{
	HttpFileHandle *handle;
	GnomeVFSResult result = GNOME_VFS_OK;
	GnomeVFSFileSize bytes_read, num_bytes=(64*1024);
	gchar *buffer = g_malloc(num_bytes);
	xmlParserCtxtPtr parserContext;
	xmlDocPtr doc = NULL;
	xmlNodePtr cur = NULL;
	gchar *raw_uri = gnome_vfs_uri_to_string (uri,
                GNOME_VFS_URI_HIDE_USER_NAME
                |GNOME_VFS_URI_HIDE_PASSWORD
                |GNOME_VFS_URI_HIDE_HOST_NAME
                |GNOME_VFS_URI_HIDE_HOST_PORT
                |GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);
	gchar *uri_string = g_strdup_printf("%s/", raw_uri);
	gchar *extraheaders = g_strdup_printf("Depth: %d\r\n", depth);

	GByteArray *request = g_byte_array_new();
	gchar *request_str = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
		"<D:propfind xmlns:D=\"DAV:\"><D:allprop/></D:propfind>";


	g_free(raw_uri);

	request = g_byte_array_append(request, request_str, 
			strlen(request_str));

	parserContext = xmlCreatePushParserCtxt(NULL, NULL, "", 0, "PROPFIND");

	result = make_request (&handle, uri, "PROPFIND", request, 
			extraheaders, context);

	if (result == GNOME_VFS_OK) {
		*handle_return = handle;
	} else {
		xmlFreeParserCtxt(parserContext);
		g_free(buffer);
		g_free(extraheaders);
		return result;
	}

	do {
		result = do_read(NULL, (GnomeVFSMethodHandle *) *handle_return, 
			buffer, num_bytes, &bytes_read, context);
		if(result != GNOME_VFS_OK) {
			xmlFreeParserCtxt(parserContext);
			g_free(buffer);
			g_free(extraheaders);
			return result;
		}
		xmlParseChunk(parserContext, buffer, bytes_read, 0);
		buffer[bytes_read]=0;
	} while( bytes_read > 0 );
	xmlParseChunk(parserContext, "", 0, 1);

	doc = parserContext->myDoc;
	if(!doc)
		return GNOME_VFS_ERROR_CORRUPTEDDATA;

	cur = doc->root;

	if(strcmp((char *)cur->name, "multistatus")) {
#if 0
		g_print("Couldn't find <multistatus>.\n");
#endif
		return GNOME_VFS_ERROR_CORRUPTEDDATA;
	}

	cur = cur->childs;

	while(cur != NULL) {
		if(!strcmp((char *)cur->name, "response")) {
			GnomeVFSFileInfo *file_info =
				process_propfind_response(cur->childs, 
					uri_string);
			/* if the file has a filename or we're doing a PROPFIND on a single 
			 * resource... */
			if(file_info->name || depth==0) { 
				handle->files = g_list_append(handle->files, file_info);
			}
		} else {
#if 0
			g_print("expecting <response> got <%s>\n", cur->name);
#endif
		}
		cur = cur->next;
	}

	g_free(buffer);
	g_free(uri_string);
	g_free(extraheaders);

	xmlFreeParserCtxt(parserContext);

	return result;
}

static GnomeVFSResult
do_open_directory(GnomeVFSMethod *method,
	GnomeVFSMethodHandle **method_handle,
	GnomeVFSURI *uri,
	GnomeVFSFileInfoOptions options,
	const GList *meta_keys,
	const GnomeVFSDirectoryFilter *filter,
	GnomeVFSContext *context) 
{
	return make_propfind_request((HttpFileHandle **)method_handle, uri, 1, context);
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod *method,
	GnomeVFSMethodHandle *method_handle,
	GnomeVFSContext *context) 
{
	
	HttpFileHandle *handle;

	handle = (HttpFileHandle *) method_handle;

	http_handle_close(handle, context);

	return GNOME_VFS_OK;
}
       
static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
       GnomeVFSMethodHandle *method_handle,
       GnomeVFSFileInfo *file_info,
       GnomeVFSContext *context)
{
	HttpFileHandle *handle;

	handle = (HttpFileHandle *) method_handle;

	if(handle->files && g_list_length(handle->files)) {
		GnomeVFSFileInfo *original_info = g_list_nth_data(handle->files, 0);

		/* copy file info from our GnomeVFSFileInfo to the one that was
		 * passed to us.
		 */
		/*
		file_info->name = g_strdup(original_info->name);
		file_info->mime_type = g_strdup(original_info->mime_type);
		file_info->size = original_info->size;
		*/
		memcpy(file_info, original_info, sizeof(*file_info));

		/* discard our GnomeVFSFileInfo */
		handle->files = g_list_remove(handle->files, original_info);
		//gnome_vfs_file_info_unref(original_info);
		g_free(original_info);

		return GNOME_VFS_OK;
	} else {
		return GNOME_VFS_ERROR_EOF;
	}
}
 
/* File info handling.  */

static GnomeVFSResult
get_file_info_from_http_handle (HttpFileHandle *handle,
				GnomeVFSFileInfo *file_info,
				GnomeVFSFileInfoOptions options)
{
	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;
	file_info->name = g_strdup (g_basename (handle->uri_string));
	if (file_info->name == NULL)
		file_info->name = g_strdup ("");
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->permissions = 0444;
	
	file_info->atime = handle->access_time;
	file_info->mtime = handle->last_modified;
	file_info->mime_type = g_strdup (handle->mime_type);
	file_info->metadata_list = NULL;

	if (handle->size_is_known) {
		file_info->size = handle->size;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SIZE;		
	}

	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, FALSE);
	GNOME_VFS_FILE_INFO_SET_SUID (file_info, FALSE);
	GNOME_VFS_FILE_INFO_SET_SGID (file_info, FALSE);
	GNOME_VFS_FILE_INFO_SET_STICKY (file_info, FALSE);

	file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE | 
		GNOME_VFS_FILE_INFO_FIELDS_FLAGS |
		GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS |
		GNOME_VFS_FILE_INFO_FIELDS_SIZE |
		GNOME_VFS_FILE_INFO_FIELDS_ATIME |
		GNOME_VFS_FILE_INFO_FIELDS_MTIME |
		GNOME_VFS_FILE_INFO_FIELDS_SIZE |
		GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
	HttpFileHandle *handle;
	GnomeVFSResult result;

	result = make_propfind_request(&handle, uri, 0, context);
	if (result == GNOME_VFS_OK) {
		/* PROPFIND worked */
		memcpy(file_info, handle->files->data, sizeof(*file_info));
		g_free(handle->files->data);
		g_list_free(handle->files);
		handle->files = NULL;
		file_info->name = g_strdup (g_basename (handle->uri_string));
		if (file_info->name == NULL)
			file_info->name = g_strdup ("");
	} else {
#if 0
		g_print("PROPFIND didn't work...\n");
		g_print("result = %s\n", gnome_vfs_result_to_string(result) );
#endif
		result = make_request (&handle, uri, "HEAD", NULL, NULL, 
			       	context);
		if (result != GNOME_VFS_OK)
			return result;

		result = get_file_info_from_http_handle (handle, file_info, options);
	}
	http_handle_close (handle, context);

	return result;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSContext *context)
{
	HttpFileHandle *handle;

	return do_get_file_info(method, ((HttpFileHandle *)method_handle)->uri, 
			file_info, options, meta_keys, context);

	handle = (HttpFileHandle *) method_handle;
	return get_file_info_from_http_handle (handle, file_info, options);
}

static gboolean
do_is_local (GnomeVFSMethod *method,
	     const GnomeVFSURI *uri)
{
	return FALSE;
}


static GnomeVFSMethod method = {
	do_open,
	NULL,
	do_close,
	do_read,
	do_write,
	NULL,
	NULL,
	NULL,
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,
	do_get_file_info_from_handle,
	do_is_local,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL /* truncate */
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
