/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-xfer.c - File transfers in the GNOME Virtual File System.

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
   Ettore Perazzoli <ettore@comm2000.it> 
   Pavel Cisler <pavel@eazel.com> 
   */

/* FIXME: Check that all the progress_info passed to the callback is
   correct.  */
/* FIXME: Check that all the flags passed by address are set at least once by
   functions that are expected to set them.  */
/* FIXME: There should be a `context' thingy in the file methods that would
   allow us to set a prefix URI for all the operation.  This way we can
   greatly optimize access to "strange" file systems.  */
/* FIXME: Handle all the GnomeVFSXferOptions.  */
/* FIXME: Move/copy symlinks properly.  */
/* FIXME: Redo progress_callback calling - should be called in the xfer thread
 	  only switching to the master thread to display progress dialogs, etc.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <sys/time.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"
#include "gnome-vfs-private-types.h"

/* Implementation of file transfers (`gnome_vfs_xfer*()' functions).  */


enum {
	/* size used for accounting for the expense of copying directories, 
	 * doing a move operation, etc. in a progress callback
	 * (during a move the file size is used for a regular file).
	 */
	DEFAULT_SIZE_OVERHEAD = 1024
};

/* bunch of utility list manipulation calls */
static GList *
g_string_list_deep_copy (const GList *source)
{
	GList *destination;
	GList *p;

	if (source == NULL)
		return NULL;

	destination = g_list_copy ((GList *)source);
	for (p = destination; p != NULL; p = p->next) {
		p->data = g_strdup (p->data);
	}

	return destination;
}

static void
g_list_deep_free (GList *list)
{
	GList *p;

	if (list == NULL)
		return;

	for (p = list; p != NULL; p = p->next) {
		g_free (p->data);
	}
	g_list_free (list);
}

/* in asynch mode the progress callback does a context switch every time
 * it gets called. We'll only call it every now and then to not loose a
 * lot of performance
 */
#define UPDATE_PERIOD 100LL * 1000LL

static gint64
system_time()
{
	struct timeval tmp;
	gettimeofday (&tmp, NULL);
	return (gint64)tmp.tv_usec + (((gint64)tmp.tv_sec) << (8 * sizeof (long)));
}

static void
init_progress (GnomeVFSProgressCallbackState *progress_state,
	       GnomeVFSXferProgressInfo *progress_info)
{
	progress_info->source_name = NULL;
	progress_info->target_name = NULL;
	progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;
	progress_info->vfs_status = GNOME_VFS_OK;
	progress_info->phase = GNOME_VFS_XFER_PHASE_INITIAL;
	progress_info->source_name = NULL;
	progress_info->target_name = NULL;
	progress_info->file_index = 1;
	progress_info->files_total = 0;
	progress_info->bytes_total = 0;
	progress_info->file_size = 0;
	progress_info->bytes_copied = 0;
	progress_info->total_bytes_copied = 0;
	progress_info->duplicate_name = 0;

	progress_state->progress_info = progress_info;
	progress_state->sync_callback = NULL;
	progress_state->update_callback = NULL;
	progress_state->async_job_data = NULL;
	progress_state->next_update_callback_time = 0LL;
	progress_state->next_text_update_callback_time = 0LL;
	progress_state->update_callback_period = UPDATE_PERIOD;

}

static void
free_progress (GnomeVFSXferProgressInfo *progress_info)
{
	g_free (progress_info->source_name);
	progress_info->source_name = NULL;
	g_free (progress_info->target_name);
	progress_info->target_name = NULL;
}

static void
progress_set_source_target_names (GnomeVFSProgressCallbackState *progress, 
	      char *source_uri, char *dest_uri)
{
	g_free (progress->progress_info->source_name);
	progress->progress_info->source_name = g_strdup (source_uri);
	g_free (progress->progress_info->target_name);
	progress->progress_info->target_name = g_strdup (dest_uri);
}

static void
progress_set_source_target_uris (GnomeVFSProgressCallbackState *progress, 
	      const GnomeVFSURI *source_uri, const GnomeVFSURI *dest_uri)
{
	g_free (progress->progress_info->source_name);
	progress->progress_info->source_name = source_uri ? gnome_vfs_uri_to_string (source_uri,
						       GNOME_VFS_URI_HIDE_PASSWORD) : NULL;
	g_free (progress->progress_info->target_name);
	progress->progress_info->target_name = dest_uri ? gnome_vfs_uri_to_string (dest_uri,
						       GNOME_VFS_URI_HIDE_PASSWORD) : NULL;

}

static int
call_progress (GnomeVFSProgressCallbackState *progress, GnomeVFSXferPhase phase)
{
	int result;

	/* FIXME: should use proper progress result values from an enum here */

	result = 0;
	progress_set_source_target_uris (progress, NULL, NULL);

	progress->next_update_callback_time = system_time () + progress->update_callback_period;;
	
	progress->progress_info->phase = phase;

	if (progress->sync_callback != NULL)
		result = (* progress->sync_callback) (progress->progress_info, progress->user_data);

	if (progress->update_callback != NULL)
		result = (* progress->update_callback) (progress->progress_info, progress->async_job_data);

	return result;	
}

static int
call_progress_uri (GnomeVFSProgressCallbackState *progress, 
		   const GnomeVFSURI *source_uri, const GnomeVFSURI *dest_uri, 
		   GnomeVFSXferPhase phase)
{
	int result;

	result = 0;
	progress_set_source_target_uris (progress, source_uri, dest_uri);

	progress->next_text_update_callback_time = system_time () + progress->update_callback_period;;
	progress->next_update_callback_time = progress->next_text_update_callback_time;
	
	progress->progress_info->phase = phase;

	if (progress->sync_callback != NULL)
		result = (* progress->sync_callback) (progress->progress_info, progress->user_data);

	if (progress->update_callback != NULL)
		result = (* progress->update_callback) (progress->progress_info, progress->async_job_data);

	return result;	
}

