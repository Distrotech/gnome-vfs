/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-directory.c - Directory handling for the GNOME Virtual
   File System.

   Copyright (C) 1999 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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


struct _GnomeVFSDirectoryHandle {
	/* URI of the directory being accessed through the handle.  */
	GnomeVFSURI *uri;

	/* Options.  */
	GnomeVFSFileInfoOptions options;

	/* Method-specific handle.  */
	GnomeVFSMethodHandle *method_handle;

	/* Metadata list.  */
	GList *meta_keys;

	/* Filter.  */
	const GnomeVFSDirectoryFilter *filter;
};

#define CHECK_IF_SUPPORTED(vfs_method, what)		\
G_STMT_START{						\
	if (vfs_method->what == NULL)			\
		return GNOME_VFS_ERROR_NOTSUPPORTED;	\
}G_STMT_END


static GnomeVFSDirectoryHandle *
gnome_vfs_directory_handle_new (GnomeVFSURI *uri,
				GnomeVFSMethodHandle *method_handle,
				GnomeVFSFileInfoOptions options,
				GList *meta_keys,
				const GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSDirectoryHandle *new;

	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (method_handle != NULL, NULL);

	new = g_new (GnomeVFSDirectoryHandle, 1);

	gnome_vfs_uri_ref (uri);

	new->uri = uri;
	new->method_handle = method_handle;
	new->options = options;
	new->meta_keys = meta_keys;
	new->filter = filter;

	return new;
}

static void
gnome_vfs_directory_handle_destroy (GnomeVFSDirectoryHandle *handle)
{
	g_return_if_fail (handle != NULL);

	gnome_vfs_uri_unref (handle->uri);
	gnome_vfs_free_string_list (handle->meta_keys);

	g_free (handle);
}


static GnomeVFSResult
open_from_uri (GnomeVFSDirectoryHandle **handle,
	       GnomeVFSURI *uri,
	       GnomeVFSFileInfoOptions options,
	       gchar *meta_keys[],
	       const GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSMethodHandle *method_handle;
	GnomeVFSResult result;
	GList *meta_list;

	if (uri->method->open_directory == NULL)
		return GNOME_VFS_ERROR_NOTSUPPORTED;

	meta_list = gnome_vfs_string_list_from_string_array (meta_keys);

	result = uri->method->open_directory (&method_handle, uri,
					      options, meta_list, filter);
	if (result != GNOME_VFS_OK) {
		gnome_vfs_free_string_list (meta_list);
		return result;
	}

	*handle = gnome_vfs_directory_handle_new (uri,
						  method_handle,
						  options,
						  meta_list,
						  filter);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
open (GnomeVFSDirectoryHandle **handle,
      const gchar *text_uri,
      GnomeVFSFileInfoOptions options,
      gchar *meta_keys[],
      const GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSURI *uri;
	GnomeVFSResult result;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	result = open_from_uri (handle, uri, options, meta_keys, filter);

	gnome_vfs_uri_unref (uri);

	return result;
}

GnomeVFSResult
gnome_vfs_directory_open (GnomeVFSDirectoryHandle **handle,
			  const gchar *text_uri,
			  GnomeVFSFileInfoOptions options,
			  gchar *meta_keys[],
			  const GnomeVFSDirectoryFilter *filter)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return open (handle, text_uri, options, meta_keys, filter);
}

GnomeVFSResult
gnome_vfs_directory_open_from_uri (GnomeVFSDirectoryHandle **handle,
				   GnomeVFSURI *uri,
				   GnomeVFSFileInfoOptions options,
				   gchar *meta_keys[],
				   const GnomeVFSDirectoryFilter *filter)
{
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return open_from_uri (handle, uri, options, meta_keys, filter);
}

GnomeVFSResult
gnome_vfs_directory_read_next (GnomeVFSDirectoryHandle *handle,
			       GnomeVFSFileInfo *file_info)
{
	CHECK_IF_SUPPORTED (handle->uri->method, read_directory);

	gnome_vfs_file_info_clear (file_info);
	return handle->uri->method->read_directory (handle->method_handle,
						    file_info);
}

GnomeVFSResult
gnome_vfs_directory_close (GnomeVFSDirectoryHandle *handle)
{
	GnomeVFSResult result;

	CHECK_IF_SUPPORTED (handle->uri->method, close_directory);

	result = handle->uri->method->close_directory (handle->method_handle);

	gnome_vfs_directory_handle_destroy (handle);

	return result;
}


