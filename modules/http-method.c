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
#include <gnome-xml/xmlmemory.h>
#include <sys/time.h>


#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <pthread.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"
#include "gnome-vfs-mime.h"

#include "http-method.h"


#if 0
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

static void
my_debug_printf(char *fmt, ...)
{
	va_list args;
	gchar * out;

	g_assert (fmt);

	va_start (args, fmt);

	out = g_strdup_vprintf (fmt, args);

	fprintf (stderr, "HTTP: [0x%08x] %s\n", (unsigned int) pthread_self(), out);

	g_free (out);
	va_end (args);
}

#define DEBUG_HTTP(x) my_debug_printf x
#else
static int nothing;
#define DEBUG_HTTP(x) nothing = 1;
#endif

#undef DAV_NO_CACHE
#ifndef DAV_NO_CACHE

/* Cache file info for 5 minutes */
#define US_CACHE_FILE_INFO (1000 * 1000 * 60 * 5)
/* Cache directory listings for 500 ms */
#define US_CACHE_DIRECTORY (1000 * 500)

/* Mutex for cache data structures */
/* The GLib mutex abstraction doesn't allow recursive mutexs */
static pthread_mutex_t cache_rlock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/* Hash maps char * URI ---> FileInfoCacheEntry */
GHashTable * gl_file_info_cache = NULL;
/* in-order list of cache entries  for expiration */
GList * gl_file_info_cache_list = NULL;
GList * gl_file_info_cache_list_last = NULL;

typedef gint64 utime_t;

typedef struct {
	gchar *			uri_string;
	GnomeVFSFileInfo *	file_info;
	utime_t			create_time;
	GList *			my_list_node;	/*node for me in gl_file_info_cache_list*/
	GList *			filenames;	/* List of char * basenames for files that are in this 
						 * collection/directory.  Empty for non-directories
						 */
	gboolean		has_filenames;	/* For directories, FALSE if the cache does not contain 
						 * the directory's children 
						 */
} FileInfoCacheEntry;

static FileInfoCacheEntry * 	cache_entry_new ();
static void 			cache_entry_free (FileInfoCacheEntry * entry);
static void			cache_trim ();
static GnomeVFSFileInfo *	cache_check (const gchar * uri_string);
static GnomeVFSFileInfo *	cache_check_uri (GnomeVFSURI *uri);
static GnomeVFSFileInfo *	cache_check_directory (const gchar * uri_string, GList **p_child_file_info_list);
static GnomeVFSFileInfo *	cache_check_directory_uri (GnomeVFSURI *uri, GList **p_child_file_info_list);
static FileInfoCacheEntry *	cache_add_no_strdup (gchar * uri_string, GnomeVFSFileInfo * file_info);
static FileInfoCacheEntry *	cache_add (const gchar * uri_string, GnomeVFSFileInfo * file_info);
static void			cache_add_uri_and_children (
					GnomeVFSURI *uri, 
					GnomeVFSFileInfo *file_info, 
					GList *file_info_list
				);
#if 0
static void			cache_add_uri (GnomeVFSURI *uri, GnomeVFSFileInfo *file_info);
#endif
static void			cache_invalidate (const gchar * uri_string);
static void			cache_invalidate_uri (GnomeVFSURI *uri);
static void			cache_invalidate_entry_and_children (const gchar * uri_string);
static void			cache_invalidate_uri_and_children (GnomeVFSURI *uri);
static void			cache_invalidate_uri_parent (GnomeVFSURI *uri);


static utime_t
get_utime()
{
    struct timeval tmp;
    gettimeofday (&tmp, NULL);
    return (utime_t)tmp.tv_usec + ((gint64)tmp.tv_sec) * 1000000LL;
}

static void
cache_init ()
{
	pthread_mutex_lock (&cache_rlock);
	gl_file_info_cache = g_hash_table_new (g_str_hash, g_str_equal);
	pthread_mutex_unlock (&cache_rlock);
}

static void
cache_shutdown ()
{
	GList *node, *node_next;

	pthread_mutex_lock (&cache_rlock);

	for (	node = g_list_first (gl_file_info_cache_list) ; 
		NULL != node ; 
		node = node_next
	) {
		node_next = g_list_next (node);
		cache_entry_free ((FileInfoCacheEntry*) node->data);
	}

	g_list_free (gl_file_info_cache_list);
	
	g_hash_table_destroy (gl_file_info_cache);

	pthread_mutex_unlock (&cache_rlock);
}

static FileInfoCacheEntry *
cache_entry_new ()
{
	FileInfoCacheEntry *ret;

	pthread_mutex_lock (&cache_rlock);

	ret = g_new0 (FileInfoCacheEntry, 1);
	ret->create_time = get_utime();
	
	gl_file_info_cache_list = g_list_prepend (gl_file_info_cache_list, ret);

	/* Note that since we've prepended, gl_file_info_cache_list points to us*/

	ret->my_list_node = gl_file_info_cache_list;

	if (NULL == gl_file_info_cache_list_last) {
		gl_file_info_cache_list_last = ret->my_list_node;
	}

	pthread_mutex_unlock (&cache_rlock);

	return ret;
}

/* Warning: as this function removes the cache entry from gl_file_info_cache_list, 
 * callee's must be careful when calling this during a list iteration
 */
static void
cache_entry_free (FileInfoCacheEntry * entry)
{
	if (entry) {
		GList *node;
		
		pthread_mutex_lock (&cache_rlock);

		g_hash_table_remove (gl_file_info_cache, entry->uri_string);
		g_free (entry->uri_string);	/* This is the same string as in the hash table */
		gnome_vfs_file_info_unref (entry->file_info);

		if (gl_file_info_cache_list_last == entry->my_list_node) {
			gl_file_info_cache_list_last = g_list_previous (entry->my_list_node);
		}
	
		gl_file_info_cache_list = g_list_remove_link (gl_file_info_cache_list, entry->my_list_node);
		g_list_free_1 (entry->my_list_node);

		for (node = entry->filenames ; node ; node = g_list_next(node)) {
			g_free (node->data);
		}

		g_list_free (entry->filenames); 
		
		g_free (entry);

		pthread_mutex_unlock (&cache_rlock);
	}
}

static void
cache_trim ()
{
	GList *node, *node_previous;
	utime_t utime_expire;

	pthread_mutex_lock (&cache_rlock);

	utime_expire = get_utime() - US_CACHE_FILE_INFO;

	for (	node = gl_file_info_cache_list_last ; 
		node && (utime_expire > ((FileInfoCacheEntry *)node->data)->create_time) ;
		node = node_previous
	) {
		node_previous = g_list_previous (node);

		DEBUG_HTTP (("Cache: Expire: '%s'",((FileInfoCacheEntry *)node->data)->uri_string));

		cache_entry_free ((FileInfoCacheEntry *)(node->data));
	}

	pthread_mutex_unlock (&cache_rlock);
}

/* Note: doesn't bother trimming entries, so the check can fast */
static GnomeVFSFileInfo *
cache_check (const gchar * uri_string)
{
	FileInfoCacheEntry *entry;
	utime_t utime_expire;
	GnomeVFSFileInfo *ret;

	pthread_mutex_lock (&cache_rlock);

	utime_expire = get_utime() - US_CACHE_FILE_INFO;

	entry = (FileInfoCacheEntry *)g_hash_table_lookup (gl_file_info_cache, uri_string);

	if (entry && (utime_expire > entry->create_time)) {
		entry = NULL;
	}

	if (entry) {
		gnome_vfs_file_info_ref (entry->file_info);

		DEBUG_HTTP (("Cache: Hit: '%s'",entry->uri_string));

		ret = entry->file_info;
	} else {
		ret = NULL;
	}
	pthread_mutex_unlock (&cache_rlock);
	return ret;
}

