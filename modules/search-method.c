/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* search-method.c: Gnome-VFS interface to the medusa search service

   Copyright (C) 2000 Eazel

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

   Authors: Seth Nickell      (seth@eazel.com) 
            Maciej Stachowiak (mjs@eazel.com)  */


typedef struct
{
	GnomeVFSURI *uri;
	MedusaSearchServiceConnection *connection;
	GnomeVFSFileInfoOptions options;
} SearchDirectoryHandle;

typdef enum {
	SEARCH_URI_TYPE_TOP_LEVEL,
	SEARCH_URI_TYPE_IMMEDIATE_CHILD,
	SEARCH_URI_TYPE_DEEPER_CHILD
} SearchURIType;


static SearchDirectoryHandle *
search_directory_handle_new (GnomeVFSURI *uri,
			     GnomeVFSFileInfoOptions options,
			     MedusaSearchServiceConnection *connection)

{
	SearchHandle *result;

	result = g_new (DirectoryHandle, 1);

	result->uri = gnome_vfs_uri_ref (uri);
	result->connection = connection;

	result->options = options;

	return result;
}

static void
search_directory_handle_destroy (SearchDirectoryHandle *search_handle)
{
	/* FIXME: check if we're freeing everything we need to */
	if (directory_handle->connection != NULL) {
		medusa_search_service_connection_destroy (directory_handle->connection);
	}

	gnome_vfs_uri_unref (directory_handle->uri);
	g_free (directory_handle);
}


/* allocates a new char* representing the file uri portion of
   a gnome-vfs search uri */

SearchURIType
parse_search_uri (char          *search_uri,
		  char **child_uri)
{
	const char *first_slash;
	const char *second_slash;
	char *escaped;
	const char *endpoint;

	fisrt_slash = strchr (search_uri, '/');

	if (first_slash == NULL || first_slash[1] == '\0') {
		*child_uri = NULL;
		return  SEARCH_URI_TYPE_TOP_LEVEL;
	}

	second_slash = strchr (first_slash, '/');

	if (second_slash == NULL) {
		endpoint = search_uri + strlen (search_uri);
	} else {
		endpoint = second_slash;
	}
	
	escaped = g_new0 (char, second_slash - first_slash);
	strncpy (escaped, first_slash + 1, second_slash - first_slash - 1);
	
	if (second_slash == NULL || second_slash[1] == '\0') {
		*child_uri= gnome_vfs_unescape_string (escaped, NULL);
		g_free (escaped);

		return SEARCH_URI_TYPE_IMMEDIATE_CHILD;
	} else {
		unescaped = gnome_vfs_unescape_string (escaped, NULL);
		*child_uri = g_strconcat (unescaped, second_slash);
		
		g_free (escaped);
		g_free (unescaped);

		return SEARCH_URI_TYPE_DEEPER_CHILD;
	}


	return retval;
}

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{

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

}
static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_seek (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset,
	 GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_tell (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{

}

static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSFileSize where,
		    GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_open_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   const GList *meta_keys,
		   const GnomeVFSDirectoryFilter *filter,
		   GnomeVFSContext *context)
{
	MedusaSearchServiceConnection *connection;

	/* FIXME: should check if we are opening a search URI
	   directory thing, or a directory via a link from a search
	   URI result. */

	/* FIXME: check for invalid URIs */

	connection = medusa_search_service_connection_new ();

	*method_handle
		= (GnomeVFSMethodHandle *) directory_handle_new (uri, dir,
								 options,
								 connection);
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext *context)
{
	SearchHandle *search_handle;

	/* FIXME: should check if we are opening a search URI
	   directory thing, or a directory via a link from a search
	   URI result. */

	search_handle = (SearchHandle *) method_handle;

	medusa_search_service_connection_destroy (directory_handle->connection);
	directory_handle_destroy (directory_handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
	
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSContext *context)
{

}

static gboolean
do_is_local (GnomeVFSMethod *method,
	     const GnomeVFSURI *uri)
{
	
}

static GnomeVFSResult
do_make_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *a,
		  GnomeVFSURI *b,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_truncate (GnomeVFSMethod *method,
	     GnomeVFSURI *uri,
	     GnomeVFSFileSize where,
	     GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_find_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *near_uri,
		   GnomeVFSFindDirectoryKind kind,
		   GnomeVFSURI **result_uri,
		   gboolean create_if_needed,
		   guint permissions,
		   GnomeVFSContext *context)
{

}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod *method,
			 GnomeVFSURI *uri,
			 const char *target_reference,
			 GnomeVFSContext *context)
{

}

static GnomeVFSMethod method = {
	do_open,
	do_create,
	do_close,
	do_read, 
	do_write,
	do_seek,
	do_tell,
	do_truncate_handle,
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
	do_check_same_fs,
        do_set_file_info,
	do_truncate,
	do_find_directory,
	do_create_symbolic_link
};