static GnomeVFSResult
load_from_handle (GnomeVFSDirectoryList **list,
		  GnomeVFSDirectoryHandle *handle)
{
	GnomeVFSResult result;
	GnomeVFSFileInfo *info;

	*list = gnome_vfs_directory_list_new ();

	while (1) {
		info = gnome_vfs_file_info_new ();
		result = gnome_vfs_directory_read_next (handle, info);
		if (result != GNOME_VFS_OK)
			break;
		gnome_vfs_directory_list_append (*list, info);
	}

	gnome_vfs_file_info_destroy (info);

	if (result != GNOME_VFS_ERROR_EOF) {
		gnome_vfs_directory_list_destroy (*list);
		return result;
	}

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_directory_load (GnomeVFSDirectoryList **list,
			  const gchar *text_uri,
			  GnomeVFSFileInfoOptions options,
			  gchar *meta_keys[],
			  const GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult result;

	result = gnome_vfs_directory_open (&handle, text_uri, options,
					   meta_keys, filter);
	if (result != GNOME_VFS_OK)
		return result;

	return load_from_handle (list, handle);
}

GnomeVFSResult
gnome_vfs_directory_load_from_uri (GnomeVFSDirectoryList **list,
				   GnomeVFSURI *uri,
				   GnomeVFSFileInfoOptions options,
				   gchar *meta_keys[],
				   const GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult result;

	result = gnome_vfs_directory_open_from_uri (&handle, uri, options,
						    meta_keys, filter);
	if (result != GNOME_VFS_OK)
		return result;

	return load_from_handle (list, handle);
}


struct _DirectoryReference {
	ino_t inode;
	dev_t device;
};
typedef struct _DirectoryReference DirectoryReference;

static GList *
prepend_reference (GList *reference_list,
		   GnomeVFSFileInfo *info)
{
	DirectoryReference *reference;

	reference = g_new (DirectoryReference, 1);
	reference->device = info->device;
	reference->inode = info->inode;

	return g_list_prepend (reference_list, reference);
}

static GList *
remove_first_reference (GList *reference_list)
{
	GList *first;

	if (reference_list == NULL)
		return NULL;

	first = reference_list;
	g_free (first->data);

	reference_list = g_list_remove_link (reference_list, first);
	g_list_free (first);

	return reference_list;
}

/* FIXME?  This leads to an O(2) algorithm, but it should not be noticeable
   unless a *very* deep recursion (more than the depth of reasonable file
   systems) is performed.  */
static gboolean
lookup_ancestor (GList *ancestors,
		 ino_t inode,
		 dev_t device)
{
	GList *p;

	for (p = ancestors; p != NULL; p = p->next) {
		DirectoryReference *reference;

		reference = p->data;
		if (reference->inode == inode && reference->device == device)
			return TRUE;
	}

	return FALSE;
}

static GnomeVFSResult
directory_visit_internal (GnomeVFSURI *uri,
			  const gchar *prefix,
			  GList *ancestor_references, /* DirectoryReference */
			  GnomeVFSFileInfoOptions info_options,
			  gchar *meta_keys[],
			  const GnomeVFSDirectoryFilter *filter,
			  GnomeVFSDirectoryVisitOptions visit_options,
			  GnomeVFSDirectoryVisitFunc callback,
			  gpointer data)
{
	GnomeVFSFileInfo *info;
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult result;
	gboolean stop;

	/* The first time, initialize the ancestor list with this
	   directory.  */
	if (prefix == NULL) {
		GnomeVFSFileInfo *info;

		info = gnome_vfs_file_info_new ();
		result = gnome_vfs_get_file_info_uri (uri, info,
						      info_options, NULL);
		if (result != GNOME_VFS_OK) {
			gnome_vfs_file_info_destroy (info);
			return result;
		}

		if (info->type != GNOME_VFS_FILE_TYPE_DIRECTORY) {
			gnome_vfs_file_info_destroy (info);
			return GNOME_VFS_ERROR_NOTADIRECTORY;
		}

		ancestor_references = prepend_reference (ancestor_references,
							 info);
		gnome_vfs_file_info_destroy (info);
	}

	result = gnome_vfs_directory_open_from_uri (&handle, uri, info_options,
						    meta_keys, filter);
	if (result != GNOME_VFS_OK)
		return result;

	info = gnome_vfs_file_info_new ();

	stop = FALSE;
	while (! stop) {
		gchar *rel_path;
		gboolean recurse;
		gboolean recursing_will_loop;

		result = gnome_vfs_directory_read_next (handle, info);
		if (result != GNOME_VFS_OK)
			break;

		/* Skip "." and "..".  */
		if (info->name[0] == '.'
		    && (info->name[1] == 0
			|| (info->name[1] == '.' && info->name[2] == 0))) {
			gnome_vfs_file_info_clear (info);
			continue;
		}

		if (prefix == NULL)
			rel_path = g_strdup (info->name);
		else
			rel_path = g_strconcat (prefix, info->name, NULL);

		if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY
		    && (visit_options & GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK))
			recursing_will_loop
				= lookup_ancestor (ancestor_references,
						   info->inode, info->device);
		else
			recursing_will_loop = FALSE;

		recurse = FALSE;
		stop = ! (* callback) (rel_path, info, recursing_will_loop,
				       data, &recurse);

		if (! stop
		    && recurse
		    && info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			GnomeVFSURI *new_uri;
			gchar *new_prefix;

			if (prefix == NULL)
				new_prefix = g_strconcat (info->name, "/",
							  NULL);
			else
				new_prefix = g_strconcat (prefix, info->name,
							  "/", NULL);

			new_uri = gnome_vfs_uri_append_path (uri, info->name);


			if (info->is_local)
				ancestor_references = prepend_reference
					(ancestor_references, info);

			result = directory_visit_internal (new_uri,
							   new_prefix,
							   ancestor_references,
							   info_options,
							   meta_keys,
							   filter,
							   visit_options,
							   callback, data);

			if (info->is_local)
				ancestor_references = remove_first_reference
					(ancestor_references);

			if (result != GNOME_VFS_OK)
				stop = TRUE;

			gnome_vfs_uri_unref (new_uri);
			g_free (new_prefix);
		}

		g_free (rel_path);

		gnome_vfs_file_info_clear (info);

		if (stop)
			break;
	}

	gnome_vfs_directory_close (handle);
	gnome_vfs_file_info_destroy (info);

	/* The first time, we are responsible for de-allocating the directory
           reference we have added by ourselves.  */
	if (prefix == NULL)
		ancestor_references
			= remove_first_reference (ancestor_references);

	if (result == GNOME_VFS_ERROR_EOF)
		return GNOME_VFS_OK;
	else
		return result;
}

