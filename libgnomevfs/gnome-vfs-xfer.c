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

   Author: Ettore Perazzoli <ettore@comm2000.it> */

/* TODO: Check that all the progress_info passed to the callback is
   correct.  */
/* TODO: Check that all the flags passed by address are set at least once by
   functions that are expected to set them.  */
/* TODO: There should be a `context' thingy in the file methods that would
   allow us to set a prefix URI for all the operation.  This way we can
   greatly optimize access to "strange" file systems.  */
/* TODO: Handle all the GnomeVFSXferOptions.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


/* Implementation of file transfers (`gnome_vfs_xfer*()' functions).  */

/* This is an element of the source file list.  It contains the name, relativve
   to the source directory, and the attributes for setting them in the target
   file.  To save some time, the `info' part is not dynamically allocated.  */
struct _Source {
	gchar *name;
	GnomeVFSFileInfo info;
};
typedef struct _Source Source;


static void
init_progress (GnomeVFSXferProgressInfo *progress_info)
{
	progress_info->source_name = NULL;
	progress_info->target_name = NULL;
	progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;
	progress_info->vfs_status = GNOME_VFS_OK;
	progress_info->phase = GNOME_VFS_XFER_PHASE_COLLECTING;
	progress_info->source_name = NULL;
	progress_info->target_name = NULL;
	progress_info->file_index = 1;
	progress_info->files_total = 0;
	progress_info->bytes_total = 0;
	progress_info->file_size = 0;
	progress_info->bytes_copied = 0;
	progress_info->total_bytes_copied = 0;
}

static void
free_progress (GnomeVFSXferProgressInfo *progress_info)
{
	if (progress_info->source_name != NULL) {
		g_free (progress_info->source_name);
		progress_info->source_name = NULL;
	}
	if (progress_info->target_name != NULL) {
		g_free (progress_info->target_name);
		progress_info->target_name = NULL;
	}
}


/* Handle an error condition according to `error_mode'.  Returns `TRUE' if the
   operation should be retried, `FALSE' otherwise.  `*result' will be set to
   the result value we want to be returned to the caller of the xfer
   function.  */
static gboolean
handle_error (GnomeVFSResult *result,
	      GnomeVFSXferProgressInfo *progress_info,
	      GnomeVFSXferErrorMode *error_mode,
	      GnomeVFSXferProgressCallback progress_callback,
	      gpointer data,
	      gboolean *skip)
{
	GnomeVFSXferErrorAction action;

	*skip = FALSE;

	switch (*error_mode) {
	case GNOME_VFS_XFER_ERROR_MODE_ABORT:
		return FALSE;

	case GNOME_VFS_XFER_ERROR_MODE_QUERY:
		g_return_val_if_fail (progress_callback != NULL, FALSE);
		progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR;
		progress_info->vfs_status = *result;
		action = (* progress_callback) (progress_info, data);

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
		  GnomeVFSXferProgressInfo *progress_info,
		  GnomeVFSXferErrorMode *error_mode,
		  GnomeVFSXferOverwriteMode *overwrite_mode,
		  GnomeVFSXferProgressCallback progress_callback,
		  gpointer data,
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
		g_return_val_if_fail (progress_callback != NULL, FALSE);
		progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE;
		progress_info->vfs_status = *result;
		action = (* progress_callback) (progress_info, data);
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
xfer_open_source (GnomeVFSHandle **source_handle,
		  GnomeVFSURI *source_uri,
		  GnomeVFSXferProgressInfo *progress_info,
		  GnomeVFSXferOptions xfer_options,
		  GnomeVFSXferErrorMode *error_mode,
		  GnomeVFSXferProgressCallback progress_callback,
		  gpointer data,
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
			retry = handle_error (&result, progress_info,
					      error_mode, progress_callback,
					      data, skip);
	} while (retry);

	return result;
}

static GnomeVFSResult
xfer_create_target (GnomeVFSHandle **target_handle,
		    GnomeVFSURI *target_uri,
		    GnomeVFSXferProgressInfo *progress_info,
		    GnomeVFSXferOptions xfer_options,
		    GnomeVFSXferErrorMode *error_mode,
		    GnomeVFSXferOverwriteMode *overwrite_mode,
		    GnomeVFSXferProgressCallback progress_callback,
		    gpointer data,
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
						  progress_info,
						  error_mode,
						  overwrite_mode,
						  progress_callback,
						  data,
						  &replace,
						  skip);

			if (replace)
				exclusive = FALSE;
		} else if (result != GNOME_VFS_OK) {
			retry = handle_error (&result,
					      progress_info,
					      error_mode,
					      progress_callback,
					      data,
					      skip);
		} else {
			retry = FALSE;
		}
	} while (retry);

	return result;
}