static int
call_progress_often (GnomeVFSProgressCallbackState *progress, GnomeVFSXferPhase phase)
{
	int result;
	gint64 now;

	result = 1;
	now = system_time ();

	progress->progress_info->phase = phase;

	if (progress->sync_callback != NULL)
		result = (* progress->sync_callback) (progress->progress_info, progress->user_data);

	if (now < progress->next_update_callback_time)
		return result;

	progress->next_update_callback_time = now + progress->update_callback_period;
	if (progress->update_callback != NULL)
		result = (* progress->update_callback) (progress->progress_info, progress->async_job_data);

	return result;
}

static int
call_progress_with_uris_often (GnomeVFSProgressCallbackState *progress, 
		   const GnomeVFSURI *source_uri, const GnomeVFSURI *dest_uri, 
		   GnomeVFSXferPhase phase)
{
	int result;
	gint64 now;

	progress_set_source_target_uris (progress, source_uri, dest_uri);
	result = 1;
	now = system_time ();

	progress->progress_info->phase = phase;

	if (progress->sync_callback != NULL)
		result = (* progress->sync_callback) (progress->progress_info, progress->user_data);

	if (now < progress->next_text_update_callback_time)
		return result;

	progress->next_text_update_callback_time = now + progress->update_callback_period;
	
	if (progress->update_callback != NULL)
		result = (* progress->update_callback) (progress->progress_info, progress->async_job_data);

	return result;
}

/* Handle an error condition according to `error_mode'.  Returns `TRUE' if the
   operation should be retried, `FALSE' otherwise.  `*result' will be set to
   the result value we want to be returned to the caller of the xfer
   function.  */
static gboolean
handle_error (GnomeVFSResult *result,
	      GnomeVFSProgressCallbackState *progress,
	      GnomeVFSXferErrorMode *error_mode,
	      gboolean *skip)
{
	GnomeVFSXferErrorAction action;

	*skip = FALSE;

	switch (*error_mode) {
	case GNOME_VFS_XFER_ERROR_MODE_ABORT:
		return FALSE;

	case GNOME_VFS_XFER_ERROR_MODE_QUERY:
		progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR;
		progress->progress_info->vfs_status = *result;
		action = call_progress (progress, progress->progress_info->phase);
		progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;

		switch (action) {
		case GNOME_VFS_XFER_ERROR_ACTION_RETRY:
			return TRUE;
		case GNOME_VFS_XFER_ERROR_ACTION_ABORT:
			*result = GNOME_VFS_ERROR_INTERRUPTED;
			return FALSE;
		case GNOME_VFS_XFER_ERROR_ACTION_SKIP:
			*skip = TRUE;
			return FALSE;
		}
		break;
	}

	*skip = FALSE;
	return FALSE;
}

/* This is conceptually similiar to the previous `handle_error()' function, but
   handles the overwrite case.  */
static gboolean
handle_overwrite (GnomeVFSResult *result,
		  GnomeVFSProgressCallbackState *progress,
		  GnomeVFSXferErrorMode *error_mode,
		  GnomeVFSXferOverwriteMode *overwrite_mode,
		  gboolean *replace,
		  gboolean *skip)
{
	GnomeVFSXferOverwriteAction action;

	switch (*overwrite_mode) {
	case GNOME_VFS_XFER_OVERWRITE_MODE_ABORT:
		*replace = FALSE;
		*result = GNOME_VFS_ERROR_FILEEXISTS;
		*skip = FALSE;
		return FALSE;
	case GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE:
		*replace = TRUE;
		*skip = FALSE;
		return TRUE;
	case GNOME_VFS_XFER_OVERWRITE_MODE_SKIP:
		*replace = FALSE;
		*skip = TRUE;
		return FALSE;
	case GNOME_VFS_XFER_OVERWRITE_MODE_QUERY:
		progress->progress_info->vfs_status = *result;
		progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE;
		action = call_progress (progress, progress->progress_info->phase);
		progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;

		switch (action) {
		case GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT:
			*replace = FALSE;
			*result = GNOME_VFS_ERROR_FILEEXISTS;
			*skip = FALSE;
			return FALSE;
		case GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE:
			*replace = TRUE;
			*skip = FALSE;
			return TRUE;
		case GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE_ALL:
			*replace = TRUE;
			*overwrite_mode = GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE;
			*skip = FALSE;
			return TRUE;
		case GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP:
			*replace = FALSE;
			*skip = TRUE;
			return FALSE;
		case GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP_ALL:
			*replace = FALSE;
			*skip = TRUE;
			*overwrite_mode = GNOME_VFS_XFER_OVERWRITE_MODE_SKIP;
			return FALSE;
		}
	}

	*replace = FALSE;
	*skip = FALSE;
	return FALSE;
}

static GnomeVFSResult
remove_file (GnomeVFSURI *uri,
	     GnomeVFSProgressCallbackState *progress,
	     GnomeVFSXferOptions xfer_options,
	     GnomeVFSXferErrorMode *error_mode,
	     GnomeVFSXferOverwriteMode *overwrite_mode,
	     gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;

	do {
		retry = FALSE;

		result = gnome_vfs_unlink_from_uri (uri);
		if (result != GNOME_VFS_OK)
			retry = handle_error (&result, progress,
					      error_mode, skip);
	} while (retry);

	return result;
}