static gchar *
cache_uri_to_string  (GnomeVFSURI *uri)
{
	gchar *uri_string;
	size_t uri_length;

	uri_string = gnome_vfs_uri_to_string (uri,
				      GNOME_VFS_URI_HIDE_USER_NAME
				      | GNOME_VFS_URI_HIDE_PASSWORD
				      | GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);

	if (uri_string) {
		uri_length = strlen (uri_string);
		/* Trim off trailing '/'s */
		if ( '/' == uri_string[uri_length-1] ) {
			uri_string[uri_length-1] = 0;
		}
	}

	return uri_string;
}

static GnomeVFSFileInfo *
cache_check_uri (GnomeVFSURI *uri)
{
	gchar *uri_string;
	GnomeVFSFileInfo *ret;

	uri_string = cache_uri_to_string (uri);

	ret = cache_check (uri_string);
	g_free (uri_string);
	return ret;
}


/* Directory operations demand fresher cache entries */
static GnomeVFSFileInfo *
cache_check_directory (const gchar * uri_string, GList **p_child_file_info_list)
{
	FileInfoCacheEntry *entry;
	utime_t utime_expire;
	GnomeVFSFileInfo *ret;
	GList *child_file_info_list = NULL;
	gboolean cache_incomplete;

	pthread_mutex_lock (&cache_rlock);

	utime_expire = get_utime() - US_CACHE_DIRECTORY;

	entry = (FileInfoCacheEntry *)g_hash_table_lookup (gl_file_info_cache, uri_string);

	if (entry && (utime_expire > entry->create_time)) {
		entry = NULL;
	}

	if (entry && entry->has_filenames) {
		DEBUG_HTTP (("Cache: Hit: '%s'",entry->uri_string));

		gnome_vfs_file_info_ref (entry->file_info);
		ret = entry->file_info;
	} else {
		ret = NULL;
	}

	if (ret && NULL != p_child_file_info_list) {
		GList * filename_node;

		cache_incomplete = FALSE;
		
		for (filename_node = entry->filenames ;
			filename_node ; 
			filename_node = g_list_next (filename_node) 
		) {
			char *child_filename;
			FileInfoCacheEntry *child_entry;

			child_filename = g_strconcat (uri_string, "/", (gchar *)filename_node->data, NULL);

			child_entry = (FileInfoCacheEntry *)g_hash_table_lookup (gl_file_info_cache, child_filename);

			/* Other HTTP requests on children can cause them to expire before the parent directory */
			if (NULL == child_entry ) {
				cache_incomplete = TRUE;
				break;
			}

			gnome_vfs_file_info_ref (child_entry->file_info);
			child_file_info_list = g_list_prepend (child_file_info_list, child_entry->file_info);

			g_free (child_filename);
		}

		if (cache_incomplete) {
			DEBUG_HTTP (("Cache: Directory was incomplete: '%s'",entry->uri_string));

			gnome_vfs_file_info_unref (ret);
			ret = NULL;
			*p_child_file_info_list = NULL;
		} else {
			*p_child_file_info_list = child_file_info_list;
		}
	}

	pthread_mutex_unlock (&cache_rlock);

	return ret;
}

static GnomeVFSFileInfo *
cache_check_directory_uri (GnomeVFSURI * uri, GList **p_child_file_info_list)
{
	gchar *uri_string;
	GnomeVFSFileInfo *ret;

	uri_string = cache_uri_to_string (uri);

	ret = cache_check_directory (uri_string, p_child_file_info_list);
	g_free (uri_string);

	return ret;
}

/* Note that this neither strdups uri_string nor calls cache_trim() */
static FileInfoCacheEntry *
cache_add_no_strdup (gchar * uri_string, GnomeVFSFileInfo * file_info)
{
	FileInfoCacheEntry *entry_existing;
	FileInfoCacheEntry *entry;

	pthread_mutex_lock (&cache_rlock);

	entry_existing = (FileInfoCacheEntry *)g_hash_table_lookup (gl_file_info_cache, uri_string);

	DEBUG_HTTP (("Cache: Add: '%s'", uri_string));

	if (entry_existing) {
		cache_entry_free (entry_existing);
		entry_existing = NULL;
	}

	entry = cache_entry_new();

	entry->uri_string =  uri_string; 
	entry->file_info = file_info;
	gnome_vfs_file_info_ref (file_info);

	g_hash_table_insert (gl_file_info_cache, entry->uri_string, entry);

	pthread_mutex_unlock (&cache_rlock);

	return entry;
}

static FileInfoCacheEntry *
cache_add (const gchar * uri_string, GnomeVFSFileInfo * file_info)
{
	cache_trim ();
	return cache_add_no_strdup (g_strdup(uri_string), file_info);
}

static void
cache_add_uri_and_children (GnomeVFSURI *uri, GnomeVFSFileInfo *file_info, GList *file_info_list)
{
	gchar *uri_string;
	gchar *child_string;
	GList *node;
	FileInfoCacheEntry *parent_entry;

	cache_trim();

	pthread_mutex_lock (&cache_rlock);

	uri_string = cache_uri_to_string (uri);

	if (uri_string) {
		/* Note--can't use no_strdup because we use uri_string below */ 
		parent_entry = cache_add (uri_string, file_info);

		parent_entry->filenames = NULL;

		for (node = file_info_list ; NULL != node ; node = g_list_next (node)) {
			GnomeVFSFileInfo *child_info;

			child_info = (GnomeVFSFileInfo *) node->data;
			parent_entry->filenames = g_list_prepend (
							parent_entry->filenames, 
							g_strdup (child_info->name)); 

			child_string = g_strconcat (uri_string, "/", child_info->name, NULL);
			cache_add_no_strdup (child_string, child_info);
		}
		/* I'm not sure that order matters... */
		parent_entry->filenames = g_list_reverse (parent_entry->filenames);
		parent_entry->has_filenames = TRUE;
	}

	pthread_mutex_unlock (&cache_rlock);

	g_free (uri_string);
}

#if 0
static void
cache_add_uri (GnomeVFSURI *uri, GnomeVFSFileInfo *file_info)
{
	cache_trim ();

	cache_add_no_strdup (cache_uri_to_string (uri), file_info);
}
#endif


static void
cache_invalidate (const gchar * uri_string)
{
	FileInfoCacheEntry *entry;

	pthread_mutex_lock (&cache_rlock);

	entry = (FileInfoCacheEntry *)g_hash_table_lookup (gl_file_info_cache, uri_string);

	if (entry) {
		DEBUG_HTTP (("Cache: Invalidate: '%s'", entry->uri_string));

		cache_entry_free (entry);
	}

	pthread_mutex_unlock (&cache_rlock);
}

static void
cache_invalidate_uri (GnomeVFSURI *uri)
{
	gchar *uri_string;

	uri_string = cache_uri_to_string (uri);

	if (uri_string) {
		cache_invalidate (uri_string);
	}

	g_free (uri_string);
}


/* Invalidates entry and everything cached immediately beneath it */
static void
cache_invalidate_entry_and_children (const gchar * uri_string)
{
	FileInfoCacheEntry *entry;

	pthread_mutex_lock (&cache_rlock);

	entry = (FileInfoCacheEntry *)g_hash_table_lookup (gl_file_info_cache, uri_string);

	if (entry) {
		GList *node;
		
		DEBUG_HTTP (("Cache: Invalidate Recursive: '%s'", entry->uri_string));

		for (node = entry->filenames ; node ; node = g_list_next (node) ) {
			char *child_filename;
			child_filename = g_strconcat (uri_string, "/", (gchar *)node->data, NULL);
			cache_invalidate (child_filename);
			g_free (child_filename);
		}
		
		cache_entry_free (entry);
	}

	pthread_mutex_unlock (&cache_rlock);
}