static GnomeVFSResult
xfer_file (GnomeVFSHandle *target_handle,
	   GnomeVFSHandle *source_handle,
	   GnomeVFSXferProgressInfo *progress_info,
	   GnomeVFSXferOptions xfer_options,
	   GnomeVFSXferErrorMode *error_mode,
	   guint block_size,
	   GnomeVFSXferProgressCallback progress_callback,
	   gpointer data,
	   gboolean *skip)
{
	GnomeVFSResult result;
	gpointer buffer;

	*skip = FALSE;

	buffer = alloca (block_size);

	do {
		GnomeVFSFileSize bytes_read;
		GnomeVFSFileSize bytes_to_write;
		GnomeVFSFileSize bytes_written;
		gboolean retry;

		progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;
		progress_info->vfs_status = GNOME_VFS_OK;

		progress_info->phase = GNOME_VFS_XFER_PHASE_READSOURCE;

		do {
			retry = FALSE;

			result = gnome_vfs_read (source_handle, buffer,
						 block_size, &bytes_read);
			if (result != GNOME_VFS_OK)
				retry = handle_error (&result, progress_info,
						      error_mode,
						      progress_callback, data,
						      skip);
		} while (retry && bytes_read > 0);

		if (result != GNOME_VFS_OK || bytes_read == 0)
			break;

		bytes_to_write = bytes_read;

		progress_info->phase = GNOME_VFS_XFER_PHASE_WRITETARGET;

		do {
			retry = FALSE;

			result = gnome_vfs_write (target_handle, buffer,
						  bytes_to_write,
						  &bytes_written);
			if (result != GNOME_VFS_OK)
				retry = handle_error (&result, progress_info,
						      error_mode,
						      progress_callback, data,
						      skip);

			bytes_to_write -= bytes_written;
		} while (retry || bytes_to_write > 0);

		progress_info->phase = GNOME_VFS_XFER_PHASE_XFERRING;
		progress_info->bytes_copied += bytes_read;
		progress_info->total_bytes_copied += bytes_read;

		if (progress_callback != NULL
		    && ! (* progress_callback) (progress_info, data))
			return GNOME_VFS_ERROR_INTERRUPTED;
	} while (result == GNOME_VFS_OK);

	if (result == GNOME_VFS_ERROR_EOF)
		return GNOME_VFS_OK;

	return result;
}


static void
append_to_xfer_file_list (GList **file_list,
			  GList **file_list_end,
			  Source *source)
{
	if (*file_list_end == NULL) {
		*file_list_end = g_list_alloc ();
		(*file_list_end)->data = source;
		*file_list = *file_list_end;
	} else {
		(*file_list_end)->next = g_list_alloc ();
		(*file_list_end)->next->prev = *file_list_end;
		*file_list_end = (*file_list_end)->next;
		(*file_list_end)->data = source;
	}
}

static void
free_xfer_file_list (GList *file_list)
{
	GList *p;

	for (p = file_list; p != NULL; p = p->next) {
		Source *source;

		source = p->data;
		g_free (source->name);
		gnome_vfs_file_info_clear (&source->info);
	}

	g_list_free (file_list);
}