static GnomeVFSResult
remove_directory (GnomeVFSURI *uri,
		  gboolean recursive,
		  GnomeVFSProgressCallbackState *progress,
		  GnomeVFSXferOptions xfer_options,
		  GnomeVFSXferErrorMode *error_mode,
		  GnomeVFSXferOverwriteMode *overwrite_mode,
		  gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;

	if (recursive) {
		GnomeVFSDirectoryHandle *directory_handle;
		result = gnome_vfs_directory_open_from_uri (&directory_handle, uri, 
			GNOME_VFS_FILE_INFO_DEFAULT, NULL, NULL);

		if (result != GNOME_VFS_OK)
			return result;
			
		for (;;) {
			GnomeVFSFileInfo info;
			GnomeVFSURI *item_uri;
			
			gnome_vfs_file_info_init (&info);
			result = gnome_vfs_directory_read_next (directory_handle, &info);
			if (result != GNOME_VFS_OK)
				break;

			/* Skip "." and "..".  */
			if (strcmp (info.name, ".") == 0 
			    || strcmp (info.name, "..") == 0) {
				continue;
			}

			item_uri = gnome_vfs_uri_append_path (uri, info.name);
			
			if (info.type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				result = remove_directory (item_uri, recursive, 
						    progress, xfer_options, error_mode, 
						    overwrite_mode, skip);
			} else {
				result = remove_file (item_uri, progress, 
						    xfer_options, error_mode, overwrite_mode, 
						    skip);
			}

			gnome_vfs_uri_unref (item_uri);

			if (call_progress_often (progress, GNOME_VFS_XFER_PHASE_DELETESOURCE) 
				== GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT) {
				result = GNOME_VFS_ERROR_INTERRUPTED;
			}

			if (result != GNOME_VFS_OK)
				break;
		}
		gnome_vfs_directory_close (directory_handle);
	}

	if (result == GNOME_VFS_ERROR_EOF)
		result = GNOME_VFS_OK;

	if (result == GNOME_VFS_OK) {
		do {
			retry = FALSE;

			result = gnome_vfs_remove_directory_from_uri (uri);
			if (result != GNOME_VFS_OK)
				retry = handle_error (&result, progress,
						      error_mode, skip);
		} while (retry);
	}

	return result;
}

/* iterates the list of names in a given directory, applies @callback on each,
 * optionaly recurses into directories
 */
static GnomeVFSResult
gnome_vfs_visit_list (const GnomeVFSURI *dir_uri,
		      const GList *name_list,
		      GnomeVFSFileInfoOptions info_options,
		      GnomeVFSDirectoryVisitOptions visit_options,
		      gboolean recursive,
		      GnomeVFSDirectoryVisitFunc callback,
		      gpointer data)
{
	GnomeVFSResult result;
	const GList *p;
	
	result = GNOME_VFS_OK;

	/* go through our list of items */
	for (p = name_list; p != NULL; p = p->next) {
		GnomeVFSURI *uri;
		GnomeVFSFileInfo info;
		
		/* get the URI and VFSFileInfo for each */
		uri = gnome_vfs_uri_append_path (dir_uri, p->data);
		gnome_vfs_file_info_init (&info);
		result = gnome_vfs_get_file_info_uri (uri, &info, info_options, NULL);
		
		if (result == GNOME_VFS_OK) {
			gboolean tmp_recurse;
			
			tmp_recurse = TRUE;
			
			/* call our callback on each item*/
			if (!callback (p->data, &info, FALSE, data, &tmp_recurse))
				result = GNOME_VFS_ERROR_INTERRUPTED;
			
			if (result == GNOME_VFS_OK
			    && recursive
			    && info.type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				/* let gnome_vfs_directory_visit_uri call our callback 
				 * recursively 
				 */
				result = gnome_vfs_directory_visit_uri
						(uri, info_options, NULL, NULL, visit_options,
						 callback, data);
			}
		}

		gnome_vfs_uri_unref (uri);
		
		if (result != GNOME_VFS_OK)
			break;
	}
	return result;
}

typedef struct CountEachFileSizeParams {
	GnomeVFSProgressCallbackState *progress;
	GnomeVFSResult result;
} CountEachFileSizeParams;

/* iteratee for count_items_and_size */
static gboolean
count_each_file_size_one (const gchar *rel_path,
			  GnomeVFSFileInfo *info,
			  gboolean recursing_will_loop,
			  gpointer data,
			  gboolean *recurse)
{
	CountEachFileSizeParams *params;

	params = (CountEachFileSizeParams *)data;

	if (call_progress_often (params->progress, params->progress->progress_info->phase) == 0) {
		/* progress callback requested to stop */
		params->result = GNOME_VFS_ERROR_INTERRUPTED;
		*recurse = FALSE;
		return FALSE;
	}

	/* count each file, folder, symlink */
	params->progress->progress_info->files_total++;
	if (info->type == GNOME_VFS_FILE_TYPE_REGULAR) {
		/* add each file size */
		params->progress->progress_info->bytes_total += info->size;
	} else if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
		/* add some small size for each directory */
		params->progress->progress_info->bytes_total += DEFAULT_SIZE_OVERHEAD;
	}

	/* watch out for infinite recursion*/
	if (recursing_will_loop) {
		params->result = GNOME_VFS_ERROR_LOOP;
		return FALSE;
	}

	*recurse = TRUE;

	return TRUE;
}

/* calculate the number of items and their total size; used as a preflight
 * before the transfer operation starts
 */