/* Invalidates entry and everything cached immediately beneath it */
static void
cache_invalidate_uri_and_children (GnomeVFSURI *uri)
{
	gchar * uri_string;

	uri_string = cache_uri_to_string (uri);

	if (uri_string) {
		cache_invalidate_entry_and_children (uri_string);
	}

	g_free (uri_string);
}

/* Invalidate all of this uri's children and all of its parent's children */
static void
cache_invalidate_uri_parent (GnomeVFSURI *uri)
{
	gchar * uri_string;
	gchar * last_slash;

	uri_string = cache_uri_to_string (uri);

	if (uri_string) {
		cache_invalidate_entry_and_children (uri_string);

		last_slash = strrchr (uri_string, (unsigned char)'/');
		if (last_slash) {
			*last_slash = 0;
			cache_invalidate_entry_and_children (uri_string);
		}
	}

	g_free (uri_string);
}


#endif /* DAV_NO_CACHE */


/* What do we qualify ourselves as?  */
/* FIXME bugzilla.eazel.com 1160: "gnome-vfs/1.0.0" may not be good. */
#define USER_AGENT_STRING 	"gnome-vfs/" VERSION

/* Custom User-Agent environment variable */
#define CUSTOM_USER_AGENT_VARIABLE "GNOME_VFS_HTTP_USER_AGENT"

/* Standard HTTP port.  */
#define DEFAULT_HTTP_PORT 	80

/* Standard HTTP proxy port */
#define DEFAULT_HTTP_PROXY_PORT 8080

/* GConf paths and keys */
#define PATH_GCONF_GNOME_VFS "/system/gnome-vfs"
#define ITEM_GCONF_HTTP_PROXY "http-proxy"
#define KEY_GCONF_HTTP_PROXY (PATH_GCONF_GNOME_VFS "/" ITEM_GCONF_HTTP_PROXY)


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

/*
 * Static Variables
 */

/* Global variables used by the HTTP proxy config */ 
static GConfClient * gl_client = NULL;
static GMutex *gl_mutex = NULL;
static gchar *gl_http_proxy = NULL;


struct _HttpFileHandle {
	GnomeVFSInetConnection *connection;
	GnomeVFSIOBuf *iobuf;
	gchar *uri_string;
	GnomeVFSURI *uri;

	/* File info for this file */
	GnomeVFSFileInfo *file_info;

	/* Bytes read so far.  */
	GnomeVFSFileSize bytes_read;

	/* Bytes to be written... */
	GByteArray *to_be_written;

	/* List of GnomeVFSFileInfo from a directory listing */
	GList *files;

	/* The last HTTP status code returned */
	guint server_status;
};
typedef struct _HttpFileHandle HttpFileHandle;

static GnomeVFSFileInfo *
defaults_file_info_new ()
{
	GnomeVFSFileInfo *ret;

	/* Fill up the file info structure with default values */
	/* Default to REGULAR unless we find out later via a PROPFIND that it's a collection */

	ret = gnome_vfs_file_info_new();

	ret->type = GNOME_VFS_FILE_TYPE_REGULAR;
	ret->flags = GNOME_VFS_FILE_FLAGS_NONE;

	ret->valid_fields |= 
		GNOME_VFS_FILE_INFO_FIELDS_TYPE
			| GNOME_VFS_FILE_INFO_FIELDS_FLAGS;


	return ret;
}


static HttpFileHandle *
http_file_handle_new (GnomeVFSInetConnection *connection,
		      GnomeVFSIOBuf *iobuf,
		      GnomeVFSURI *uri)
{
	HttpFileHandle *result;

	result = g_new (HttpFileHandle, 1);

	result->connection = connection;
	result->iobuf = iobuf;
	result->uri_string = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE );
	result->uri = uri;
	gnome_vfs_uri_ref(result->uri);

	result->file_info = defaults_file_info_new();
	result->file_info->name = gnome_vfs_uri_extract_short_name (uri);
	result->bytes_read = 0;
	result->to_be_written = NULL;
	result->files = NULL;
	result->server_status = 0;

	return result;
}

static void
http_file_handle_destroy (HttpFileHandle *handle)
{
	if (handle) {
		gnome_vfs_uri_unref(handle->uri);
		gnome_vfs_file_info_unref (handle->file_info);
		g_free (handle->uri_string);
		if (handle->to_be_written) {
			g_byte_array_free(handle->to_be_written, TRUE);
		}
		g_list_foreach(handle->files, (GFunc)gnome_vfs_file_info_unref, NULL);
		g_list_free(handle->files);
		g_free (handle);
	}
}


/* The following comes from GNU Wget with minor changes by myself.
   Copyright (C) 1995, 1996, 1997, 1998 Free Software Foundation, Inc.  */
/* Parse the HTTP status line, which is of format:

   HTTP-Version SP Status-Code SP Reason-Phrase

   The function returns the status-code, or -1 if the status line is
   malformed.  The pointer to reason-phrase is returned in RP.  */