struct _RecursionInfo {
	gchar             *base_path;
	gulong            *files_total_ptr;
	GnomeVFSFileSize  *bytes_total_ptr;
	GList            **file_list_ptr;
	GList            **file_list_end_ptr;
	GnomeVFSResult     result;
};
typedef struct _RecursionInfo RecursionInfo;

static gboolean
xfer_file_list_recursion_callback (const gchar *rel_path,
				   GnomeVFSFileInfo *info,
				   gboolean recursing_will_loop,
				   gpointer data,
				   gboolean *recurse)
{
	RecursionInfo *recursion_info;
	Source *source;

	recursion_info = data;

	(*recursion_info->files_total_ptr)++;
	if (info->type == GNOME_VFS_FILE_TYPE_REGULAR)
		*recursion_info->bytes_total_ptr += info->size;

	source = g_new (Source, 1);
	source->name = g_strconcat (recursion_info->base_path, rel_path, NULL);
	gnome_vfs_file_info_copy (&source->info, info);

	append_to_xfer_file_list (recursion_info->file_list_ptr,
				  recursion_info->file_list_end_ptr,
				  source);

	if (! recursing_will_loop) {
		*recurse = TRUE;
		return TRUE;
	} else {
		recursion_info->result = GNOME_VFS_ERROR_LOOP;
		return FALSE;
	}
}

/* This creates the list of files to copy, with their attributes
   (GnomeVFSFileInfo).  The files in `source_name_list' are always at the
   beginning, recursion follows.  This way, it's easy to associate the source
   names with the destination names in the main transfer function.  */
static GnomeVFSResult
create_xfer_file_list (GnomeVFSURI *source_dir_uri,
		       const GList *source_name_list,
		       GnomeVFSXferOptions xfer_options,
		       GList **file_list,
		       gulong *files_total,
		       GnomeVFSFileSize *bytes_total)
{
	GnomeVFSFileInfoOptions info_options;
	GnomeVFSDirectoryVisitOptions visit_options;
	GnomeVFSResult result;
	const GList *p;
	GList *file_list_end;
	guint i, n;

	*file_list = file_list_end = NULL;
	*files_total = 0;
	*bytes_total = 0;

	if (xfer_options & GNOME_VFS_XFER_FOLLOWLINKS)
		info_options = GNOME_VFS_FILE_INFO_FOLLOWLINKS;
	else
		info_options = GNOME_VFS_FILE_INFO_DEFAULT;

	/* First add all the files to the list.  These will be all added at the
           head of the list.  */

	for (p = source_name_list; p != NULL; p = p->next) {
		GnomeVFSURI *uri;
		gchar *source_name;
		Source *new_source;

		source_name = p->data;
		uri = gnome_vfs_uri_append_path (source_dir_uri, source_name);

		new_source = g_new (Source, 1);
		new_source->name = g_strdup (source_name);
		gnome_vfs_file_info_init (&new_source->info);

		result = gnome_vfs_get_file_info_uri (uri, &new_source->info,
						      info_options, NULL);
		if (result != GNOME_VFS_OK) {
			gnome_vfs_uri_unref (uri);
			g_free (new_source->name);
			gnome_vfs_file_info_clear (&new_source->info);
			free_xfer_file_list (*file_list);
			*file_list = NULL;
			return result;
		}

		(*files_total)++;
		if (new_source->info.type == GNOME_VFS_FILE_TYPE_REGULAR)
			*bytes_total += new_source->info.size;

		append_to_xfer_file_list (file_list, &file_list_end,
					  new_source);

		gnome_vfs_uri_unref (uri);
	}

	/* If requested to, then recurse into subdirectories.  */

	if (! (xfer_options & GNOME_VFS_XFER_RECURSIVE))
		return GNOME_VFS_OK;

	visit_options = GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK;
	if (xfer_options & GNOME_VFS_XFER_SAMEFS)
		visit_options |= GNOME_VFS_DIRECTORY_VISIT_SAMEFS;

	n = *files_total;
	for (i = 0, p = *file_list; i < n; i++, p = p->next) {
		RecursionInfo recursion_info;
		Source *source;
		GnomeVFSURI *uri;

		source = p->data;
		if (source->info.type != GNOME_VFS_FILE_TYPE_DIRECTORY)
			continue;

		recursion_info.base_path = g_strconcat (source->name, "/",
							NULL);
		recursion_info.files_total_ptr = files_total;
		recursion_info.bytes_total_ptr = bytes_total;
		recursion_info.file_list_ptr = file_list;
		recursion_info.file_list_end_ptr = &file_list_end;
		recursion_info.result = GNOME_VFS_OK;

		uri = gnome_vfs_uri_append_path (source_dir_uri, source->name);
		result = gnome_vfs_directory_visit_uri
			(uri, info_options, NULL, NULL, visit_options,
			 xfer_file_list_recursion_callback, &recursion_info);

		g_free (recursion_info.base_path);
		recursion_info.base_path = NULL;
		gnome_vfs_uri_unref (uri);

		if (result != GNOME_VFS_OK
		    || recursion_info.result != GNOME_VFS_OK) {
			free_xfer_file_list (*file_list);
			*file_list = NULL;
			if (result != GNOME_VFS_OK)
				return result;
			else
				return recursion_info.result;
		}
	}

	return GNOME_VFS_OK;
}