static GnomeVFSResult
count_items_and_size (const GnomeVFSURI *dir_uri,
		      const GList *name_list,
		      GnomeVFSXferOptions xfer_options,
		      GnomeVFSProgressCallbackState *progress,
		      gboolean move)
{
	GnomeVFSFileInfoOptions info_options;
	GnomeVFSDirectoryVisitOptions visit_options;
	CountEachFileSizeParams each_params;

	/* initialize the results */
	progress->progress_info->files_total = 0;
	progress->progress_info->bytes_total = 0;

	/* set up the params for recursion */
	visit_options = GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK;
	if (xfer_options & GNOME_VFS_XFER_SAMEFS)
		visit_options |= GNOME_VFS_DIRECTORY_VISIT_SAMEFS;
	each_params.progress = progress;
	each_params.result = GNOME_VFS_OK;

	if (xfer_options & GNOME_VFS_XFER_FOLLOWLINKS) {
		info_options = GNOME_VFS_FILE_INFO_FOLLOWLINKS;
	} else {
		info_options = GNOME_VFS_FILE_INFO_DEFAULT;
	}

	return gnome_vfs_visit_list (dir_uri, name_list, info_options,
		visit_options, !move && (xfer_options & GNOME_VFS_XFER_RECURSIVE) != 0,
		count_each_file_size_one, &each_params);
}

/* Compares the list of files about to be created by a transfer with
 * any possible existing files with conflicting names in the target directory.
 * Handles conflicts, optionaly removing the conflicting file/directory
 */
static GnomeVFSResult
handle_name_conflicts (const GnomeVFSURI *source_dir_uri,
		       GList **source_name_list,
		       GnomeVFSURI *target_dir_uri,
		       GList **target_name_list,
		       GnomeVFSXferOptions xfer_options,
		       GnomeVFSXferErrorMode *error_mode,
		       GnomeVFSXferOverwriteMode *overwrite_mode,
		       GnomeVFSProgressCallbackState *progress)
{
	GnomeVFSResult result;
	GList *source_item;
	GList *target_item;
	
	result = GNOME_VFS_OK;
	
	/* go through the list of names */
	for (target_item = *target_name_list, source_item = *source_name_list; 
	     target_item != NULL;) {
		GnomeVFSURI *uri;
		GnomeVFSFileInfo info;
		gboolean replace;
		gboolean skip;
		
		skip = FALSE;

		/* get the URI and VFSFileInfo for each */
		uri = gnome_vfs_uri_append_path (target_dir_uri, target_item->data);
		gnome_vfs_file_info_init (&info);
		result = gnome_vfs_get_file_info_uri (uri, &info, GNOME_VFS_FILE_INFO_DEFAULT, NULL);
		
		if (result == GNOME_VFS_OK) {
			/* FIXME:
			 * use a better way to tell if a file exists here
			 */

			progress_set_source_target_uris (progress, NULL, uri);
			 
			/* no error getting info -- file exists, ask what to do */
			replace = handle_overwrite (&result, progress, error_mode,
						  overwrite_mode, &replace, &skip);
			
			/* FIXME:
			 * move items to Trash here
			 */

			/* get rid of the conflicting file */
			if (replace) {
				if (info.type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
					remove_directory (uri, TRUE, progress, xfer_options, error_mode,
							  overwrite_mode, &skip);
				} else {
					remove_file (uri, progress, xfer_options, error_mode,
						     overwrite_mode, &skip);
				}
			}
		} else {
			/* no conflict, carry on */
			result = GNOME_VFS_OK;
		}

		gnome_vfs_uri_unref (uri);
		
		if (result != GNOME_VFS_OK) {
			break;
		}

		if (skip) {
			/* skipping a file, remove it's name from the source and target name
			 * lists.
			 */
			GList *source_item_to_remove;
			GList *target_item_to_remove;

			source_item_to_remove = source_item;
			target_item_to_remove = target_item;
			
			source_item = source_item->next;
			target_item = target_item->next;

			*source_name_list = g_list_remove_link (*source_name_list, source_item_to_remove);
			*target_name_list = g_list_remove_link (*target_name_list, target_item_to_remove);
			
			continue;
		}


		target_item = target_item->next; 
		source_item = source_item->next;
	}

	return result;
}

/* Create new directory. If GNOME_VFS_XFER_USE_UNIQUE_NAMES is set, 
 * return with an error if a name conflict occurs, else
 * handle the overwrite.
 * On success, opens the new directory
 */
static GnomeVFSResult
create_directory (GnomeVFSURI *dir_uri,
		  GnomeVFSDirectoryHandle **return_handle,
		  GnomeVFSXferOptions xfer_options,
		  GnomeVFSXferErrorMode *error_mode,
		  GnomeVFSXferOverwriteMode *overwrite_mode,
		  GnomeVFSProgressCallbackState *progress,
		  gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;
	
	*skip = FALSE;
	do {
		retry = FALSE;

		result = gnome_vfs_make_directory_for_uri (dir_uri, 0700);

		if (result == GNOME_VFS_ERROR_FILEEXISTS) {
			gboolean force_replace;

			if ((xfer_options & GNOME_VFS_XFER_USE_UNIQUE_NAMES) != 0) {
				/* just let the caller pass a unique name*/
				return result;
			}

			retry = handle_overwrite (&result,
						  progress,
						  error_mode,
						  overwrite_mode,
						  &force_replace,
						  skip);

			if (*skip) {
				return GNOME_VFS_OK;
			}
			if (force_replace) {
				result = remove_directory (dir_uri, TRUE, progress, 
							xfer_options, error_mode, overwrite_mode, 
							skip);
			} else {
				result = GNOME_VFS_OK;
			}
		}

		if (result == GNOME_VFS_OK) {
			return gnome_vfs_directory_open_from_uri (return_handle, 
								  dir_uri, 
								  GNOME_VFS_FILE_INFO_DEFAULT, 
								  NULL, NULL);
		}
		/* handle the error case */
		retry = handle_error (&result, progress,
				      error_mode, skip);

		if (*skip) {
			return GNOME_VFS_OK;
		}

	} while (retry);

	return result;
}