static gboolean
parse_status (const gchar *cline,
	      guint *status_return)
{
	/* (the variables must not be named `major' and `minor', because
	   that breaks compilation with SunOS4 cc.)  */
	guint mjr, mnr;
	guint statcode;
	const guchar *p, *line;

	line = (const guchar *)cline;

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

	/* FIXME bugzilla.eazel.com 1163 */
	switch (status) {
	case HTTP_STATUS_UNAUTHORIZED:
	case HTTP_STATUS_PROXY_AUTH_REQUIRED:
		return GNOME_VFS_ERROR_ACCESS_DENIED;
	case HTTP_STATUS_NOT_FOUND:
		return GNOME_VFS_ERROR_NOT_FOUND;
	case HTTP_STATUS_METHOD_NOT_ALLOWED:
	case HTTP_STATUS_BAD_REQUEST:
	case HTTP_STATUS_NOT_IMPLEMENTED:
	case HTTP_STATUS_UNSUPPORTED_VERSION:
		return GNOME_VFS_ERROR_NOT_SUPPORTED;
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

	for (result = 0; isdigit ((unsigned char)*p); p++)
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
	handle->file_info->size = size;
	handle->file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SIZE;
	return TRUE;
}

static gboolean
set_content_type (HttpFileHandle *handle,
		  const gchar *value)
{
	gchar *p;

	g_free (handle->file_info->mime_type);

	if((p=strchr(value, ';')))
		handle->file_info->mime_type = g_strndup (value, p-value);
	else
		handle->file_info->mime_type = g_strdup (value);

	handle->file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	return TRUE;
}

static gboolean
set_last_modified (HttpFileHandle *handle,
		   const gchar *value)
{
	time_t time;

	if (! gnome_vfs_atotm (value, &time))
		return FALSE;

	handle->file_info->mtime = time;
	handle->file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MTIME;
	return TRUE;
}

static gboolean
set_access_time (HttpFileHandle *handle,
		 const gchar *value)
{
	time_t time;

	if (! gnome_vfs_atotm (value, &time))
		return FALSE;

	handle->file_info->atime = time;
	handle->file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_ATIME;
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

	/* Simply ignore headers we don't know.  */
	return TRUE;
}


/* Header/status reading.  */

static GnomeVFSResult
get_header (GnomeVFSIOBuf *iobuf,
	    GString *s)
{
	GnomeVFSResult result;
	GnomeVFSFileSize bytes_read;
	guint count;

	g_string_truncate (s, 0);

	count = 0;
	while (1) {
		gchar c;

		result = gnome_vfs_iobuf_read (iobuf, &c, 1, &bytes_read);
		if (result != GNOME_VFS_OK) {
			return result;
		}
		if (bytes_read == 0) {
			return GNOME_VFS_ERROR_EOF;
		}

		if (c == '\n') {
			/* Handle continuation lines.  */
			if (count != 0 && (count != 1 || s->str[0] != '\r')) {
				gchar next;

				result = gnome_vfs_iobuf_peekc (iobuf, &next);
				if (result != GNOME_VFS_OK) {
					return result;
				}
				
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

/* rename this function? */
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
	if (result != GNOME_VFS_OK) {
		goto error;
	}

	if (! parse_status (header_string->str, &server_status)) {
		result = GNOME_VFS_ERROR_NOT_FOUND; /* FIXME bugzilla.eazel.com 1161 */
		goto error;
	}

	(*handle_return)->server_status = server_status;

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
			result = GNOME_VFS_ERROR_NOT_FOUND; /* FIXME bugzilla.eazel.com 1161 */
			break;
		}
	}

	if (result != GNOME_VFS_OK) {
		goto error;
	}

	g_string_free (header_string, TRUE);

#ifdef HTTP_VFS_CONTEXT_MESSAGES
	if ((*handle_return)->size_is_known) {
		gchar* msg;
		gchar* sz;

		sz = gnome_vfs_format_file_size_for_display ((*handle_return)->size);

		msg = g_strdup_printf(_("%s to retrieve"), sz);

		if(context) gnome_vfs_context_emit_message(context, msg);
		
		g_free(sz);
		g_free(msg);
	}
#endif /* HTTP_VFS_CONTEXT_MESSAGES */

	return GNOME_VFS_OK;

 error:
	http_file_handle_destroy (*handle_return);
	*handle_return = NULL;
	g_string_free (header_string, TRUE);
	return result;
}

/* BASE64 code ported from neon (http://www.webdav.org/neon) */
static const gchar b64_alphabet[65] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/=" };

static gchar *base64( const gchar *text ) {
    /* The tricky thing about this is doing the padding at the end,
     *      * doing the bit manipulation requires a bit of concentration only */
    gchar *buffer, *point;
    gint inlen, outlen;

    /* Use 'buffer' to store the output. Work out how big it should be...
     *      * This must be a multiple of 4 bytes */

    inlen = strlen( text );
    outlen = (inlen*4)/3;
    if( (inlen % 3) > 0 ) /* got to pad */
        outlen += 4 - (inlen % 3);

    buffer = g_malloc( outlen + 1 ); /* +1 for the \0 */

    /* now do the main stage of conversion, 3 bytes at a time,
     *      * leave the trailing bytes (if there are any) for later */

    for( point=buffer; inlen>=3; inlen-=3, text+=3 ) {
        *(point++) = b64_alphabet[ (*text)>>2 ];
        *(point++) = b64_alphabet[ ((*text)<<4 & 0x30) | (*(text+1))>>4 ];
        *(point++) = b64_alphabet[ ((*(text+1))<<2 & 0x3c) | (*(text+2))>>6 ];
        *(point++) = b64_alphabet[ (*(text+2)) & 0x3f ];
    }

    /* Now deal with the trailing bytes */
    if( inlen ) {
        /* We always have one trailing byte */
        *(point++) = b64_alphabet[ (*text)>>2 ];
        *(point++) = b64_alphabet[ ( ((*text)<<4 & 0x30) |
                                     (inlen==2?(*(text+1))>>4:0) ) ];
        *(point++) = (inlen==1?'=':b64_alphabet[ (*(text+1))<<2 & 0x3c ] );
        *(point++) = '=';
    }

    /* Null-terminate */
    *point = '\0';

    return buffer;
}


/**
 * sig_gconf_value_changed 
 * GTK signal function for when HTTP proxy GConf key has changed.
 */
static void
sig_gconf_value_changed (
	GConfClient* client,
	const gchar* key,
	GConfValue* value)
{
	g_mutex_lock (gl_mutex);
	
	if ( key && 0 == strcmp (KEY_GCONF_HTTP_PROXY, key)) {
		if (value) {
			if (GCONF_VALUE_STRING == value->type) {
				gl_http_proxy = g_strdup (gconf_value_to_string (value));
				DEBUG_HTTP (("New HTTP proxy: '%s'", gl_http_proxy));
			} else {
				DEBUG_HTTP (("Incorrect type for HTTP proxy setting"));
			}
		} else {
			DEBUG_HTTP (("HTTP proxy unset"));
			g_free (gl_http_proxy);
			gl_http_proxy = NULL;
		}
	}

	g_mutex_unlock (gl_mutex);
}

/**
 * host_port_from_string
 * splits a <host>:<port> formatted string into its separate components
 */
static gboolean
host_port_from_string (
	const gchar *http_proxy,
	gchar **p_proxy_host, 
	guint *p_proxy_port)
{
	gchar *port_part;
	
	port_part = strchr (http_proxy, ':');

	if (port_part && '\0' != ++port_part && p_proxy_port) {
		*p_proxy_port = (guint) strtoul (port_part, NULL, 10);
	} else if (p_proxy_port) {
		*p_proxy_port = DEFAULT_HTTP_PROXY_PORT;
	}

	if (p_proxy_host) {
		if ( port_part != http_proxy ) {
			*p_proxy_host = g_strndup (http_proxy, port_part - http_proxy - 1);
		} else {
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * http_proxy_for_host_port
 * Retrives an appropriate HTTP proxy for a given host and port.
 * Currently, only a single HTTP proxy is implemented (there's no way for
 * specifying non-proxy domain names's).  Returns FALSE if the connect should
 * take place directly
 */
static gboolean
http_proxy_for_host_port (
	const gchar *host,
	guint host_port,
	gchar **p_proxy_host,		/* Callee must free */
	guint *p_proxy_port)
{
	gboolean ret = FALSE;
	struct in_addr in, in_loop, in_mask;

	g_mutex_lock (gl_mutex);

	/* Don't force "localhost" or 127.x.x.x through the proxy */
	/* This is a special case that we'd like to generalize into a gconf config */

	inet_aton("127.0.0.0", &in_loop); 
	inet_aton("255.0.0.0", &in_mask); 

	if (host && ( 0 == strcmp (host, "localhost") 
		      || ( 0 != inet_aton (host, &in) 
		      		&& ((in.s_addr & in_mask.s_addr) == in_loop.s_addr)))
	) {
		ret = FALSE;
	} else 
	if (gl_http_proxy) {
		if (host_port_from_string (gl_http_proxy, p_proxy_host, p_proxy_port)) {
			ret = TRUE;
		} else {
			ret = FALSE;
		};
	} else {
		if (p_proxy_host) {
			p_proxy_host = NULL;
		}
		if (p_proxy_port) {
			p_proxy_port = NULL;
		}
		ret = FALSE;
	}

	g_mutex_unlock (gl_mutex);

	return ret;
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
	gchar *user_agent;
	guint host_port;
	gboolean proxy_connect = FALSE;
	gchar *proxy_host = NULL;
	guint proxy_port;
	const gchar *path;
	
	toplevel_uri = (GnomeVFSToplevelURI *) uri;

	if (toplevel_uri->host_port == 0)
		host_port = DEFAULT_HTTP_PORT;
	else
		host_port = toplevel_uri->host_port;

	if (toplevel_uri->host_name == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else if ( http_proxy_for_host_port (toplevel_uri->host_name, host_port, &proxy_host, &proxy_port)) {
		proxy_connect = TRUE;

		result = gnome_vfs_inet_connection_create (&connection,
							   proxy_host,
							   proxy_port,
							   context ? gnome_vfs_context_get_cancellation(context) : NULL);

	} else {
		proxy_connect = FALSE;

		result = gnome_vfs_inet_connection_create (&connection,
							   toplevel_uri->host_name,
							   host_port,
							   context ? gnome_vfs_context_get_cancellation(context) : NULL);
	}

	if (result != GNOME_VFS_OK) {
		return result;
	}
	
	iobuf = gnome_vfs_inet_connection_get_iobuf (connection);

	if (proxy_connect) {
		uri_string = gnome_vfs_uri_to_string (uri,
						      GNOME_VFS_URI_HIDE_USER_NAME
						      |GNOME_VFS_URI_HIDE_PASSWORD);

	} else {
		uri_string = gnome_vfs_uri_to_string (uri,
						      GNOME_VFS_URI_HIDE_USER_NAME
						      |GNOME_VFS_URI_HIDE_PASSWORD
						      |GNOME_VFS_URI_HIDE_HOST_NAME
						      |GNOME_VFS_URI_HIDE_HOST_PORT
						      |GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);
	}
	
	/* Request line.  */
	request = g_string_new (method);
	g_string_append (request, " ");
	g_string_append (request, uri_string);

	DEBUG_HTTP (("-->Making request '%s %s'", method, uri_string));

	g_free (uri_string);

	path = gnome_vfs_uri_get_path (uri);
	if (strlen (path) == 0) {
		g_string_append (request, "/");
	}


	/* Our code doesn't handle the chunked transfer-encoding that mod_dav 
	 * uses HTTP/1.1 request responses. */
	g_string_append (request, " HTTP/1.0\r\n");

	/* `Host:' header.  */
	if(toplevel_uri->host_port && toplevel_uri->host_port != 80)
		g_string_sprintfa (request, "Host: %s:%d\r\n",
			   toplevel_uri->host_name, toplevel_uri->host_port);
	else
		g_string_sprintfa (request, "Host: %s\r\n",
			   toplevel_uri->host_name);

	/* Basic authentication */
	if(toplevel_uri->user_name) {
		gchar *raw = g_strdup_printf("%s:%s", toplevel_uri->user_name,
				toplevel_uri->password?toplevel_uri->password:"");
		gchar *enc = base64(raw);
		g_string_sprintfa(request, "Authorization: Basic %s\n", enc);
		g_free(enc);
		g_free(raw);
	}

	/* `Accept:' header.  */
	g_string_append (request, "Accept: */*\r\n");

	/* `Content-Length' header.  */
	if(data)
		g_string_sprintfa (request, "Content-Length: %d\r\n", data->len);

	/* `User-Agent:' header.  */
	user_agent = getenv(CUSTOM_USER_AGENT_VARIABLE);
	if(!user_agent) user_agent = USER_AGENT_STRING;
	g_string_sprintfa (request, "User-Agent: %s\r\n", user_agent);

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

	if(data && data->data) {
#if 0
		g_print("sending data...\n");
#endif
		result = gnome_vfs_iobuf_write (iobuf, data->data, data->len,
						&bytes_written);
	}
	if (result == GNOME_VFS_OK)
		result = gnome_vfs_iobuf_flush (iobuf);
	if (result != GNOME_VFS_OK) {
		goto error;
	}
	
	/* Read the headers and create our internal HTTP file handle.  */
	result = create_handle (handle_return, uri, connection, iobuf,
				context);

	if (result != GNOME_VFS_OK)
		goto error;

	return result;

 error:
	g_free (proxy_host);
	gnome_vfs_iobuf_destroy (iobuf);
	gnome_vfs_inet_connection_destroy (connection, NULL);
	return result;
}

static void
http_handle_close (HttpFileHandle *handle, GnomeVFSContext *context)
{
	if (handle) {
		/* Both iobuf and connection can be NULL if a create and a close */
		/* was done with no i/o */
		if (handle->iobuf) {
			gnome_vfs_iobuf_flush (handle->iobuf);
			gnome_vfs_iobuf_destroy (handle->iobuf);
			handle->iobuf = NULL;
		}

		if (handle->connection) {
			gnome_vfs_inet_connection_destroy (handle->connection,
							 context ? gnome_vfs_context_get_cancellation(context) : NULL);
			handle->connection = NULL;
		}
				
#ifdef HTTP_VFS_CONTEXT_MESSAGES
		if (handle->uri_string) {
			gchar *msg;

			msg = g_strdup_printf(_("Closing connection to %s"),
								handle->uri_string);

			if(context) gnome_vfs_context_emit_message (context, msg);

			g_free (msg);
		}
#endif /* HTTP_VFS_CONTEXT_MESSAGES */

		
		http_file_handle_destroy (handle);
	}
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

	g_return_val_if_fail (uri->parent == NULL, GNOME_VFS_ERROR_INVALID_URI);
	g_return_val_if_fail (!(mode & GNOME_VFS_OPEN_READ && 
						mode & GNOME_VFS_OPEN_WRITE),
			      GNOME_VFS_ERROR_INVALID_OPEN_MODE);

	DEBUG_HTTP (("+Open"));

	if(mode & GNOME_VFS_OPEN_READ) {
		result = make_request (&handle, uri, "GET", NULL, NULL,
			       	context);
	} else {
		handle = http_file_handle_new(NULL, NULL, uri); /* shrug */
	}
	if (result == GNOME_VFS_OK)
		*method_handle = (GnomeVFSMethodHandle *) handle;

	DEBUG_HTTP (("-Open"));

	return result;
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
	/* try to write a zero length file - this appears to be the 
	 * only reliable way of testing if a put will succeed. 
	 * Xythos can apparently tell us if we have write permission by
	 * playing with LOCK, but mod_dav cannot. */
	HttpFileHandle *handle;
	GnomeVFSResult result;
	GByteArray *bytes = g_byte_array_new();

	DEBUG_HTTP (("+Create"));

#ifndef DAV_NO_CACHE
	cache_invalidate_uri_parent (uri);
#endif /* DAV_NO_CACHE */

      	result = make_request (&handle, uri, "PUT", bytes, NULL, context);

	if (result != GNOME_VFS_OK) {
		/* the PUT failed */
		return result;
	}

	/* clean up */
	g_byte_array_free(bytes, TRUE);
	http_handle_close(handle, context);

	/* FIXME bugzilla.eazel.com 1159: do we need to do something more intelligent here? */
	result = do_open(method, method_handle, uri, GNOME_VFS_OPEN_WRITE, context);

	DEBUG_HTTP (("-Create"));

	return result;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	HttpFileHandle *handle;
	GnomeVFSResult result = GNOME_VFS_OK;

	DEBUG_HTTP (("+Close"));

	handle = (HttpFileHandle *) method_handle;

	/* if the handle was opened in write mode then:
	 * 1) there won't be a connection open, and
	 * 2) there will be data to_be_written...
	 */
	if (handle->to_be_written != NULL) {
		GnomeVFSURI *uri = handle->uri;
		GByteArray *bytes = handle->to_be_written;

#ifndef DAV_NO_CACHE
		cache_invalidate_uri (uri);
#endif /* DAV_NO_CACHE */

		result = make_request(&handle, uri, "PUT", bytes, NULL, context);
	}

	http_handle_close (handle, context);

	DEBUG_HTTP (("-Close"));

	return result;
}
	
static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_read,
	  GnomeVFSContext *context)
{
	HttpFileHandle *handle;

	DEBUG_HTTP (("+Write"));

	handle = (HttpFileHandle *) method_handle;

	if(handle->to_be_written == NULL) {
		handle->to_be_written = g_byte_array_new();
	}
	handle->to_be_written = g_byte_array_append(handle->to_be_written, buffer, num_bytes);
	*bytes_read = num_bytes;

	DEBUG_HTTP (("+Write"));
	
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

	DEBUG_HTTP (("+Read"));

	handle = (HttpFileHandle *) method_handle;

	if (handle->file_info->flags & GNOME_VFS_FILE_INFO_FIELDS_SIZE) {
		GnomeVFSFileSize max_bytes;

		max_bytes = handle->file_info->size - handle->bytes_read;
		num_bytes = MIN (max_bytes, num_bytes);
	}

	result = gnome_vfs_iobuf_read (handle->iobuf, buffer, num_bytes,
				       bytes_read);

	handle->bytes_read += *bytes_read;

#ifdef HTTP_VFS_CONTEXT_MESSAGES
	{
		gchar *msg;
		gchar *read_str = NULL;
		gchar *total_str = NULL;

		read_str = gnome_vfs_format_file_size_for_display (handle->bytes_read);

		if (handle->file_info->flags & GNOME_VFS_FILE_INFO_FIELDS_SIZE) {
			total_str = gnome_vfs_format_file_size_for_display (handle->size);
		}

		if (total_str)
			msg = g_strdup_printf(_("%s of %s read"),
					      read_str, total_str);
		else
			msg = g_strdup_printf(_("%s read"), read_str);

		if (context) gnome_vfs_context_emit_message(context, msg);

		g_free(msg);
		g_free(read_str);
		if (total_str)
			g_free(total_str);
	}
#endif /* HTTP_VFS_CONTEXT_MESSAGES */

	DEBUG_HTTP (("-Read"));

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
			gchar *nc = xmlNodeGetContent(l);
			if(nc) {
				if(!strcmp((char *)l->name, "getcontenttype")) {
					file_info->valid_fields |= 
						GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
					if(!file_info->mime_type)
						file_info->mime_type = 
							g_strdup(nc);

#if 0
					g_print("found content-type: %s\n", nc);
#endif

				} else if(!strcmp((char *)l->name, "getcontentlength")){
					file_info->valid_fields |= 
						GNOME_VFS_FILE_INFO_FIELDS_SIZE;
					file_info->size = atoi(nc);

#if 0
					g_print("found content-length: %s\n", nc);
#endif

				}
				xmlFree(nc);
			}
			if(!strcmp((char *)l->name, "resourcetype")) {
				file_info->valid_fields |= 
					GNOME_VFS_FILE_INFO_FIELDS_TYPE;
				file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;

				if(l->childs && l->childs->name && 
					  !strcmp((char *)l->childs->name, 
					  "collection")) {
					file_info->type = 
						GNOME_VFS_FILE_TYPE_DIRECTORY;
					g_free(file_info->mime_type);
					file_info->mime_type = g_strdup("x-directory/webdav");
					file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
				}
			}
			/* FIXME bugzilla.eazel.com 2795: 
			 * all date related properties:
			 * creationdate
			 * getlastmodified
			 */
			l = l->next;
		}
		node = node->next;
	}

	if ( ! (file_info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE) ) {
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
		file_info->mime_type = g_strdup(gnome_vfs_mime_type_from_name_or_default (file_info->name, "text/plain"));
	}

	if ( ! (file_info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_TYPE ) ) {
		/* Is this a reasonable assumption ? */
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;
		file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	}
}

/* The problem here: Eazel Vault returns "https:" URI's which GnomeVFS doesn't recognize
 * So, if it's an https: uri scheme, just change it to "http" and we'll all be happy
 */
static GnomeVFSURI *
propfind_href_to_vfs_uri (const gchar *propfind_href_uri)
{
	size_t https_len = strlen("https:");
	GnomeVFSURI *ret;

	if ( 0 == strncmp (propfind_href_uri, "https:", https_len)) {
		gchar *new_uri;
		new_uri = g_strconcat ("http:", https_len + propfind_href_uri, NULL);
		ret = gnome_vfs_uri_new (new_uri);
		g_free (new_uri);
	} else {
		ret = gnome_vfs_uri_new(propfind_href_uri);
	}

	return ret;
}

static GnomeVFSFileInfo *
process_propfind_response(xmlNodePtr n, GnomeVFSURI *base_uri)
{
	GnomeVFSFileInfo *file_info = defaults_file_info_new();
	GnomeVFSURI *second_base = gnome_vfs_uri_append_path(base_uri, "/");

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	while(n != NULL) {
		if(!strcmp((char *)n->name, "href")) {
			gchar *nc = xmlNodeGetContent(n);
			gchar *nodecontent = gnome_vfs_unescape_string(nc, "/");
			xmlFree(nc);
			if(nodecontent && *nodecontent) {
				gint len;
				GnomeVFSURI *uri = propfind_href_to_vfs_uri (nodecontent);

				if( !strcmp(base_uri->text, uri->text) ||
				    !strcmp(second_base->text, uri->text)) {
					file_info->name = NULL; /* this file is the . directory */
				} else {
					file_info->name = gnome_vfs_uri_extract_short_name(uri);
					gnome_vfs_uri_unref(uri);

					len = strlen(file_info->name)-1;
					if(file_info->name[len] == '/') {
						/* trim trailing `/` - it confuses stuff */
						file_info->name[len] = '\0';
					}
				}
			} else {
				g_warning("got href without contents in PROPFIND response");
			}

#if 0
			if(!strncmp(uri_string, nodecontent, strlen(uri_string))) {
				/* our DAV server is prepending the 
				 * current path
				 */
				if(strcmp(uri_string, nodecontent)) {
					file_info->name = g_strdup(nodecontent+strlen(uri_string));
				} else {
					file_info->name = g_strdup("");
				}
			} else {
				file_info->name = 
					g_strdup(nodecontent);
			}
#endif
			g_free(nodecontent);
		} else if(!strcmp((char *)n->name, "propstat")) {
			process_propfind_propstat(n->childs, file_info);
		}
		n = n->next;
	}

	gnome_vfs_uri_unref(second_base);

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
	gchar *raw_uri;
	gchar *unescaped_uri_string;
	GnomeVFSURI *unescaped_uri;
	gchar *extraheaders = g_strdup_printf("Depth: %d\r\n", depth);
	gboolean found_root_node_props;

	GByteArray *request = g_byte_array_new();
	gchar *request_str = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
		"<D:propfind xmlns:D=\"DAV:\">"
		"<D:prop>"
                "<D:creationdate/>"
                "<D:getcontentlength/>"
                "<D:getcontenttype/>"
                "<D:getlastmodified/>"
                "<D:resourcetype/>"
		"</D:prop>"
		/*"<D:allprop/>"*/
		"</D:propfind>";

	request = g_byte_array_append(request, request_str, 
			strlen(request_str));

	parserContext = xmlCreatePushParserCtxt(NULL, NULL, "", 0, "PROPFIND");

#ifndef DAV_NO_CACHE
	if (depth > 0) {
		cache_invalidate_uri_and_children (uri);
	}
#endif /* DAV_NO_CACHE */

	result = make_request (&handle, uri, "PROPFIND", request, 
			extraheaders, context);

	/* FIXME -- it looks like some http servers (eg, www.yahoo.com) treat
	 * PROPFIND as a GET and return a 200 OK.  Others may return access
	 * denied errors or redirects or any other legal response.  This
	 * case probably needs to be made more robust
	 */
	if(result == GNOME_VFS_OK && handle->server_status != 207) { /* Multi-Status */
		DEBUG_HTTP(("HTTP server returned an invalid PROPFIND response: %d", handle->server_status));
		result = GNOME_VFS_ERROR_NOT_SUPPORTED;
	}

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
		return GNOME_VFS_ERROR_CORRUPTED_DATA;

	cur = doc->root;

	if(strcmp((char *)cur->name, "multistatus")) {
		DEBUG_HTTP(("Couldn't find <multistatus>.\n"));
		return GNOME_VFS_ERROR_CORRUPTED_DATA;
	}

	cur = cur->childs;

	raw_uri = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	unescaped_uri_string = gnome_vfs_unescape_string(raw_uri, "/");
	unescaped_uri = gnome_vfs_uri_new(unescaped_uri_string);
	g_free(raw_uri);
	raw_uri = NULL;
	g_free(unescaped_uri_string);
	unescaped_uri_string = NULL;

	found_root_node_props = FALSE;
	while(cur != NULL) {
		if(!strcmp((char *)cur->name, "response")) {
			GnomeVFSFileInfo *file_info =
				process_propfind_response(cur->childs, 
					unescaped_uri);

			if(file_info->name) { 
				handle->files = g_list_append(handle->files, file_info);
			} else {
				/* This response refers to the root node */
				/* Abandon the old information that came from create_handle*/

				file_info->name = handle->file_info->name;
				handle->file_info->name = NULL;
				gnome_vfs_file_info_unref (handle->file_info);
				handle->file_info = file_info;
				found_root_node_props = TRUE;
			}
			
		} else {
			DEBUG_HTTP(("expecting <response> got <%s>\n", cur->name));
		}
		cur = cur->next;
	}

	if (!found_root_node_props) {
		DEBUG_HTTP (("Failed to find root request node properties during propfind"));
		result = GNOME_VFS_ERROR_CORRUPTED_DATA;
	}

#ifndef DAV_NO_CACHE
	cache_add_uri_and_children (uri, handle->file_info, handle->files);
#endif /* DAV_NO_CACHE */


	gnome_vfs_uri_unref(unescaped_uri);

	g_free(buffer);
	g_free(extraheaders);

	xmlFreeParserCtxt(parserContext);

	return result;
}

static GnomeVFSResult
do_open_directory(GnomeVFSMethod *method,
	GnomeVFSMethodHandle **method_handle,
	GnomeVFSURI *uri,
	GnomeVFSFileInfoOptions options,
	const GnomeVFSDirectoryFilter *filter,
	GnomeVFSContext *context) 
{
	/* TODO move to using the gnome_vfs_file_info_list family of functions */
	GnomeVFSResult result;
	HttpFileHandle *handle = NULL;
#ifndef DAV_NO_CACHE
	GnomeVFSFileInfo * file_info_cached;
	GList *child_file_info_cached_list = NULL;
#endif /*DAV_NO_CACHE*/

	DEBUG_HTTP (("+Open_Directory options: %d dirfilter: 0x%08x URI: '%s'", options, (unsigned int) filter, gnome_vfs_uri_to_string( uri, 0)));

#ifndef DAV_NO_CACHE
	/* Check the cache--is this even a directory?  
	 * (Nautilus, in particular, seems to like to make this call on non directories
	 */

	file_info_cached = cache_check_uri (uri);

	if (file_info_cached) {
		if ( GNOME_VFS_FILE_TYPE_DIRECTORY != file_info_cached->type ) {
			gnome_vfs_file_info_unref (file_info_cached);
			result = GNOME_VFS_ERROR_NOT_A_DIRECTORY;
			goto error;
		}
		gnome_vfs_file_info_unref (file_info_cached);
		file_info_cached = NULL;
	}


	/* The check for directory contents is more stringent */
	file_info_cached = cache_check_directory_uri (uri, &child_file_info_cached_list);

	if (file_info_cached) {
		handle = http_file_handle_new (NULL, NULL, uri);
		handle->file_info = file_info_cached;
		handle->files = child_file_info_cached_list;
		result = GNOME_VFS_OK;
	} else {
#endif /*DAV_NO_CACHE*/
		result = make_propfind_request(&handle, uri, 1, context);
		/* mfleming -- is this necessary?  Most DAV server's I've seen don't have the horrible
		 * lack-of-trailing-/-is-a-301 problem for PROPFIND's
		 */
		if (result == GNOME_VFS_ERROR_NOT_FOUND) { /* 404 not found */
			if(uri->text && *uri->text 
			   && uri->text[strlen(uri->text)-1] != '/') {
				GnomeVFSURI *tmpuri = gnome_vfs_uri_append_path(uri, "/");
				result = do_open_directory(method, (GnomeVFSMethodHandle **)&handle, tmpuri, options, filter, context);
				gnome_vfs_uri_unref(tmpuri);

			}
		}

		if ( GNOME_VFS_OK == result 
		     && GNOME_VFS_FILE_TYPE_DIRECTORY != handle->file_info->type) {
			result = GNOME_VFS_ERROR_NOT_A_DIRECTORY;
			http_handle_close (handle, context);
			handle = NULL;
		}
#ifndef DAV_NO_CACHE
	}
#endif /*DAV_NO_CACHE*/
	
	*method_handle = (GnomeVFSMethodHandle *)handle;

#ifndef DAV_NO_CACHE
error:
#endif /*DAV_NO_CACHE*/
	DEBUG_HTTP (("-Open_Directory (%d)", result));

	return result;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod *method,
	GnomeVFSMethodHandle *method_handle,
	GnomeVFSContext *context) 
{
	
	HttpFileHandle *handle;

	DEBUG_HTTP (("+Close_Directory"));

	handle = (HttpFileHandle *) method_handle;

	http_handle_close(handle, context);

	DEBUG_HTTP (("-Close_Directory"));

	return GNOME_VFS_OK;
}
       
static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
       GnomeVFSMethodHandle *method_handle,
       GnomeVFSFileInfo *file_info,
       GnomeVFSContext *context)
{
	HttpFileHandle *handle;

	DEBUG_HTTP (("+Read_Directory"));

	handle = (HttpFileHandle *) method_handle;

	if(handle->files && g_list_length(handle->files)) {
		GnomeVFSFileInfo *original_info = g_list_nth_data(handle->files, 0);
		gboolean found_entry = FALSE;

		/* mfleming -- Why is this check here?  Does anyone set original_info->name to NULL? */
		if(original_info->name && original_info->name[0]) {
			gnome_vfs_file_info_copy (file_info, original_info); 
			found_entry = TRUE;
		}

		/* remove our GnomeVFSFileInfo from the list */
		handle->files = g_list_remove(handle->files, original_info);
		gnome_vfs_file_info_unref(original_info);

		DEBUG_HTTP (("-Read_Directory"));
	
		/* mfleming -- Is this necessary? */
		if(found_entry) {
			return GNOME_VFS_OK;
		} else {
			return do_read_directory (method, method_handle, file_info, context);
		}
	} else {
		DEBUG_HTTP (("-Read_Directory"));

		return GNOME_VFS_ERROR_EOF;
	}
}
 

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  GnomeVFSContext *context)
{
	HttpFileHandle *handle;
	GnomeVFSResult result;
#ifndef DAV_NO_CACHE
	GnomeVFSFileInfo * file_info_cached;
#endif /*DAV_NO_CACHE*/

	DEBUG_HTTP (("+Get_File_Info options: %d", options));

#ifndef DAV_NO_CACHE
	file_info_cached = cache_check_uri (uri);

	if (file_info_cached) {
		gnome_vfs_file_info_copy (file_info, file_info_cached);
		gnome_vfs_file_info_unref (file_info_cached);
		result = GNOME_VFS_OK;
	} else {
#endif /*DAV_NO_CACHE*/
		/*
		 * Start off by making a PROPFIND request.  Fall back to a HEAD if it fails
		 */

		result = make_propfind_request(&handle, uri, 0, context);

		/* Note that theoretically we could not bother with this request if we get a 404 back,
		 * but since some servers seem to return wierd things on PROPFIND (mostly 200 OK's...)
		 * I'm not going to count on the PROPFIND response....
		 */ 
		if (result == GNOME_VFS_OK) {
			gnome_vfs_file_info_copy (file_info, handle->file_info);
		} else {
			result = make_request (&handle, uri, "HEAD", NULL, NULL, 
				       	context);

			if (result == GNOME_VFS_OK) {
				gnome_vfs_file_info_copy (file_info, handle->file_info);
			} else {
				if (result == GNOME_VFS_ERROR_NOT_FOUND) { /* 404 not found */
					/* FIXME - mfleming - is this code really appropriate?
					 * In any case, it doesn't seem to be appropriate for a DAV-enabled
					 * server, since they don't seem to send 301's when you PROPFIND collections
					 * without a trailing '/'
					 */
					if(uri->text && *uri->text &&
							uri->text[strlen(uri->text)-1] != '/') {
						GnomeVFSURI *tmpuri = gnome_vfs_uri_append_path(uri, "/");

						result = do_get_file_info(method, tmpuri, file_info, options, context);
						gnome_vfs_uri_unref(tmpuri);
					}
				}

				DEBUG_HTTP (("-Get_File_Info"));
				return result;
			}
		}

		http_handle_close (handle, context);
		handle = NULL;
#ifndef DAV_NO_CACHE
	}
#endif /* DAV_NO_CACHE */
	DEBUG_HTTP (("-Get_File_Info"));

	return result;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      GnomeVFSContext *context)
{
	GnomeVFSResult result;

	DEBUG_HTTP (("+Get_File_Info_From_Handle"));

	result = do_get_file_info(method, ((HttpFileHandle *)method_handle)->uri, 
			file_info, options, context);

	DEBUG_HTTP (("-Get_File_Info_From_Handle"));

	return result;
}

static gboolean
do_is_local (GnomeVFSMethod *method,
	     const GnomeVFSURI *uri)
{
	DEBUG_HTTP (("+Is_Local"));
	return FALSE;
	DEBUG_HTTP (("-Is_Local"));

}

static GnomeVFSResult do_make_directory(GnomeVFSMethod * method,
                     GnomeVFSURI * uri,
                     guint perm, GnomeVFSContext * context) {
	/* MKCOL /path HTTP/1.0 */

	HttpFileHandle *handle;
	GnomeVFSResult result;

	DEBUG_HTTP (("+Make_Directory"));

#ifndef DAV_NO_CACHE
	cache_invalidate_uri_parent (uri);
#endif /* DAV_NO_CACHE */

	result =  make_request (&handle, uri, "MKCOL", NULL, NULL, context);

	http_handle_close (handle, context);

	DEBUG_HTTP (("-Make_Directory"));

	return result;
}

static GnomeVFSResult do_remove_directory(GnomeVFSMethod * method,
                       GnomeVFSURI * uri, GnomeVFSContext * context) {
	/* DELETE /path HTTP/1.0 */
	HttpFileHandle *handle;
	GnomeVFSResult result;

	DEBUG_HTTP (("+Remove_Directory"));

#ifndef DAV_NO_CACHE
	cache_invalidate_uri_parent (uri);
#endif /* DAV_NO_CACHE */

	result = make_request (&handle, uri, "DELETE", NULL, NULL,
                        context);
	http_handle_close (handle, context);

	DEBUG_HTTP (("-Remove_Directory"));

	return result;
}

/* a strcmp that doesn't barf on NULLs */
static gint
my_strcmp(const gchar *a, const gchar *b) {
	if( (!a && b) || (a && !b) ) return 1;
	if( (!a && !b) ) return 0;
	return strcmp(a,b);
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	/* MOVE /path1 HTTP/1.0
	 * Destination: /path2 */
	HttpFileHandle *handle;
	GnomeVFSResult result;

	gchar *destpath, *destheader;

	DEBUG_HTTP (("+Move"));

	if(my_strcmp(gnome_vfs_uri_get_scheme(old_uri), gnome_vfs_uri_get_scheme(new_uri)) ||
		my_strcmp(gnome_vfs_uri_get_host_name(old_uri), gnome_vfs_uri_get_host_name(new_uri)) ||
	  	my_strcmp(gnome_vfs_uri_get_user_name(old_uri), gnome_vfs_uri_get_user_name(new_uri)) ||
	  	my_strcmp(gnome_vfs_uri_get_password(old_uri), gnome_vfs_uri_get_password(new_uri)) ||
		(gnome_vfs_uri_get_host_port(old_uri) != gnome_vfs_uri_get_host_port(new_uri))) {

		/* the host/username/password/port are different */
		return GNOME_VFS_ERROR_NOT_SAME_FILE_SYSTEM;
	}

	destpath = gnome_vfs_uri_to_string(new_uri, GNOME_VFS_URI_HIDE_USER_NAME|GNOME_VFS_URI_HIDE_PASSWORD);
	destheader = g_strdup_printf("Destination: %s\r\n", destpath);


	result = make_request (&handle, old_uri, "MOVE", NULL, destheader, context);
	http_handle_close (handle, context);

#ifndef DAV_NO_CACHE
	cache_invalidate_uri_parent (old_uri);
	cache_invalidate_uri_parent (new_uri);
#endif /*DAV_NO_CACHE*/

	DEBUG_HTTP (("-Move"));

	return result;
	//return GNOME_VFS_ERROR_NOT_SUPPORTED;
}


static GnomeVFSResult do_unlink(GnomeVFSMethod * method,
             GnomeVFSURI * uri, GnomeVFSContext * context)
{
	GnomeVFSResult result;
	
	DEBUG_HTTP (("+Unlink"));
	result = do_remove_directory(method, uri, context);
	DEBUG_HTTP (("-Unlink"));

	return result;
}


static GnomeVFSMethod method = {
	do_open,
	do_create,
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
	do_make_directory,
	do_remove_directory,
	do_move,
	do_unlink,
	NULL,
	NULL, /* truncate */
	NULL, /* find_directory */
	NULL  /* create_symbolic_link */
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
        char *argv[] = {"dummy"};
        int argc = 1;
        GError *err_gconf = NULL;
        GConfValue *val_gconf = NULL;

	/* Ensure GConf is init'd.  If more modules start to rely on
	 * GConf, then this should probably be moved into a more 
	 * central location
	 */

	if (!gconf_is_initialized ()) {
		/* auto-initializes OAF if necessary */
		gconf_init (argc, argv, NULL);
	}

	/* ensure GTK is inited for gconf-client. */
	gtk_type_init();
	gtk_signal_init();

	gl_client = gconf_client_get_default ();

	gtk_object_ref(GTK_OBJECT(gl_client));
	gtk_object_sink(GTK_OBJECT(gl_client));

#ifdef G_THREADS_ENABLED
        if (g_thread_supported ())
                gl_mutex = g_mutex_new ();
        else
                gl_mutex = NULL;
#endif


	gconf_client_add_dir (gl_client, PATH_GCONF_GNOME_VFS, GCONF_CLIENT_PRELOAD_NONE, &err_gconf);

	if (err_gconf) {
		DEBUG_HTTP (("GConf error during client_add_dir '%s'", err_gconf->message));
		g_error_free (err_gconf);
	}

	gtk_signal_connect (GTK_OBJECT(gl_client), "value_changed", (GtkSignalFunc) sig_gconf_value_changed, NULL);

	/* Load the http proxy setting */

	val_gconf = gconf_client_get (gl_client, KEY_GCONF_HTTP_PROXY, &err_gconf);

	if (err_gconf) {
		DEBUG_HTTP (("GConf error during client_get '%s'", err_gconf->message));
		g_error_free (err_gconf);
	} else if (val_gconf) {
		sig_gconf_value_changed (gl_client, KEY_GCONF_HTTP_PROXY, val_gconf);
	}

#ifndef DAV_NO_CACHE
	cache_init();
#endif /*DAV_NO_CACHE*/

	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
	gtk_signal_disconnect_by_func (GTK_OBJECT(gl_client), (GtkSignalFunc) sig_gconf_value_changed, NULL);

	gtk_object_destroy(GTK_OBJECT(gl_client));
	gtk_object_unref(GTK_OBJECT(gl_client));

#ifndef DAV_NO_CACHE
	cache_shutdown();
#endif /*DAV_NO_CACHE*/

#ifdef G_THREADS_ENABLED
	if (g_thread_supported ()) 
		g_mutex_free (gl_mutex);
#endif
	gl_client = NULL;
}
