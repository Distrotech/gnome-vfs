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


struct _GnomeVFSAsyncContext {
	GnomeVFSJob *job;
};

/* In practice, GnomeVFSAsyncHandle does not exist, and only
   GnomeVFSHandles are used.  But this way we make sure programmers
   use handles appropriately.  */
struct _GnomeVFSAsyncHandle {
	gpointer dummy;
};


GnomeVFSAsyncContext *
gnome_vfs_async_context_new (void)
{
	GnomeVFSAsyncContext *new;

	new = g_new (GnomeVFSAsyncContext, 1);

	new->job = gnome_vfs_job_new (new);

	return new;
}

void
gnome_vfs_async_context_destroy (GnomeVFSAsyncContext *context)
{
	/* FIXME: What if this fails?  */
	gnome_vfs_job_destroy (context->job);

	g_free (context);
}


GnomeVFSResult
gnome_vfs_async_open (GnomeVFSAsyncContext *context,
		      const gchar *text_uri,
		      GnomeVFSOpenMode open_mode,
		      GnomeVFSAsyncOpenCallback callback,
		      gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSOpenJob *open_job;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	open_job = &job->info.open;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_OPEN;
	job->callback = callback;
	job->callback_data = callback_data;

	open_job->request.text_uri = g_strdup (text_uri);
	open_job->request.open_mode = open_mode;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_create (GnomeVFSAsyncContext *context,
			const gchar *text_uri,
			GnomeVFSOpenMode open_mode,
			gboolean exclusive,
			guint perm,
			GnomeVFSAsyncOpenCallback callback,
			gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSCreateJob *create_job;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	create_job = &job->info.create;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_CREATE;
	job->callback = callback;
	job->callback_data = callback_data;

	create_job->request.text_uri = g_strdup (text_uri);
	create_job->request.open_mode = open_mode;
	create_job->request.exclusive = exclusive;
	create_job->request.perm = perm;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_close (GnomeVFSAsyncContext *context,
		       GnomeVFSAsyncHandle *handle,
		       GnomeVFSAsyncCloseCallback callback,
		       gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSCloseJob *close_job;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	close_job = &job->info.close;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_CLOSE;
	job->callback = callback;
	job->callback_data = callback_data;

	close_job->request.handle = (GnomeVFSHandle *) handle;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_read (GnomeVFSAsyncContext *context,
		      GnomeVFSAsyncHandle *handle,
		      gpointer buffer,
		      guint bytes,
		      GnomeVFSAsyncReadCallback callback,
		      gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSReadJob *read_job;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	read_job = &job->info.read;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_READ;
	job->callback = callback;
	job->callback_data = callback_data;

	read_job->request.handle = (GnomeVFSHandle *) handle;
	read_job->request.buffer = buffer;
	read_job->request.num_bytes = bytes;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_async_write (GnomeVFSAsyncContext *context,
		       GnomeVFSAsyncHandle *handle,
		       gconstpointer buffer,
		       guint bytes,
		       GnomeVFSAsyncWriteCallback callback,
		       gpointer callback_data)
{
	GnomeVFSJob *job;
	GnomeVFSWriteJob *write_job;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	write_job = &job->info.write;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_WRITE;
	job->callback = callback;
	job->callback_data = callback_data;

	write_job->request.handle = (GnomeVFSHandle *) handle;
	write_job->request.buffer = buffer;
	write_job->request.num_bytes = bytes;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}


static gchar **
copy_meta_keys (gchar *meta_keys[])
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
gnome_vfs_async_load_directory (GnomeVFSAsyncContext *context,
				const gchar *text_uri,
				GnomeVFSFileInfoOptions options,
				gchar *meta_keys[],
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

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	load_directory_job = &job->info.load_directory;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_LOAD_DIRECTORY;
	job->callback = callback;
	job->callback_data = callback_data;

	load_directory_job->request.text_uri = g_strdup (text_uri);
	load_directory_job->request.options = options;
	load_directory_job->request.meta_keys = copy_meta_keys (meta_keys);
	load_directory_job->request.sort_rules = copy_sort_rules (sort_rules);
	load_directory_job->request.reverse_order = reverse_order;
	load_directory_job->request.filter_type = filter_type;
	load_directory_job->request.filter_options = filter_options;
	load_directory_job->request.filter_pattern = g_strdup (filter_pattern);
	load_directory_job->request.items_per_notification = items_per_notification;

	gnome_vfs_job_go (job);

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
gnome_vfs_async_xfer (GnomeVFSAsyncContext *context,
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

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_directory_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_name_list != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (target_directory_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (progress_callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	job = context->job;
	xfer_job = &job->info.xfer;

	gnome_vfs_job_prepare (job);

	job->type = GNOME_VFS_JOB_LOAD_DIRECTORY;
	job->callback = progress_callback;
	job->callback_data = callback_data;

	xfer_job->request.source_directory_uri = g_strdup (source_directory_uri);
	xfer_job->request.source_name_list = copy_string_list (source_name_list);
	xfer_job->request.target_directory_uri = g_strdup (target_directory_uri);
	xfer_job->request.target_name_list = copy_string_list (target_name_list);
	xfer_job->request.xfer_options = xfer_options;
	xfer_job->request.error_mode = error_mode;
	xfer_job->request.overwrite_mode = overwrite_mode;

	gnome_vfs_job_go (job);

	return GNOME_VFS_OK;
}