/* Copy the data of a single file. */
static GnomeVFSResult
copy_file_data (GnomeVFSHandle *target_handle,
		GnomeVFSHandle *source_handle,
		GnomeVFSProgressCallbackState *progress,
		GnomeVFSXferOptions xfer_options,
		GnomeVFSXferErrorMode *error_mode,
		guint block_size,
		gboolean *skip)
{
	GnomeVFSResult result;
	gpointer buffer;
	
	*skip = FALSE;

	buffer = alloca (block_size);

	if (call_progress_often (progress, GNOME_VFS_XFER_PHASE_COPYING) == 0)
		return GNOME_VFS_ERROR_INTERRUPTED;

	do {
		GnomeVFSFileSize bytes_read;
		GnomeVFSFileSize bytes_to_write;
		GnomeVFSFileSize bytes_written;
		gboolean retry;

		progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;
		progress->progress_info->vfs_status = GNOME_VFS_OK;

		progress->progress_info->phase = GNOME_VFS_XFER_PHASE_READSOURCE;

		do {
			retry = FALSE;

			result = gnome_vfs_read (source_handle, buffer,
						 block_size, &bytes_read);
			if (result != GNOME_VFS_OK)
				retry = handle_error (&result, progress,
						      error_mode, skip);
		} while (retry && bytes_read > 0);

		if (result != GNOME_VFS_OK || bytes_read == 0 || *skip)
			break;

		bytes_to_write = bytes_read;

		progress->progress_info->phase = GNOME_VFS_XFER_PHASE_WRITETARGET;

		do {
			retry = FALSE;

			result = gnome_vfs_write (target_handle, buffer,
						  bytes_to_write,
						  &bytes_written);
			if (result != GNOME_VFS_OK)
				retry = handle_error (&result, progress,
						      error_mode, skip);

			bytes_to_write -= bytes_written;
		} while (retry || bytes_to_write > 0);

		progress->progress_info->phase = GNOME_VFS_XFER_PHASE_COPYING;
		progress->progress_info->bytes_copied += bytes_read;
		progress->progress_info->total_bytes_copied += bytes_read;


		if (call_progress_often (progress, GNOME_VFS_XFER_PHASE_COPYING) == 0)
			return GNOME_VFS_ERROR_INTERRUPTED;


		if (*skip) {
			break;
		}

	} while (result == GNOME_VFS_OK);


	if (result == GNOME_VFS_ERROR_EOF)
		return GNOME_VFS_OK;

	return result;
}

static GnomeVFSResult
xfer_open_source (GnomeVFSHandle **source_handle,
		  GnomeVFSURI *source_uri,
		  GnomeVFSProgressCallbackState *progress,
		  GnomeVFSXferOptions xfer_options,
		  GnomeVFSXferErrorMode *error_mode,
		  gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;

	*skip = FALSE;
	do {
		retry = FALSE;

		result = gnome_vfs_open_uri (source_handle, source_uri,
					     GNOME_VFS_OPEN_READ);
		if (result != GNOME_VFS_OK)
			retry = handle_error (&result, progress,
					      error_mode, skip);
	} while (retry);

	return result;
}

static GnomeVFSResult
xfer_create_target (GnomeVFSHandle **target_handle,
		    GnomeVFSURI *target_uri,
		    GnomeVFSProgressCallbackState *progress,
		    GnomeVFSXferOptions xfer_options,
		    GnomeVFSXferErrorMode *error_mode,
		    GnomeVFSXferOverwriteMode *overwrite_mode,
		    gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;
	gboolean exclusive;

	if (*overwrite_mode == GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE)
		exclusive = FALSE;
	else
		exclusive = TRUE;

	*skip = FALSE;
	do {
		retry = FALSE;

		result = gnome_vfs_create_uri (target_handle, target_uri,
					       GNOME_VFS_OPEN_WRITE,
					       exclusive, 0600);

		if (result == GNOME_VFS_ERROR_FILEEXISTS) {
			gboolean replace;

			retry = handle_overwrite (&result,
						  progress,
						  error_mode,
						  overwrite_mode,
						  &replace,
						  skip);

			if (replace)
				exclusive = FALSE;
		} else if (result != GNOME_VFS_OK) {
			retry = handle_error (&result,
					      progress,
					      error_mode,
					      skip);
		} else {
			retry = FALSE;
		}
	} while (retry);

	return result;
}

static GnomeVFSResult
copy_file (GnomeVFSFileInfo *info,  
	   GnomeVFSURI *source_uri,
	   GnomeVFSURI *target_uri,
	   GnomeVFSXferOptions xfer_options,
	   GnomeVFSXferErrorMode *error_mode,
	   GnomeVFSXferOverwriteMode *overwrite_mode,
	   GnomeVFSProgressCallbackState *progress,
	   gboolean *skip)
{
	GnomeVFSResult result;
	GnomeVFSHandle *source_handle, *target_handle;

	progress->progress_info->phase = GNOME_VFS_XFER_PHASE_OPENSOURCE;
	progress->progress_info->bytes_copied = 0;
	result = xfer_open_source (&source_handle, source_uri,
				   progress, xfer_options,
				   error_mode, skip);
	if (*skip)
		return GNOME_VFS_OK;
	if (result != GNOME_VFS_OK)
		return result;

	progress->progress_info->phase = GNOME_VFS_XFER_PHASE_OPENTARGET;
	result = xfer_create_target (&target_handle, target_uri,
				     progress, xfer_options,
				     error_mode, overwrite_mode,
				     skip);


	if (*skip) {
		gnome_vfs_close (source_handle);
		return GNOME_VFS_OK;
	}
	if (result != GNOME_VFS_OK) {
		gnome_vfs_close (source_handle);
		return result;
	}

	if (call_progress_with_uris_often (progress, 
			       source_uri, target_uri, 
			       GNOME_VFS_XFER_PHASE_OPENTARGET) 
			       != GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT) {


		result = copy_file_data (target_handle, source_handle,
					progress, xfer_options, error_mode,
					info->io_block_size, skip);
	}

	progress->progress_info->file_index++;
	if (call_progress_often (progress, GNOME_VFS_XFER_PHASE_CLOSETARGET) == 0) 
		result = GNOME_VFS_ERROR_INTERRUPTED;

	/* FIXME: Check errors here.  */
	gnome_vfs_close (source_handle);
	gnome_vfs_close (target_handle);

	if (*skip)
		return GNOME_VFS_OK;

	return result;
}

