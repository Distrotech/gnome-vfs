/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* gnome-vfs-backend.c - Handling of asynchronocity backends in the GNOME
                         Virtual File System.

   Copyright (C) 2000 Red Hat, Inc.
   Copyright (C) 2000 Eazel, Inc.
   All rights reserved.

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

   Author: Elliot Lee <sopwith@redhat.com>
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gnome-vfs-backend.h"
#include "gnome-vfs-types.h"
#include "gnome-vfs-private-types.h"

#include "gnome-vfs.h"
#include <gmodule.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static GModule *gmod = NULL;
static gboolean (* gnome_vfs_backend_module_init)(gboolean deps_init);

static char backend_lower[128] = "";

const char *
gnome_vfs_backend_name (void)
{
	return (*backend_lower) ? backend_lower : NULL;
}

void
gnome_vfs_backend_loadinit (gpointer app, gpointer modinfo)
{
	char *backend;
	char backend_filename[256];
	gboolean retval;

	/* Decide which backend module to load, based on
	   (a) environment variable
	   (b) default
	*/
	if (gmod)
		return;

	backend = getenv("GNOME_VFS_BACKEND");
	if (!backend)
		backend = GNOME_VFS_DEFAULT_BACKEND;

	strcpy(backend_lower, backend);
	g_strdown(backend_lower);

	g_snprintf(backend_filename, sizeof(backend_filename), "libgnomevfs-%s.so.0", backend_lower);

	gmod = g_module_open(backend_filename, G_MODULE_BIND_LAZY);
	if (!gmod)
	{
		g_error("Could not open %s: %s", backend_filename, g_module_error());
	}
	g_snprintf(backend_filename, sizeof(backend_filename), "gnome_vfs_%s_init", backend_lower);
	retval = g_module_symbol(gmod, backend_filename, (gpointer *)&gnome_vfs_backend_module_init);
	if (!retval)
	{
		g_module_close(gmod); gmod = NULL;
		g_error("Could not locate module initialization function: %s", g_module_error());
	}
}

gboolean
gnome_vfs_backend_init (gboolean deps_init)
{
	g_assert (gmod);
	g_assert (gnome_vfs_backend_init);

	gnome_vfs_backend_module_init (deps_init);

	return TRUE;
}

typedef GnomeVFSResult (*GnomeVFSAsyncFunction) ();

static GnomeVFSAsyncFunction
func_lookup(const char *func_name)
{
	char *name;
	gpointer function;

	name = g_strdup_printf ("%s_%s", backend_lower, func_name);

	g_assert (gmod);
	if (!g_module_symbol (gmod, name, &function))
		function = NULL;

	return (GnomeVFSAsyncFunction) function;
}

#define CALL_BACKEND(name, parameters) \
G_STMT_START { \
	if (!real_##name) { \
		real_##name = func_lookup (#name); \
	} \
	result = real_##name == NULL \
		? GNOME_VFS_ERROR_INTERNAL \
		: real_##name parameters; \
} G_STMT_END

#define CALL_BACKEND_RETURN(name, parameters) \
G_STMT_START { \
	GnomeVFSResult result; \
	CALL_BACKEND (name, parameters); \
	return result; \
} G_STMT_END

typedef struct {
	GnomeVFSResult result;
	union {
		GnomeVFSAsyncOpenCallback open;
		GnomeVFSAsyncOpenAsChannelCallback open_as_channel;
		GnomeVFSAsyncCloseCallback close;
		GnomeVFSAsyncReadCallback read;
		GnomeVFSAsyncWriteCallback write;
		GnomeVFSAsyncGetFileInfoCallback get_file_info;
		GnomeVFSAsyncDirectoryLoadCallback load_directory;
	} callback;
	gpointer callback_data;
	GnomeVFSAsyncHandle *handle;
	gpointer buffer;
	GnomeVFSFileSize bytes_requested;
	GList *uris;
} CallbackData;

static gboolean
report_failure_open_callback (gpointer callback_data)
{
	CallbackData *data;

	data = callback_data;
	(* data->callback.open) (NULL,
				 data->result,
				 data->callback_data);
	g_free (data);

	return FALSE;
}

static void
report_failure_open (GnomeVFSResult result,
		     GnomeVFSAsyncOpenCallback callback,
		     gpointer callback_data)
{
	CallbackData *data;

	if (result == GNOME_VFS_OK)
		return;

	data = g_new0 (CallbackData, 1);
	data->callback.open = callback;
	data->result = result;
	data->callback_data = callback_data;
	g_idle_add (report_failure_open_callback, data);
}