static GnomeVFSResult
copy_regular (Source *source,
	      GnomeVFSURI *source_uri,
	      GnomeVFSURI *target_uri,
	      GnomeVFSXferProgressInfo *progress_info,
	      GnomeVFSXferOptions xfer_options,
	      GnomeVFSXferErrorMode *error_mode,
	      GnomeVFSXferOverwriteMode *overwrite_mode,
	      GnomeVFSXferProgressCallback progress_callback,
	      gpointer data,
	      gboolean *skip)
{
	GnomeVFSResult result;
	GnomeVFSHandle *source_handle, *target_handle;

	progress_info->phase = GNOME_VFS_XFER_PHASE_OPENSOURCE;
	result = xfer_open_source (&source_handle, source_uri,
				   progress_info, xfer_options,
				   error_mode, progress_callback,
				   data, skip);
	if (*skip)
		return GNOME_VFS_OK;
	if (result != GNOME_VFS_OK)
		return result;

	progress_info->phase = GNOME_VFS_XFER_PHASE_OPENTARGET;
	result = xfer_create_target (&target_handle, target_uri,
				     progress_info, xfer_options,
				     error_mode, overwrite_mode,
				     progress_callback, data,
				     skip);

	if (*skip) {
		gnome_vfs_close (source_handle);
		return GNOME_VFS_OK;
	}
	if (result != GNOME_VFS_OK) {
		gnome_vfs_close (source_handle);
		return result;
	}

	result = xfer_file (target_handle, source_handle,
			    progress_info, xfer_options, error_mode,
			    source->info.io_block_size,
			    progress_callback, data,
			    skip);

	/* FIXME: Check errors here.  */
	gnome_vfs_close (source_handle);
	gnome_vfs_close (target_handle);

	if (*skip)
		return GNOME_VFS_OK;

	return result;
}

static GnomeVFSResult
copy_directory (Source *source,
		GnomeVFSURI *source_uri,
		GnomeVFSURI *target_uri,
		GnomeVFSXferProgressInfo *progress_info,
		GnomeVFSXferOptions xfer_options,
		GnomeVFSXferErrorMode *error_mode,
		GnomeVFSXferOverwriteMode *overwrite_mode,
		GnomeVFSXferProgressCallback progress_callback,
		gpointer data,
		gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;

	*skip = FALSE;
	do {
		retry = FALSE;

		result = gnome_vfs_make_directory_for_uri (target_uri, 0700);

		/* We ignore file existance here so that we are not too much of
                   a nuisance.  (FIXME?)  */
		if (result != GNOME_VFS_OK
		    && result != GNOME_VFS_ERROR_FILEEXISTS)
			retry = handle_error (&result, progress_info,
					      error_mode,
					      progress_callback, data, skip);
	} while (retry);

	return result;
}