static GnomeVFSResult
copy_directory (GnomeVFSURI *source_dir_uri,
		GnomeVFSURI *target_dir_uri,
		GnomeVFSXferOptions xfer_options,
		GnomeVFSXferErrorMode *error_mode,
		GnomeVFSXferOverwriteMode *overwrite_mode,
		GnomeVFSProgressCallbackState *progress,
		gboolean *skip)
{
	GnomeVFSResult result;
	GnomeVFSDirectoryHandle *source_directory_handle;
	GnomeVFSDirectoryHandle *dest_directory_handle;

	source_directory_handle = NULL;
	dest_directory_handle = NULL;
	
	result = gnome_vfs_directory_open_from_uri (&source_directory_handle, source_dir_uri, 
			GNOME_VFS_FILE_INFO_DEFAULT, NULL, NULL);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	progress->progress_info->bytes_copied = 0;
	/* FIXME:
	 * use a better progress phase here??
	 */
	if (call_progress_with_uris_often (progress, 
			       source_dir_uri, target_dir_uri, 
			       GNOME_VFS_XFER_PHASE_COPYING) 
		== GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT) {
		return GNOME_VFS_ERROR_INTERRUPTED;
	}

	result = create_directory (target_dir_uri, 
				   &dest_directory_handle,
				   xfer_options,
				   error_mode,
				   overwrite_mode,
				   progress,
				   skip);

	progress->progress_info->file_index++;
	progress->progress_info->total_bytes_copied += DEFAULT_SIZE_OVERHEAD;

	/* We do not deal with symlink loops here. 
	 * That's OK because we don't follow symlinks.
	 */
	if (!*skip && result == GNOME_VFS_OK) {
		for (;;) {
			GnomeVFSURI *source_uri;
			GnomeVFSURI *dest_uri;
			GnomeVFSFileInfo info;

			gnome_vfs_file_info_init (&info);

			result = gnome_vfs_directory_read_next (source_directory_handle, &info);
			if (result != GNOME_VFS_OK) {
				break;
			}
			
			/* Skip "." and "..".  */
			if (strcmp (info.name, ".") == 0 
			    || strcmp (info.name, "..") == 0) {
				continue;
			}

			source_uri = gnome_vfs_uri_append_path (source_dir_uri, info.name);
			dest_uri = gnome_vfs_uri_append_path (target_dir_uri, info.name);
			
			if (info.type == GNOME_VFS_FILE_TYPE_REGULAR) {
				result = copy_file (&info, source_uri, dest_uri, 
						    xfer_options, error_mode, overwrite_mode, 
						    progress, skip);
			} else if (info.type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				result = copy_directory (source_uri, dest_uri, 
						    xfer_options, error_mode, overwrite_mode, 
						    progress, skip);
			} else {
				/* FIXME */
				g_assert (!"unimplemented");
			}

			gnome_vfs_uri_unref (dest_uri);
			gnome_vfs_uri_unref (source_uri);

			if (result != GNOME_VFS_OK) {
				result = GNOME_VFS_OK;
			}
		}
	}

	if (result == GNOME_VFS_ERROR_EOF)
		/* all is well, we just finished iterating the directory */
		result = GNOME_VFS_OK;

	if (dest_directory_handle != NULL)
		gnome_vfs_directory_close (dest_directory_handle);
	if (source_directory_handle != NULL)
		gnome_vfs_directory_close (source_directory_handle);

	return result;
}
		