void
gnome_vfs_async_open (GnomeVFSAsyncHandle **handle_return,
		      const gchar *text_uri,
		      GnomeVFSOpenMode open_mode,
		      GnomeVFSAsyncOpenCallback callback,
		      gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_open) (GnomeVFSAsyncHandle **handle_return,
					      const gchar *text_uri,
					      GnomeVFSOpenMode open_mode,
					      GnomeVFSAsyncOpenCallback callback,
					      gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_open,
		      (handle_return, text_uri, open_mode, callback, callback_data));
	report_failure_open (result, callback, callback_data);
}

void
gnome_vfs_async_open_uri (GnomeVFSAsyncHandle **handle_return,
			  GnomeVFSURI *uri,
			  GnomeVFSOpenMode open_mode,
			  GnomeVFSAsyncOpenCallback callback,
			  gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_open_uri) (GnomeVFSAsyncHandle **handle_return,
						  GnomeVFSURI *uri,
						  GnomeVFSOpenMode open_mode,
						  GnomeVFSAsyncOpenCallback callback,
						  gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_open_uri,
		      (handle_return, uri, open_mode, callback, callback_data));
	report_failure_open (result, callback, callback_data);
}

static gboolean
report_failure_open_as_channel_callback (gpointer callback_data)
{
	CallbackData *data;

	data = callback_data;
	(* data->callback.open_as_channel) (NULL,
					    NULL,
					    data->result,
					    data->callback_data);
	g_free (data);

	return FALSE;
}

static void
report_failure_open_as_channel (GnomeVFSResult result,
				GnomeVFSAsyncOpenAsChannelCallback callback,
				gpointer callback_data)
{
	CallbackData *data;
	
	if (result == GNOME_VFS_OK)
		return;
	
	data = g_new0 (CallbackData, 1);
	data->callback.open_as_channel = callback;
	data->result = result;
	data->callback_data = callback_data;
	g_idle_add (report_failure_open_as_channel_callback, data);
}

void
gnome_vfs_async_open_as_channel (GnomeVFSAsyncHandle **handle_return,
				 const gchar *text_uri,
				 GnomeVFSOpenMode open_mode,
				 guint advised_block_size,
				 GnomeVFSAsyncOpenAsChannelCallback callback,
				 gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_open_as_channel) (GnomeVFSAsyncHandle **handle_return,
							 const gchar *text_uri,
							 GnomeVFSOpenMode open_mode,
							 guint advised_block_size,
							 GnomeVFSAsyncOpenAsChannelCallback callback,
							 gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_open_as_channel,
		      (handle_return, text_uri, open_mode, advised_block_size,
		       callback, callback_data));
	report_failure_open_as_channel (result, callback, callback_data);
}

void	 
gnome_vfs_async_create (GnomeVFSAsyncHandle **handle_return,
			const gchar *text_uri,
			GnomeVFSOpenMode open_mode,
			gboolean exclusive,
			guint perm,
			GnomeVFSAsyncOpenCallback callback,
			gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_create) (GnomeVFSAsyncHandle **handle_return,
						const gchar *text_uri,
						GnomeVFSOpenMode open_mode,
						gboolean exclusive,
						guint perm,
						GnomeVFSAsyncOpenCallback callback,
						gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_create,
		      (handle_return, text_uri, open_mode, exclusive, perm,
		       callback, callback_data));
	report_failure_open (result, callback, callback_data);
}

void
gnome_vfs_async_create_as_channel (GnomeVFSAsyncHandle **handle_return,
				   const gchar *text_uri,
				   GnomeVFSOpenMode open_mode,
				   gboolean exclusive,
				   guint perm,
				   GnomeVFSAsyncOpenAsChannelCallback callback,
				   gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_create_as_channel) (GnomeVFSAsyncHandle **handle_return,
							   const gchar *text_uri,
							   GnomeVFSOpenMode open_mode,
							   gboolean exclusive,
							   guint perm,
							   GnomeVFSAsyncOpenAsChannelCallback callback,
							   gpointer callback_data) = NULL;

	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_create_as_channel,
		      (handle_return, text_uri, open_mode, exclusive, perm,
		       callback, callback_data));
	report_failure_open_as_channel (result, callback, callback_data);
}

void
gnome_vfs_async_create_uri (GnomeVFSAsyncHandle **handle_return,
			    GnomeVFSURI *text_uri,
			    GnomeVFSOpenMode open_mode,
			    gboolean exclusive,
			    guint perm,
			    GnomeVFSAsyncOpenCallback callback,
			    gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_create_uri) (GnomeVFSAsyncHandle **handle_return,
						    GnomeVFSURI *uri,
						    GnomeVFSOpenMode open_mode,
						    gboolean exclusive,
						    guint perm,
						    GnomeVFSAsyncOpenCallback callback,
						    gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_create_uri,
		      (handle_return, text_uri, open_mode, exclusive, perm,
		       callback, callback_data));
	report_failure_open (result, callback, callback_data);
}

