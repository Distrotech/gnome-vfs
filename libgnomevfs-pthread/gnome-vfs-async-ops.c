/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-async-ops.c - Asynchronous operations supported by the
   GNOME Virtual File System (version for POSIX threads).

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

   Author: Ettore Perazzoli <ettore@comm2000.it> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-job.h"


GnomeVFSResult
gnome_vfs_async_cancel (GnomeVFSAsyncHandle *handle)
{
	GnomeVFSJob *job;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = (GnomeVFSJob *) handle;

	/* FIXME should free the handle.  */

	return gnome_vfs_job_cancel (job);
}


GnomeVFSResult
gnome_vfs_async_open (GnomeVFSAsyncHandle **handle_return,
		      const gchar *text_uri,
		      GnomeVFSOpenMode open_mode,
		      GnomeVFSAsyncOpenCallback callback,
		      gpointer callback_data)
{
	GnomeVFSURI *uri;
	GnomeVFSResult retval;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	retval = gnome_vfs_async_open_uri (handle_return, uri, open_mode,
					   callback, callback_data);

	gnome_vfs_uri_unref (uri);
	return retval;
}

GnomeVFSResult
gnome_vfs_async_open_uri (GnomeVFSAsyncHandle **handle_return,
			  GnomeVFSURI *uri,
			  GnomeVFSOpenMode open_mode,
			  GnomeVFSAsyncOpenCallback callback,
			  gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSOpenJob *open_job;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = gnome_vfs_job_new ();
	if (job == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_OPEN;
	job->callback = callback;
	job->callback_data = callback_data;

	open_job = &job->info.open;
	open_job->request.uri = gnome_vfs_uri_ref (uri);
	open_job->request.open_mode = open_mode;

	gnome_vfs_job_go (job);

	*handle_return = (GnomeVFSAsyncHandle *) job;

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_open_as_channel (GnomeVFSAsyncHandle **handle_return,
				 const gchar *text_uri,
				 GnomeVFSOpenMode open_mode,
				 guint advised_block_size,
				 GnomeVFSAsyncOpenAsChannelCallback callback,
				 gpointer callback_data)
{
	GnomeVFSResult retval;
	GnomeVFSURI *uri;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	retval = gnome_vfs_async_open_uri_as_channel
		(handle_return, uri, open_mode, advised_block_size, callback,
		 callback_data);

	gnome_vfs_uri_unref (uri);

	return retval;
}

GnomeVFSResult
gnome_vfs_async_open_uri_as_channel (GnomeVFSAsyncHandle **handle_return,
				     GnomeVFSURI *uri,
				     GnomeVFSOpenMode open_mode,
				     guint advised_block_size,
				     GnomeVFSAsyncOpenAsChannelCallback callback,
				     gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSOpenAsChannelJob *open_as_channel_job;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = gnome_vfs_job_new ();
	if (job == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_OPEN_AS_CHANNEL;
	job->callback = callback;
	job->callback_data = callback_data;

	open_as_channel_job = &job->info.open_as_channel;
	open_as_channel_job->request.uri = gnome_vfs_uri_ref (uri);
	open_as_channel_job->request.open_mode = open_mode;
	open_as_channel_job->request.advised_block_size = advised_block_size;

	gnome_vfs_job_go (job);

	*handle_return = (GnomeVFSAsyncHandle *) job;

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_create (GnomeVFSAsyncHandle **handle_return,
			const gchar *text_uri,
			GnomeVFSOpenMode open_mode,
			gboolean exclusive,
			guint perm,
			GnomeVFSAsyncOpenCallback callback,
			gpointer callback_data)
{
	GnomeVFSURI *uri;
	GnomeVFSResult retval;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	retval = gnome_vfs_async_create_uri (handle_return, uri, open_mode,
					     exclusive, perm, callback,
					     callback_data);

	gnome_vfs_uri_unref (uri);

	return retval;
}

GnomeVFSResult
gnome_vfs_async_create_uri (GnomeVFSAsyncHandle **handle_return,
			    GnomeVFSURI *uri,
			    GnomeVFSOpenMode open_mode,
			    gboolean exclusive,
			    guint perm,
			    GnomeVFSAsyncOpenCallback callback,
			    gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSCreateJob *create_job;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = gnome_vfs_job_new ();
	if (job == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_CREATE;
	job->callback = callback;
	job->callback_data = callback_data;

	create_job = &job->info.create;
	create_job->request.uri = gnome_vfs_uri_ref (uri);
	create_job->request.open_mode = open_mode;
	create_job->request.exclusive = exclusive;
	create_job->request.perm = perm;

	gnome_vfs_job_go (job);

	*handle_return = (GnomeVFSAsyncHandle *) job;

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_create_as_channel (GnomeVFSAsyncHandle **handle_return,
				   const gchar *text_uri,
				   GnomeVFSOpenMode open_mode,
				   gboolean exclusive,
				   guint perm,
				   GnomeVFSAsyncOpenAsChannelCallback callback,
				   gpointer callback_data)
{
	GnomeVFSURI *uri;
	GnomeVFSJob *job;
	GnomeVFSCreateAsChannelJob *create_as_channel_job;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	job = gnome_vfs_job_new ();
	if (job == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_CREATE_AS_CHANNEL;
	job->callback = callback;
	job->callback_data = callback_data;

	create_as_channel_job = &job->info.create_as_channel;
	create_as_channel_job->request.uri = gnome_vfs_uri_ref (uri);
	create_as_channel_job->request.open_mode = open_mode;
	create_as_channel_job->request.exclusive = exclusive;
	create_as_channel_job->request.perm = perm;

	gnome_vfs_job_go (job);

	*handle_return = (GnomeVFSAsyncHandle *) job;

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_close (GnomeVFSAsyncHandle *handle,
		       GnomeVFSAsyncCloseCallback callback,
		       gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSCloseJob *close_job;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = (GnomeVFSJob *) handle;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_CLOSE;
	job->callback = callback;
	job->callback_data = callback_data;

	close_job = &job->info.close;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_read (GnomeVFSAsyncHandle *handle,
		      gpointer buffer,
		      guint bytes,
		      GnomeVFSAsyncReadCallback callback,
		      gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSReadJob *read_job;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = (GnomeVFSJob *) handle;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_READ;
	job->callback = callback;
	job->callback_data = callback_data;

	read_job = &job->info.read;
	read_job->request.buffer = buffer;
	read_job->request.num_bytes = bytes;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_write (GnomeVFSAsyncHandle *handle,
		       gconstpointer buffer,
		       guint bytes,
		       GnomeVFSAsyncWriteCallback callback,
		       gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSWriteJob *write_job;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = (GnomeVFSJob *) handle;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_WRITE;
	job->callback = callback;
	job->callback_data = callback_data;

	write_job = &job->info.write;
	write_job->request.buffer = buffer;
	write_job->request.num_bytes = bytes;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}


static gchar **
copy_meta_keys (const gchar *meta_keys[])
{
	gchar **new;
	guint count, i;

	if (meta_keys == NULL)
		return NULL;

	for (count = 0; meta_keys[count] != NULL; count++)
		;

	new = g_new (gchar *, count + 1);

	for (i = 0; i < count; i++)
		new[i] = g_strdup (meta_keys[i]);
	new[i] = NULL;

	return new;
}

static GnomeVFSDirectorySortRule *
copy_sort_rules (GnomeVFSDirectorySortRule *rules)
{
	GnomeVFSDirectorySortRule *new;
	guint count, i;

	if (rules == NULL)
		return NULL;

	for (count = 0; rules[count] != GNOME_VFS_DIRECTORY_SORT_NONE; count++)
		;

	new = g_new (GnomeVFSDirectorySortRule, count + 1);

	for (i = 0; i < count; i++)
		new[i] = rules[i];
	new[i] = GNOME_VFS_DIRECTORY_SORT_NONE;

	return new;
}

GnomeVFSResult
gnome_vfs_async_load_directory (GnomeVFSAsyncHandle **handle_return,
				const gchar *text_uri,
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
	GnomeVFSURI *uri;
	GnomeVFSResult retval;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	retval = gnome_vfs_async_load_directory_uri (handle_return, uri,
						     options, meta_keys,
						     sort_rules,
						     reverse_order,
						     filter_type,
						     filter_options,
						     filter_pattern,
						     items_per_notification,
						     callback,
						     callback_data);

	gnome_vfs_uri_unref (uri);

	return retval;
}

GnomeVFSResult
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
	GnomeVFSJob *job;
	GnomeVFSLoadDirectoryJob *load_directory_job;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = gnome_vfs_job_new ();
	if (job == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_LOAD_DIRECTORY;
	job->callback = callback;
	job->callback_data = callback_data;

	load_directory_job = &job->info.load_directory;
	load_directory_job->request.uri = gnome_vfs_uri_ref (uri);
	load_directory_job->request.options = options;
	load_directory_job->request.meta_keys = copy_meta_keys (meta_keys);
	load_directory_job->request.sort_rules = copy_sort_rules (sort_rules);
	load_directory_job->request.reverse_order = reverse_order;
	load_directory_job->request.filter_type = filter_type;
	load_directory_job->request.filter_options = filter_options;
	load_directory_job->request.filter_pattern = g_strdup (filter_pattern);
	load_directory_job->request.items_per_notification = items_per_notification;

	gnome_vfs_job_go (job);

	*handle_return = (GnomeVFSAsyncHandle *) job;

	return GNOME_VFS_OK;
}


static GList *
copy_string_list (const GList *list)
{
	GList *new, *last, *p;

	if (list == NULL)
		return NULL;

	new = g_list_alloc ();
	new->data = g_strdup (list->data);
	last = new;

	for (p = list->next; p != NULL; p = p->next) {
		gchar *s;

		s = g_strdup (p->data);
		last = g_list_append (last, s);
		last = last->next;
	}

	return new;
}

GnomeVFSResult
gnome_vfs_async_xfer (GnomeVFSAsyncHandle **handle_return,
		      const gchar *source_directory_uri,
		      const GList *source_name_list,
		      const gchar *target_directory_uri,
		      const GList *target_name_list,
		      GnomeVFSXferOptions xfer_options,
		      GnomeVFSXferErrorMode error_mode,
		      GnomeVFSXferOverwriteMode overwrite_mode,
		      GnomeVFSAsyncXferProgressCallback progress_callback,
		      gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSXferJob *xfer_job;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_directory_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_name_list != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (target_directory_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (progress_callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = gnome_vfs_job_new ();
	if (job == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_LOAD_DIRECTORY;
	job->callback = progress_callback;
	job->callback_data = callback_data;

	xfer_job = &job->info.xfer;
	xfer_job->request.source_directory_uri = g_strdup (source_directory_uri);
	xfer_job->request.source_name_list = copy_string_list (source_name_list);
	xfer_job->request.target_directory_uri = g_strdup (target_directory_uri);
	xfer_job->request.target_name_list = copy_string_list (target_name_list);
	xfer_job->request.xfer_options = xfer_options;
	xfer_job->request.error_mode = error_mode;
	xfer_job->request.overwrite_mode = overwrite_mode;

	gnome_vfs_job_go (job);

	*handle_return = (GnomeVFSAsyncHandle *) job;

	return GNOME_VFS_OK;
}

guint
gnome_vfs_async_add_status_callback (GnomeVFSAsyncHandle *handle,
				     GnomeVFSStatusCallback callback,
				     gpointer user_data)
{
	GnomeVFSJob *job;

	g_return_val_if_fail (handle != NULL, 0);
	g_return_val_if_fail (callback != NULL, 0);

	job = (GnomeVFSJob *) handle;

	return gnome_vfs_message_callbacks_add(gnome_vfs_context_get_message_callbacks(job->context), callback, user_data);
}

void
gnome_vfs_async_remove_status_callback (GnomeVFSAsyncHandle *handle,
					guint callback_id)
{
	GnomeVFSJob *job;

	g_return_if_fail (handle != NULL);
	g_return_if_fail (callback_id > 0);

	job = (GnomeVFSJob *) handle;

	gnome_vfs_message_callbacks_remove(gnome_vfs_context_get_message_callbacks(job->context), callback_id);
}