static GnomeVFSResult
copy_items (const GnomeVFSURI *source_dir_uri,
	    const GList *source_name_list,
	    GnomeVFSURI *target_dir_uri,
	    const GList *target_name_list,
	    GnomeVFSXferOptions xfer_options,
	    GnomeVFSXferErrorMode *error_mode,
	    GnomeVFSXferOverwriteMode *overwrite_mode,
	    GnomeVFSProgressCallbackState *progress)
{
	GnomeVFSResult result;
	const GList *source_name_item;
	const GList *dest_name_item;
	
	result = GNOME_VFS_OK;

	/* go through the list of names */
	for (source_name_item = source_name_list, dest_name_item = target_name_list; 
		source_name_item != NULL;) {

		GnomeVFSURI *source_uri;
		GnomeVFSURI *target_uri;
		GnomeVFSFileInfo info;
		gboolean skip;
		int count;
		int progress_result;

		skip = FALSE;
		target_uri = NULL;

		/* get source URI and file info */
		source_uri = gnome_vfs_uri_append_path (source_dir_uri, source_name_item->data);
		gnome_vfs_file_info_init (&info);
		result = gnome_vfs_get_file_info_uri (source_uri, &info, 
						      GNOME_VFS_FILE_INFO_DEFAULT, NULL);

		progress->progress_info->duplicate_name = g_strdup (dest_name_item->data);

		if (result == GNOME_VFS_OK) {
			/* optionally keep trying until we hit a unique target name */
			for (count = 1; ; count++) {
				GnomeVFSXferOverwriteMode saved_overwrite_mode;

				target_uri = gnome_vfs_uri_append_path (target_dir_uri, 
					progress->progress_info->duplicate_name);

				progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;
				progress->progress_info->file_size = info.size;
				progress->progress_info->bytes_copied = 0;
				if (call_progress_with_uris_often (progress, 
						       source_uri, target_uri, 
						       GNOME_VFS_XFER_PHASE_COPYING) == 0) {
					result = GNOME_VFS_ERROR_INTERRUPTED;
				}
				/* temporarily set an overwrite such that a conflict
				 * will fail with an error
				 */
				saved_overwrite_mode = *overwrite_mode;
				*overwrite_mode = GNOME_VFS_XFER_OVERWRITE_MODE_ABORT;
				
				if (info.type == GNOME_VFS_FILE_TYPE_REGULAR) {
					result = copy_file (&info, source_uri, target_uri, 
							    xfer_options, error_mode, overwrite_mode, 
							    progress, &skip);
				} else if (info.type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
					result = copy_directory (source_uri, target_uri, 
							    xfer_options, error_mode,
							    overwrite_mode, progress, &skip);
				} else {
					/* FIXME */
					g_assert (!"unimplemented");
				}

				
				/* restore overwrite mode */
				*overwrite_mode = saved_overwrite_mode;

				if (result != GNOME_VFS_ERROR_FILEEXISTS)
					/* whatever happened, it wasn't a name conflict */
					break;

				if (*overwrite_mode != GNOME_VFS_XFER_OVERWRITE_MODE_QUERY
				    || (xfer_options & GNOME_VFS_XFER_USE_UNIQUE_NAMES) == 0)
					break;

				/* pass in the current target name, progress will update it to 
				 * a new unique name such as 'foo (copy)' or 'bar (copy 2)'
				 */
				g_free (progress->progress_info->duplicate_name);
				progress->progress_info->duplicate_name = g_strdup (dest_name_item->data);
				progress->progress_info->duplicate_count = count;
				progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_DUPLICATE;
				progress->progress_info->vfs_status = result;
				progress_result = call_progress_uri (progress, source_uri, target_uri, 
						       GNOME_VFS_XFER_PHASE_COPYING);
				progress->progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;

				if (progress_result == GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT) {
					break;
				}

				if (skip) {
					break;
				}
				
				/* try again with new uri */
				gnome_vfs_uri_unref (target_uri);

			}
		}

		gnome_vfs_uri_unref (target_uri);
		gnome_vfs_uri_unref (source_uri);
		g_free (progress->progress_info->duplicate_name);

		if (result != GNOME_VFS_OK) {
			break;
		}

		source_name_item = source_name_item->next;
		dest_name_item = dest_name_item->next;

		g_assert ((source_name_item != NULL) == (dest_name_item != NULL));
	}

	return result;
}

static GnomeVFSResult
move_items (const GnomeVFSURI *source_dir_uri,
	    const GList *source_name_list,
	    GnomeVFSURI *target_dir_uri,
	    const GList *target_name_list,
	    GnomeVFSXferOptions xfer_options,
	    GnomeVFSXferErrorMode *error_mode,
	    GnomeVFSXferOverwriteMode *overwrite_mode,
	    GnomeVFSProgressCallbackState *progress)
{
	GnomeVFSResult result;
	const GList *source_name_item;
	const GList *dest_name_item;
	
	result = GNOME_VFS_OK;

	/* go through the list of names, move each item */
	for (source_name_item = source_name_list, dest_name_item = target_name_list; 
		source_name_item != NULL;) {

		GnomeVFSURI *source_uri;
		GnomeVFSURI *target_uri;
		gboolean retry;
		gboolean skip;

		source_uri = gnome_vfs_uri_append_path (source_dir_uri, source_name_item->data);
		target_uri = gnome_vfs_uri_append_path (target_dir_uri, dest_name_item->data);

		retry = FALSE;
		skip = FALSE;
		
		do {
			result = GNOME_VFS_OK;
			progress->progress_info->file_size = DEFAULT_SIZE_OVERHEAD;
			progress->progress_info->bytes_copied = 0;
			if (call_progress_with_uris_often (progress, source_uri,
						target_uri, GNOME_VFS_XFER_PHASE_MOVING) == 0) {
				result = GNOME_VFS_ERROR_INTERRUPTED;
			}

			if (result == GNOME_VFS_OK) {
				/* no matter what the replace mode, just overwrite the destination
				 * handle_name_conflicts took care of conflicting files
				 */
				result = gnome_vfs_move_uri (source_uri, target_uri, 
							     GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE);
			}

			if (result != GNOME_VFS_OK) {
				retry = handle_error (&result, progress,
						      error_mode, &skip);
			}
		} while (retry);
		
		gnome_vfs_uri_unref (target_uri);
		gnome_vfs_uri_unref (source_uri);

		if (result != GNOME_VFS_OK && !skip)
			break;

		source_name_item = source_name_item->next;
		dest_name_item = dest_name_item->next;
		g_assert ((source_name_item != NULL) == (dest_name_item != NULL));
	}

	return result;
}