static gboolean
report_failure_close_callback (gpointer callback_data)
{
	CallbackData *data;

	data = callback_data;
	(* data->callback.close) (data->handle,
				  data->result,
				  data->callback_data);
	g_free (data);

	return FALSE;
}

static void
report_failure_close (GnomeVFSResult result,
		      GnomeVFSAsyncHandle *handle,
		      GnomeVFSAsyncCloseCallback callback,
		      gpointer callback_data)
{
	CallbackData *data;
	
	if (result == GNOME_VFS_OK)
		return;
	
	data = g_new0 (CallbackData, 1);
	data->callback.close = callback;
	data->handle = handle;
	data->result = result;
	data->callback_data = callback_data;
	g_idle_add (report_failure_close_callback, data);
}

void	 
gnome_vfs_async_close (GnomeVFSAsyncHandle *handle,
		       GnomeVFSAsyncCloseCallback callback,
		       gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_close) (GnomeVFSAsyncHandle *handle,
					       GnomeVFSAsyncCloseCallback callback,
					       gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_close,
		      (handle, callback, callback_data));
	report_failure_close (result, handle, callback, callback_data);
}

static gboolean
report_failure_read_callback (gpointer callback_data)
{
	CallbackData *data;

	data = callback_data;
	(* data->callback.read) (data->handle,
				 data->result,
				 data->buffer,
				 data->bytes_requested,
				 0,
				 data->callback_data);
	g_free (data);

	return FALSE;
}

static void
report_failure_read (GnomeVFSResult result,
		     GnomeVFSAsyncHandle *handle,
		     gpointer buffer,
		     guint bytes_requested,
		     GnomeVFSAsyncReadCallback callback,
		     gpointer callback_data)
{
	CallbackData *data;
	
	if (result == GNOME_VFS_OK)
		return;
	
	data = g_new0 (CallbackData, 1);
	data->callback.read = callback;
	data->handle = handle;
	data->result = result;
	data->buffer = buffer;
	data->bytes_requested = bytes_requested,
	data->callback_data = callback_data;
	g_idle_add (report_failure_read_callback, data);
}

void	 
gnome_vfs_async_read (GnomeVFSAsyncHandle *handle,
		      gpointer buffer,
		      guint bytes,
		      GnomeVFSAsyncReadCallback callback,
		      gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_read) (GnomeVFSAsyncHandle *handle,
					      gpointer buffer,
					      guint bytes,
					      GnomeVFSAsyncReadCallback callback,
					      gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_read,
		      (handle, buffer, bytes,
		       callback, callback_data));
	report_failure_read (result,
			     handle, buffer, bytes,
			     callback, callback_data);
}

static gboolean
report_failure_write_callback (gpointer callback_data)
{
	CallbackData *data;

	data = callback_data;
	(* data->callback.write) (data->handle,
				 data->result,
				 data->buffer,
				 data->bytes_requested,
				 0,
				 data->callback_data);
	g_free (data);

	return FALSE;
}

static void
report_failure_write (GnomeVFSResult result,
		      GnomeVFSAsyncHandle *handle,
		      gconstpointer buffer,
		      guint bytes_requested,
		      GnomeVFSAsyncWriteCallback callback,
		      gpointer callback_data)
{
	CallbackData *data;
	
	if (result == GNOME_VFS_OK)
		return;
	
	data = g_new0 (CallbackData, 1);
	data->callback.write = callback;
	data->handle = handle;
	data->result = result;
	data->buffer = (gpointer) buffer;
	data->bytes_requested = bytes_requested,
	data->callback_data = callback_data;
	g_idle_add (report_failure_write_callback, data);
}

void	 
gnome_vfs_async_write (GnomeVFSAsyncHandle *handle,
		       gconstpointer buffer,
		       guint bytes,
		       GnomeVFSAsyncWriteCallback callback,
		       gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_write) (GnomeVFSAsyncHandle *handle,
					       gconstpointer buffer,
					       guint bytes,
					       GnomeVFSAsyncWriteCallback callback,
					       gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_write,
		      (handle, buffer, bytes,
		       callback, callback_data));
	report_failure_write (result,
			      handle, buffer, bytes,
			      callback, callback_data);
}