static GnomeVFSResult
copy_special (Source *source,
	      GnomeVFSURI *source_uri,
	      GnomeVFSURI *target_uri,
	      GnomeVFSXferProgressInfo *progress_info,
	      GnomeVFSXferOptions xfer_options,
	      GnomeVFSXferErrorMode *error_mode,
	      GnomeVFSXferOverwriteMode *overwrite_mode,
	      GnomeVFSXferProgressCallback callback,
	      gpointer data,
	      gboolean *skip)
{
	return GNOME_VFS_OK;	/* FIXME TODO TODO TODO */
}

static GnomeVFSResult
remove_directory (GnomeVFSURI *uri,
		  GnomeVFSXferProgressInfo *progress_info,
		  GnomeVFSXferOptions xfer_options,
		  GnomeVFSXferErrorMode *error_mode,
		  GnomeVFSXferOverwriteMode *overwrite_mode,
		  GnomeVFSXferProgressCallback progress_callback,
		  gpointer data,
		  gboolean *skip)
{
	GnomeVFSResult result;
	gboolean retry;

	do {
		retry = FALSE;

		result = gnome_vfs_remove_directory_from_uri (uri);
		if (result != GNOME_VFS_OK)
			retry = handle_error (&result, progress_info,
					      error_mode, progress_callback,
					      data, skip);
	} while (retry);

	return result;
}


static GnomeVFSResult
move_file (Source *source,
	   GnomeVFSURI *source_uri,
	   GnomeVFSURI *target_uri,
	   GnomeVFSXferProgressInfo *progress_info,
	   GnomeVFSXferOptions xfer_options,
	   GnomeVFSXferErrorMode *error_mode,
	   GnomeVFSXferOverwriteMode *overwrite_mode,
	   GnomeVFSXferProgressCallback progress_callback,
	   gpointer data,
	   gboolean *skip)
{
	GnomeVFSResult result;
	gboolean force_replace;
	gboolean retry;

	if (*overwrite_mode & GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE)
		force_replace = TRUE;
	else
		force_replace = FALSE;

	do {
		retry = FALSE;

		result = gnome_vfs_move_uri (source_uri, target_uri,
					     force_replace);

		if (result == GNOME_VFS_ERROR_FILEEXISTS) {
			retry = handle_overwrite (&result,
						  progress_info,
						  error_mode,
						  overwrite_mode,
						  progress_callback,
						  data,
						  &force_replace,
						  skip);
		} else if (result != GNOME_VFS_OK) {
			retry = handle_error (&result,
					      progress_info,
					      error_mode,
					      progress_callback,
					      data,
					      skip);
		}
	} while (retry);

	return result;
}

static GnomeVFSResult
move_directory_nonrecursive (Source *source,
			     GnomeVFSURI *source_uri,
			     GnomeVFSURI *target_uri,
			     GnomeVFSXferProgressInfo *progress_info,
			     GnomeVFSXferOptions xfer_options,
			     GnomeVFSXferErrorMode *error_mode,
			     GnomeVFSXferOverwriteMode *overwrite_mode,
			     GnomeVFSXferProgressCallback progress_callback,
			     gpointer data,
			     gboolean *skip)
{
	GnomeVFSResult result;

	result = copy_directory (source, source_uri, target_uri,
				 progress_info, xfer_options, error_mode,
				 overwrite_mode, progress_callback, data,
				 skip);
	if (skip || result != GNOME_VFS_OK)
		return result;

	progress_info->phase = GNOME_VFS_XFER_PHASE_DELETESOURCE;

	result = remove_directory (source_uri, progress_info, xfer_options,
				   error_mode, overwrite_mode,
				   progress_callback, data, skip);

	return result;
}