GnomeVFSResult
gnome_vfs_directory_visit_uri (GnomeVFSURI *uri,
			       GnomeVFSFileInfoOptions info_options,
			       gchar *meta_keys[],
			       const GnomeVFSDirectoryFilter *filter,
			       GnomeVFSDirectoryVisitOptions visit_options,
			       GnomeVFSDirectoryVisitFunc callback,
			       gpointer data)
{
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	return directory_visit_internal (uri, NULL, NULL,
					 info_options, meta_keys, filter,
					 visit_options, callback, data);
}

GnomeVFSResult
gnome_vfs_directory_visit (const gchar *text_uri,
			   GnomeVFSFileInfoOptions info_options,
			   gchar *meta_keys[],
			   const GnomeVFSDirectoryFilter *filter,
			   GnomeVFSDirectoryVisitOptions visit_options,
			   GnomeVFSDirectoryVisitFunc callback,
			   gpointer data)
{
	GnomeVFSURI *uri;
	GnomeVFSResult result;

	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);

	uri = gnome_vfs_uri_new (text_uri);

	result = directory_visit_internal (uri, NULL, NULL,
					   info_options, meta_keys, filter,
					   visit_options, callback, data);

	gnome_vfs_uri_unref (uri);

	return result;
}

GnomeVFSResult
gnome_vfs_directory_visit_files_at_uri (GnomeVFSURI *uri,
					GList *file_list,
					GnomeVFSFileInfoOptions info_options,
					gchar *meta_keys[],
					const GnomeVFSDirectoryFilter *filter,
					GnomeVFSDirectoryVisitOptions
						visit_options,
					GnomeVFSDirectoryVisitFunc callback,
					gpointer data)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	GList *p;

	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (file_list != NULL, GNOME_VFS_ERROR_BADPARAMS);

	info = gnome_vfs_file_info_new ();
	result = GNOME_VFS_OK;

	for (p = file_list; p != NULL; p = p->next) {
		GnomeVFSURI *file_uri;
		gboolean recurse;
		gboolean stop;

		file_uri = gnome_vfs_uri_append_path (uri, p->data);
		gnome_vfs_get_file_info_uri (file_uri, info, info_options,
					     meta_keys);

		recurse = FALSE;
		stop = ! (* callback) (info->name, info, FALSE, data,
				       &recurse);

		if (! stop
		    && recurse
		    && info->type == GNOME_VFS_FILE_TYPE_DIRECTORY)
			result = gnome_vfs_directory_visit_uri
				(uri, info_options,
				 meta_keys, filter,
				 visit_options,
				 callback, data);

		gnome_vfs_uri_unref (file_uri);

		if (result != GNOME_VFS_OK || stop)
			break;
	}

	gnome_vfs_file_info_destroy (info);
	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_directory_visit_files (const gchar *text_uri,
				 GList *file_list,
				 GnomeVFSFileInfoOptions info_options,
				 gchar *meta_keys[],
				 const GnomeVFSDirectoryFilter *filter,
				 GnomeVFSDirectoryVisitOptions
				 	visit_options,
				 GnomeVFSDirectoryVisitFunc callback,
				 gpointer data)
{
	GnomeVFSURI *uri;
	GnomeVFSResult result;

	uri = gnome_vfs_uri_new (text_uri);

	result = gnome_vfs_directory_visit_files_at_uri (uri, file_list,
							 info_options,
							 meta_keys,
							 filter,
							 visit_options,
							 callback,
							 data);
	gnome_vfs_uri_unref (uri);

	return result;
}