static gboolean
report_failure_get_file_info_callback (gpointer callback_data)
{
	CallbackData *data;
	GList *p, *results;
	GnomeVFSGetFileInfoResult *result;

	results = NULL;
	data = callback_data;

	/* Create a list of all the files with the same result for
	 * each one.
	 */
	for (p = data->uris; p != NULL; p = p->next) {
		result = g_new (GnomeVFSGetFileInfoResult, 1);
		result->uri = p->data;
		result->result = data->result;
		result->file_info = NULL;
		results = g_list_prepend (results, result);
	}
	results = g_list_reverse (results);

	/* Call back. */
	(* data->callback.get_file_info) (NULL,
					  results,
					  data->callback_data);

	/* Free the results list. */
	g_list_foreach (results, (GFunc) g_free, NULL);
	g_list_free (results);

	/* Free the callback data. */
	gnome_vfs_file_info_list_free (data->uris);
	g_free (data);

	return FALSE;
}

static void
report_failure_get_file_info (GnomeVFSResult result,
			      GList *uris,
			      GnomeVFSAsyncGetFileInfoCallback callback,
			      gpointer callback_data)
{
	CallbackData *data;

	if (result == GNOME_VFS_OK)
		return;

	data = g_new0 (CallbackData, 1);
	data->callback.get_file_info = callback;
	data->result = result;
	data->callback_data = callback_data;
	data->uris = gnome_vfs_file_info_list_copy (uris);
	g_idle_add (report_failure_get_file_info_callback, data);
}

void
gnome_vfs_async_get_file_info  (GnomeVFSAsyncHandle **handle_return,
				GList *uris,
				GnomeVFSFileInfoOptions options,
				const char * const meta_keys[],
				GnomeVFSAsyncGetFileInfoCallback callback,
				gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_get_file_info) (GnomeVFSAsyncHandle **handle_return,
						       GList *uris,
						       GnomeVFSFileInfoOptions options,
						       const char * const meta_keys[],
						       GnomeVFSAsyncGetFileInfoCallback callback,
						       gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_get_file_info,
		      (handle_return, uris, options, meta_keys,
		       callback, callback_data));
	report_failure_get_file_info (result, uris, callback, callback_data);
}

static gboolean
report_failure_load_directory_callback (gpointer callback_data)
{
	CallbackData *data;

	data = callback_data;
	(* data->callback.load_directory) (NULL,
					   data->result,
					   NULL,
					   0,
					   data->callback_data);
	g_free (data);

	return FALSE;
}

static void
report_failure_load_directory (GnomeVFSResult result,
			       GnomeVFSAsyncDirectoryLoadCallback callback,
			       gpointer callback_data)
{
	CallbackData *data;

	if (result == GNOME_VFS_OK)
		return;

	data = g_new0 (CallbackData, 1);
	data->callback.load_directory = callback;
	data->result = result;
	data->callback_data = callback_data;
	g_idle_add (report_failure_load_directory_callback, data);
}

void
gnome_vfs_async_load_directory_uri (GnomeVFSAsyncHandle **handle_return,
				    GnomeVFSURI *uri,
				    GnomeVFSFileInfoOptions options,
				    const gchar *meta_keys[],
				    GnomeVFSDirectorySortRule sort_rules[],
				    gboolean reverse_order,
				    GnomeVFSDirectoryFilterType filter_type,
				    GnomeVFSDirectoryFilterOptions filter_options,
				    const gchar *filter_pattern,
				    guint items_per_notification,
				    GnomeVFSAsyncDirectoryLoadCallback callback,
				    gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_load_directory_uri) (GnomeVFSAsyncHandle **handle_return,
							    GnomeVFSURI *uri,
							    GnomeVFSFileInfoOptions options,
							    const gchar *meta_keys[],
							    GnomeVFSDirectorySortRule sort_rules[],
							    gboolean reverse_order,
							    GnomeVFSDirectoryFilterType filter_type,
							    GnomeVFSDirectoryFilterOptions filter_options,
							    const gchar *filter_pattern,
							    guint items_per_notification,
							    GnomeVFSAsyncDirectoryLoadCallback callback,
							    gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_load_directory_uri,
		      (handle_return, uri, options, meta_keys, sort_rules,
		       reverse_order, filter_type, filter_options,
		       filter_pattern, items_per_notification,
		       callback, callback_data));
	report_failure_load_directory (result, callback, callback_data);
}