static GnomeVFSResult
fast_move (GnomeVFSURI *source_dir_uri,
	   const GList *source_name_list,
	   GnomeVFSURI *target_dir_uri,
	   const GList *target_name_list,
	   GnomeVFSXferOptions xfer_options,
	   GnomeVFSXferErrorMode error_mode,
	   GnomeVFSXferOverwriteMode overwrite_mode,
	   GnomeVFSXferProgressCallback progress_callback,
	   gpointer data)
{
	GnomeVFSXferProgressInfo progress_info;
	GnomeVFSResult result;
	GList *file_list;
	const GList *sp, *tp;

	init_progress (&progress_info);

	progress_info.phase = GNOME_VFS_XFER_PHASE_READYTOGO;
	if (! (* progress_callback) (&progress_info, data)) {
		free_progress (&progress_info);
		return GNOME_VFS_ERROR_INTERRUPTED;
	}

	/* Create the file list.  If we are asked to make a recursive move, we
	   don't want to do a recursive visit: moving a directory will move all
	   the files contained in it automagically.  */

	result = create_xfer_file_list
		(source_dir_uri, source_name_list,
		 xfer_options & ~GNOME_VFS_XFER_RECURSIVE,
		 &file_list,
		 &progress_info.files_total,
		 &progress_info.bytes_total);
	if (result != GNOME_VFS_OK)
		return result;

	sp = file_list;
	tp = target_name_list;

	while (sp != NULL) {
		const gchar *source_name, *target_name;
		GnomeVFSURI *source_uri, *target_uri;
		Source *source;
		gboolean skip;

		source = sp->data;

		source_name = source->name;
		if (tp == NULL || tp->data == NULL)
			target_name = source_name;
		else
			target_name = tp->data;

		source_uri = gnome_vfs_uri_append_path (source_dir_uri,
							source_name);
		target_uri = gnome_vfs_uri_append_path (target_dir_uri,
							target_name);

		progress_info.phase = GNOME_VFS_XFER_PHASE_XFERRING;
		progress_info.source_name
			= gnome_vfs_uri_to_string (source_uri,
						   GNOME_VFS_URI_HIDE_PASSWORD);
		progress_info.target_name
			= gnome_vfs_uri_to_string (target_uri,
						   GNOME_VFS_URI_HIDE_PASSWORD);
		progress_info.file_size = source->info.size;
		progress_info.bytes_copied = 0;

		if (progress_callback != NULL
		    && ! (* progress_callback) (&progress_info, data)) {
			free_progress (&progress_info);
			free_xfer_file_list (file_list);
			return GNOME_VFS_ERROR_INTERRUPTED;
		}

		/* If this is a directory and we are not requested to make a
                   recursive copy, we have to copy it by hand.  */
		if (source->info.type == GNOME_VFS_FILE_TYPE_DIRECTORY
		    && ! (xfer_options & GNOME_VFS_XFER_RECURSIVE)) {
			result = move_directory_nonrecursive
						(source,
						 source_uri, target_uri,
						 &progress_info,
						 xfer_options,
						 &error_mode,
						 &overwrite_mode,
						 progress_callback, data,
						 &skip);
		} else {
			result = move_file (source, source_uri, target_uri,
					    &progress_info, xfer_options,
					    &error_mode, &overwrite_mode,
					    progress_callback, data, &skip);
		}

		if (skip) {
			free_progress (&progress_info);
			continue;
		}
		if (result != GNOME_VFS_OK) {
			free_progress (&progress_info);
			break;
		}

		progress_info.phase = GNOME_VFS_XFER_PHASE_FILECOMPLETED;
		progress_info.bytes_copied = progress_info.file_size;
		if (! (* progress_callback) (&progress_info, data)) {
			free_progress (&progress_info);
			free_xfer_file_list (file_list);
			return GNOME_VFS_ERROR_INTERRUPTED;
		}

		sp = sp->next;
		if (tp != NULL)
			tp = tp->next;
	}

	free_progress (&progress_info);
	free_xfer_file_list (file_list);

	/* Done, at last.  At this point, there is no chance to interrupt the
           operation anymore so we don't check the return value.  */
	progress_info.phase = GNOME_VFS_XFER_PHASE_COMPLETED;
	(* progress_callback) (&progress_info, data);

	return result;
}