static GnomeVFSResult
gnome_vfs_xfer_uri_internal (GnomeVFSURI *source_dir_uri,
			    const GList *source_names,
			    GnomeVFSURI *target_dir_uri,
			    const GList *target_names,
			    GnomeVFSXferOptions xfer_options,
			    GnomeVFSXferErrorMode error_mode,
			    GnomeVFSXferOverwriteMode overwrite_mode,
			    GnomeVFSProgressCallbackState *progress)
{
	GnomeVFSResult result;
	GList *source_name_list;
	GList *target_name_list;
	gboolean move;

	result = GNOME_VFS_OK;
	move = FALSE;

	/* Create an owning list of source and destination names.
	 * We want to be able to remove items that we decide to skip during
	 * name conflict check.
	 */
	source_name_list = g_string_list_deep_copy (source_names);
	target_name_list = g_string_list_deep_copy (target_names);
	if (target_name_list == NULL)
		target_name_list = g_string_list_deep_copy (source_names);

	/* FIXME:
	 * check if destination is writable
	 * and bail if not
	 */
	move = (xfer_options & GNOME_VFS_XFER_REMOVESOURCE) != 0;

	if ((xfer_options & GNOME_VFS_XFER_USE_UNIQUE_NAMES) == 0) {
		gboolean same_fs;
		result = gnome_vfs_check_same_fs_uris (source_dir_uri, target_dir_uri,
							&same_fs);
		move &= same_fs;
	}

	if (result == GNOME_VFS_OK) {

		call_progress (progress, GNOME_VFS_XFER_PHASE_INITIAL);

		progress->progress_info->phase = GNOME_VFS_XFER_PHASE_COLLECTING;
		result = count_items_and_size (source_dir_uri, source_name_list,
					       xfer_options, progress, move);
		if (result == GNOME_VFS_OK) {

			/* FIXME:
			 * check if destination has enough space
			 * and bail if not
			 */

			if ((xfer_options & GNOME_VFS_XFER_USE_UNIQUE_NAMES) == 0) {
				result = handle_name_conflicts (source_dir_uri, &source_name_list,
								target_dir_uri, &target_name_list,
							        xfer_options, &error_mode, &overwrite_mode,
							        progress);
			}

			if (result == GNOME_VFS_OK) {


				call_progress (progress, GNOME_VFS_XFER_PHASE_READYTOGO);

				if (move) {
					result = move_items (source_dir_uri, source_name_list,
							     target_dir_uri, target_name_list,
							     xfer_options, &error_mode, &overwrite_mode, progress);
				} else {
					result = copy_items (source_dir_uri, source_name_list,
							     target_dir_uri, target_name_list,
							     xfer_options, &error_mode, &overwrite_mode, progress);
				}
				
				if (result == GNOME_VFS_OK) {

					if (!move && (xfer_options & GNOME_VFS_XFER_REMOVESOURCE)) {
						/* FIXME:
						 */
					}
				}
			}
		}
	}
	
	/* Done, at last.  At this point, there is no chance to interrupt the
           operation anymore so we don't check the return value.  */
	call_progress (progress, GNOME_VFS_XFER_PHASE_COMPLETED);

	free_progress (progress->progress_info);
	g_list_deep_free (source_name_list);
	g_list_deep_free (target_name_list);

	return result;
}

GnomeVFSResult
gnome_vfs_xfer_uri (GnomeVFSURI *source_dir_uri,
		    const GList *source_names,
		    GnomeVFSURI *target_dir_uri,
		    const GList *target_names,
		    GnomeVFSXferOptions xfer_options,
		    GnomeVFSXferErrorMode error_mode,
		    GnomeVFSXferOverwriteMode overwrite_mode,
		    GnomeVFSXferProgressCallback progress_callback,
		    gpointer data)
{
	GnomeVFSProgressCallbackState progress_state;
	GnomeVFSXferProgressInfo progress_info;
	GnomeVFSResult result;

	g_return_val_if_fail (source_dir_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_names != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (target_dir_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);		
	
	
	init_progress (&progress_state, &progress_info);
	progress_state.sync_callback = progress_callback;
	progress_state.user_data = data;

	result = gnome_vfs_xfer_uri_internal (source_dir_uri,
					      source_names,
					      target_dir_uri,
					      target_names,
					      xfer_options,
					      error_mode,
					      overwrite_mode,
					      &progress_state);
	
	return result;
}


GnomeVFSResult
gnome_vfs_xfer_private (const gchar *source_dir,
			const GList *source_name_list,
			const gchar *target_dir,
			const GList *target_name_list,
			GnomeVFSXferOptions xfer_options,
			GnomeVFSXferErrorMode error_mode,
			GnomeVFSXferOverwriteMode overwrite_mode,
			GnomeVFSXferProgressCallback progress_callback,
			gpointer data,
			GnomeVFSXferProgressCallback sync_progress_callback,
			gpointer sync_progress_data)
{
	GnomeVFSProgressCallbackState progress_state;
	GnomeVFSXferProgressInfo progress_info;
	GnomeVFSURI *source_dir_uri;
	GnomeVFSURI *target_dir_uri;
	GnomeVFSResult result;
	
	init_progress (&progress_state, &progress_info);
	progress_state.sync_callback = sync_progress_callback;
	progress_state.user_data = sync_progress_data;
	progress_state.update_callback = progress_callback;
	progress_state.async_job_data = data;

	source_dir_uri = gnome_vfs_uri_new (source_dir);
	if (source_dir_uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;
	target_dir_uri = gnome_vfs_uri_new (target_dir);
	if (target_dir_uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_xfer_uri_internal (source_dir_uri,
				     source_name_list,
				     target_dir_uri,
				     target_name_list,
				     xfer_options,
				     error_mode,
				     overwrite_mode,
				     &progress_state);

	gnome_vfs_uri_unref (source_dir_uri);
	gnome_vfs_uri_unref (target_dir_uri);

	return result;
}

GnomeVFSResult
gnome_vfs_xfer (const gchar *source_dir,
		const GList *source_name_list,
		const gchar *target_dir,
		const GList *target_name_list,
		GnomeVFSXferOptions xfer_options,
		GnomeVFSXferErrorMode error_mode,
		GnomeVFSXferOverwriteMode overwrite_mode,
		GnomeVFSXferProgressCallback progress_callback,
		gpointer data)
{
	return gnome_vfs_xfer_private (source_dir, source_name_list, 
				      target_dir, target_name_list,
				      xfer_options, error_mode, overwrite_mode,
				      NULL, NULL, progress_callback, data);
}