void
gnome_vfs_async_load_directory (GnomeVFSAsyncHandle **handle_return,
				const gchar *uri,
				GnomeVFSFileInfoOptions options,
				const gchar * const meta_keys[],
				GnomeVFSDirectorySortRule sort_rules[],
				gboolean reverse_order,
				GnomeVFSDirectoryFilterType filter_type,
				GnomeVFSDirectoryFilterOptions filter_options,
				const gchar *filter_pattern,
				guint items_per_notification,
				GnomeVFSAsyncDirectoryLoadCallback callback,
				gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_load_directory) (GnomeVFSAsyncHandle **handle_return,
							const gchar *uri,
							GnomeVFSFileInfoOptions options,
							const gchar * const meta_keys[],
							GnomeVFSDirectorySortRule sort_rules[],
							gboolean reverse_order,
							GnomeVFSDirectoryFilterType filter_type,
							GnomeVFSDirectoryFilterOptions filter_options,
							const gchar *filter_pattern,
							guint items_per_notification,
							GnomeVFSAsyncDirectoryLoadCallback callback,
							gpointer callback_data) = NULL;
	GnomeVFSResult result;

	CALL_BACKEND (gnome_vfs_async_load_directory,
		      (handle_return, uri, options, meta_keys, sort_rules, reverse_order,
		       filter_type, filter_options, filter_pattern, items_per_notification,
		       callback, callback_data));
	report_failure_load_directory (result, callback, callback_data);
}

GnomeVFSResult
gnome_vfs_async_xfer (GnomeVFSAsyncHandle **handle_return,
		      const gchar *source_dir,
		      const GList *source_name_list,
		      const gchar *target_dir,
		      const GList *target_name_list,
		      GnomeVFSXferOptions xfer_options,
		      GnomeVFSXferErrorMode error_mode,
		      GnomeVFSXferOverwriteMode overwrite_mode,
		      GnomeVFSAsyncXferProgressCallback progress_update_callback,
		      gpointer update_callback_data,
		      GnomeVFSXferProgressCallback progress_sync_callback,
		      gpointer sync_callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_xfer) (GnomeVFSAsyncHandle **handle_return,
					      const gchar *source_dir,
					      const GList *source_name_list,
					      const gchar *target_dir,
					      const GList *target_name_list,
					      GnomeVFSXferOptions xfer_options,
					      GnomeVFSXferErrorMode error_mode,
					      GnomeVFSXferOverwriteMode overwrite_mode,
					      GnomeVFSAsyncXferProgressCallback progress_update_callback,
					      gpointer update_callback_data,
					      GnomeVFSXferProgressCallback progress_sync_callback,
					      gpointer sync_callback_data) = NULL;

	CALL_BACKEND_RETURN (gnome_vfs_async_xfer,
			     (handle_return,
			      source_dir, source_name_list,
			      target_dir, target_name_list,
			      xfer_options, error_mode, overwrite_mode,
			      progress_update_callback, update_callback_data,
			      progress_sync_callback, sync_callback_data));
}

GnomeVFSResult
gnome_vfs_async_cancel (GnomeVFSAsyncHandle *handle)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_cancel)(GnomeVFSAsyncHandle *handle) = NULL;

	CALL_BACKEND_RETURN (gnome_vfs_async_cancel, (handle));
}

guint
gnome_vfs_async_add_status_callback (GnomeVFSAsyncHandle *handle,
				     GnomeVFSStatusCallback callback,
				     gpointer user_data)
{
	static guint
		(*real_gnome_vfs_async_add_status_callback) (GnomeVFSAsyncHandle *handle,
							     GnomeVFSStatusCallback callback,
							     gpointer user_data) = NULL;

	CALL_BACKEND_RETURN (gnome_vfs_async_add_status_callback,
			     (handle, callback, user_data));
}

void
gnome_vfs_async_remove_status_callback (GnomeVFSAsyncHandle *handle,
					guint callback_id)
{
	static void
		(*real_gnome_vfs_async_remove_status_callback) (GnomeVFSAsyncHandle *handle,
								guint callback_id) = NULL;

	if (!real_gnome_vfs_async_remove_status_callback)
	{
		real_gnome_vfs_async_remove_status_callback = (void (*)())
			func_lookup("gnome_vfs_async_remove_status_callback");
		if (!real_gnome_vfs_async_remove_status_callback)
			return;
	}

	real_gnome_vfs_async_remove_status_callback (handle, callback_id);
}

/* For testing only. */
extern int gnome_vfs_debug_get_thread_count (void);
int
gnome_vfs_debug_get_thread_count (void)
{
	GnomeVFSAsyncFunction function;

	function = func_lookup ("gnome_vfs_debug_get_thread_count");
	if (function == NULL) {
		return -1;
	}

	return (* function) ();
}