/* FIXME: This function does not deal with recursive copying of multiple directories properly.   */
GnomeVFSResult
gnome_vfs_xfer_uri (GnomeVFSURI *source_dir_uri,
		    const GList *source_name_list,
		    GnomeVFSURI *target_dir_uri,
		    const GList *target_name_list,
		    GnomeVFSXferOptions xfer_options,
		    GnomeVFSXferErrorMode error_mode,
		    GnomeVFSXferOverwriteMode overwrite_mode,
		    GnomeVFSXferProgressCallback progress_callback,
		    gpointer data)
{
	GnomeVFSXferProgressInfo progress_info;
	GnomeVFSResult result;
	GList *file_list;
	GList *rmdir_list;
	GList *p;
	gboolean same_fs;

	g_return_val_if_fail (source_dir_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_name_list != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (target_dir_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	if (gnome_vfs_check_same_fs_uris (source_dir_uri, target_dir_uri,
					  &same_fs) == GNOME_VFS_OK
	    && same_fs
	    && (xfer_options & GNOME_VFS_XFER_REMOVESOURCE)) {
		/* Optimization possible!  We can use `gnome_vfs_move()'
                   instead of copying the files manually and remove the
                   original copies.  */
		return fast_move (source_dir_uri, source_name_list,
				  target_dir_uri, target_name_list,
				  xfer_options, error_mode,
				  overwrite_mode, progress_callback, data);
	}
	
	init_progress (&progress_info);

	result = create_xfer_file_list (source_dir_uri, source_name_list,
					xfer_options, &file_list,
					&progress_info.files_total,
					&progress_info.bytes_total);
	if (result != GNOME_VFS_OK)
		return result;

	progress_info.phase = GNOME_VFS_XFER_PHASE_READYTOGO;
	if (! (* progress_callback) (&progress_info, data)) {
		free_xfer_file_list (file_list);
		free_progress (&progress_info);
		return GNOME_VFS_ERROR_INTERRUPTED;
	}

	/* This will hold a list of the directories to remove when we are done
           copying all the files.  */
	rmdir_list = NULL;

	for (p = file_list; p != NULL; p = p->next) {
		GnomeVFSURI *source_uri, *target_uri;
		Source *source;
		gboolean skip;

		free_progress (&progress_info);

		source = p->data;

		source_uri = gnome_vfs_uri_append_path (source_dir_uri,
							source->name);

		if (target_name_list != NULL) {
			target_uri = gnome_vfs_uri_append_path
				(target_dir_uri, target_name_list->data);
			target_name_list = target_name_list->next;
		} else {
			target_uri = gnome_vfs_uri_append_path (target_dir_uri,
								source->name);
		}

		progress_info.phase = GNOME_VFS_XFER_PHASE_XFERRING;
		progress_info.source_name
			= gnome_vfs_uri_to_string (source_uri,
						   GNOME_VFS_URI_HIDE_PASSWORD);
		progress_info.target_name
			= gnome_vfs_uri_to_string (target_uri,
						   GNOME_VFS_URI_HIDE_PASSWORD);
		progress_info.file_size = source->info.size;
		progress_info.bytes_copied = 0;

		if (progress_callback != NULL
		    && ! (* progress_callback) (&progress_info, data)) {
			free_progress (&progress_info);
			free_xfer_file_list (file_list);
			return GNOME_VFS_ERROR_INTERRUPTED;
		}

		switch (source->info.type) {
		case GNOME_VFS_FILE_TYPE_REGULAR:
			result = copy_regular (source,
					       source_uri, target_uri,
					       &progress_info,
					       xfer_options,
					       &error_mode,
					       &overwrite_mode,
					       progress_callback, data,
					       &skip);
			break;
		case GNOME_VFS_FILE_TYPE_DIRECTORY:
			result = copy_directory (source,
						 source_uri, target_uri,
						 &progress_info,
						 xfer_options,
						 &error_mode,
						 &overwrite_mode,
						 progress_callback, data,
						 &skip);
			break;
		default:
			result = copy_special (source,
					       source_uri, target_uri,
					       &progress_info,
					       xfer_options,
					       &error_mode,
					       &overwrite_mode,
					       progress_callback, data,
					       &skip);
		}

		/* Remove the source if requested to.  */
		if ((xfer_options & GNOME_VFS_XFER_REMOVESOURCE)
		    && ! skip && result == GNOME_VFS_OK) {
			/* Directories must be removed last.  */
			if (source->info.type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				rmdir_list = g_list_prepend
					(rmdir_list,
					 gnome_vfs_uri_ref (source_uri));
			} else {
				gnome_vfs_unlink_from_uri (source_uri);
			}
		}

		gnome_vfs_uri_unref (source_uri);
		gnome_vfs_uri_unref (target_uri);

		/* We assume that the copy function handles the callback by
		   itself, so if it has returned an error we just use it
		   as our return value.  */
		if (skip) {
			free_progress (&progress_info);
			continue;
		}
		if (result != GNOME_VFS_OK) {
			free_progress (&progress_info);
			break;
		}

		/* FIXME Set attributes.  */

		progress_info.phase = GNOME_VFS_XFER_PHASE_FILECOMPLETED;
		progress_info.bytes_copied = progress_info.file_size;
		if (! (* progress_callback) (&progress_info, data)) {
			free_progress (&progress_info);
			free_xfer_file_list (file_list);
			return GNOME_VFS_ERROR_INTERRUPTED;
		}

		progress_info.file_index++;

		free_progress (&progress_info);
	}

	/* Now remove all the directories that have been transferred.  The list
           should be ordered for giving correct results already (i.e. inner
           directories first).  */
	if (rmdir_list != NULL) {
		init_progress (&progress_info);

		progress_info.phase = GNOME_VFS_XFER_PHASE_DELETESOURCE;
		progress_info.target_name = NULL;
		progress_info.files_total = g_list_length (rmdir_list);
		progress_info.bytes_total = 0; /* FIXME inconsistent */
		progress_info.file_size = 0; /* FIXME inconsistent */
		progress_info.bytes_copied = 0;
		progress_info.total_bytes_copied = 0;

		for (p = rmdir_list; p != NULL; p = p->next) {
			GnomeVFSURI *uri;
			gboolean skip;

			uri = p->data;
			progress_info.source_name
				= gnome_vfs_uri_to_string
					(uri, GNOME_VFS_URI_HIDE_PASSWORD);

			result = remove_directory (uri, &progress_info,
						   xfer_options, &error_mode,
						   &overwrite_mode,
						   progress_callback,
						   data, &skip);
			gnome_vfs_uri_unref (uri);

			progress_info.file_index++;

			free_progress (&progress_info);

			if (skip)
				continue;
			if (result != GNOME_VFS_OK)
				break;
		}

		/* Free directory URIs that have not been handled so far.  */
		for (; p != NULL; p = p->next)
			gnome_vfs_uri_unref (p->data);

		g_list_free (rmdir_list);
	}

	free_xfer_file_list (file_list);

	/* Done, at last.  At this point, there is no chance to interrupt the
           operation anymore so we don't check the return value.  */
	progress_info.phase = GNOME_VFS_XFER_PHASE_COMPLETED;
	(* progress_callback) (&progress_info, data);

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
	GnomeVFSURI *source_dir_uri;
	GnomeVFSURI *target_dir_uri;
	GnomeVFSResult result;

	source_dir_uri = gnome_vfs_uri_new (source_dir);
	if (source_dir_uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;
	target_dir_uri = gnome_vfs_uri_new (target_dir);
	if (target_dir_uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_xfer_uri (source_dir_uri,
				     source_name_list,
				     target_dir_uri,
				     target_name_list,
				     xfer_options,
				     error_mode,
				     overwrite_mode,
				     progress_callback,
				     data);

	gnome_vfs_uri_unref (source_dir_uri);
	gnome_vfs_uri_unref (target_dir_uri);

	return result;
}
