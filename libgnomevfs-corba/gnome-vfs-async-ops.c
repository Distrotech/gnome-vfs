/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-async-ops.c - Asynchronous operations supported by the
   GNOME Virtual File System (CORBA-based version).

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

/* FIXME: `operation_in_progress' should be set immediately.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libgnorba/gnorba.h>
#include <orb/orbit.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-corba.h"
#include "gnome-vfs-slave.h"
#include "gnome-vfs-slave-process.h"
#include "gnome-vfs-slave-launch.h"


#define RETURN_IF_SLAVE_BUSY(slave)					  \
	g_return_val_if_fail						  \
		(slave->operation_in_progress == GNOME_VFS_ASYNC_OP_NONE, \
		 GNOME_VFS_ERROR_INPROGRESS)


GnomeVFSResult	 
corba_gnome_vfs_async_open (GnomeVFSAsyncHandle **handle_return,
			    const gchar *text_uri,
			    GnomeVFSOpenMode open_mode,
			    GnomeVFSAsyncOpenCallback callback,
			    gpointer callback_data);

GnomeVFSResult	 
corba_gnome_vfs_async_open (GnomeVFSAsyncHandle **handle_return,
			    const gchar *text_uri,
			    GnomeVFSOpenMode open_mode,
			    GnomeVFSAsyncOpenCallback callback,
			    gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new ();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->callback = callback;
	slave->callback_data = callback_data;

	GNOME_VFS_Slave_Request_open (slave->request_objref,
				      text_uri,
				      open_mode,
				      &slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_OPEN;

	*handle_return = (GnomeVFSAsyncHandle *) slave;
	return GNOME_VFS_OK;
}

GnomeVFSResult
corba_gnome_vfs_async_open_as_channel (GnomeVFSAsyncHandle **handle_return,
				       const gchar *text_uri,
				       GnomeVFSOpenMode open_mode,
				       guint advised_block_size,
				       GnomeVFSAsyncOpenAsChannelCallback callback,
				       gpointer callback_data);

GnomeVFSResult
corba_gnome_vfs_async_open_as_channel (GnomeVFSAsyncHandle **handle_return,
				       const gchar *text_uri,
				       GnomeVFSOpenMode open_mode,
				       guint advised_block_size,
				       GnomeVFSAsyncOpenAsChannelCallback callback,
				       gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail ((open_mode & (GNOME_VFS_OPEN_READ
					    | GNOME_VFS_OPEN_WRITE))
			      != (GNOME_VFS_OPEN_READ | GNOME_VFS_OPEN_WRITE),
			      GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new ();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->callback = callback;
	slave->callback_data = callback_data;

	GNOME_VFS_Slave_Request_open_as_channel (slave->request_objref,
						 text_uri,
						 open_mode,
						 advised_block_size,
						 &slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_OPEN_AS_CHANNEL;

	*handle_return = (GnomeVFSAsyncHandle *) slave;

	return GNOME_VFS_OK;
}

GnomeVFSResult	 
corba_gnome_vfs_async_create (GnomeVFSAsyncHandle **handle_return,
			      const gchar *text_uri,
			      GnomeVFSOpenMode open_mode,
			      gboolean exclusive,
			      guint perm,
			      GnomeVFSAsyncOpenCallback callback,
			      gpointer callback_data);

GnomeVFSResult	 
corba_gnome_vfs_async_create (GnomeVFSAsyncHandle **handle_return,
			      const gchar *text_uri,
			      GnomeVFSOpenMode open_mode,
			      gboolean exclusive,
			      guint perm,
			      GnomeVFSAsyncOpenCallback callback,
			      gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail ((open_mode & GNOME_VFS_OPEN_WRITE), GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (! (open_mode & GNOME_VFS_OPEN_READ), GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new ();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->callback = callback;
	slave->callback_data = callback_data;

	GNOME_VFS_Slave_Request_create (slave->request_objref,
					text_uri,
					open_mode,
					exclusive,
					perm,
					&slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_CREATE;

	*handle_return = (GnomeVFSAsyncHandle *) slave;

	return GNOME_VFS_OK;
}

GnomeVFSResult
corba_gnome_vfs_async_create_as_channel (GnomeVFSAsyncHandle **handle_return,
					 const gchar *text_uri,
					 GnomeVFSOpenMode open_mode,
					 gboolean exclusive,
					 guint perm,
					 GnomeVFSAsyncOpenAsChannelCallback callback,
					 gpointer callback_data);
GnomeVFSResult
corba_gnome_vfs_async_create_as_channel (GnomeVFSAsyncHandle **handle_return,
					 const gchar *text_uri,
					 GnomeVFSOpenMode open_mode,
					 gboolean exclusive,
					 guint perm,
					 GnomeVFSAsyncOpenAsChannelCallback callback,
					 gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail ((open_mode & GNOME_VFS_OPEN_WRITE), GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail ((open_mode & GNOME_VFS_OPEN_READ)
			      && (open_mode & GNOME_VFS_OPEN_WRITE),
			      GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new ();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->callback = callback;
	slave->callback_data = callback_data;

	GNOME_VFS_Slave_Request_create_as_channel (slave->request_objref,
						   text_uri,
						   open_mode,
						   exclusive,
						   perm,
						   &slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_OPEN_AS_CHANNEL;

	*handle_return = (GnomeVFSAsyncHandle *) slave;

	return GNOME_VFS_OK;
}

GnomeVFSResult	 
corba_gnome_vfs_async_close (GnomeVFSAsyncHandle *handle,
			     GnomeVFSAsyncCloseCallback callback,
			     gpointer callback_data);
GnomeVFSResult	 
corba_gnome_vfs_async_close (GnomeVFSAsyncHandle *handle,
			     GnomeVFSAsyncCloseCallback callback,
			     gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = (GnomeVFSSlaveProcess *) handle;

	RETURN_IF_SLAVE_BUSY (slave);

	slave->callback = callback;
	slave->callback_data = callback_data;

	GNOME_VFS_Slave_FileHandle_close (slave->file_handle_objref,
					  &slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_CLOSE;

	gnome_vfs_slave_process_destroy (slave);
	
	return GNOME_VFS_OK;
}

GnomeVFSResult	 
corba_gnome_vfs_async_read (GnomeVFSAsyncHandle *handle,
			    gpointer buffer,
			    guint bytes,
			    GnomeVFSAsyncReadCallback callback,
			    gpointer callback_data);
GnomeVFSResult	 
corba_gnome_vfs_async_read (GnomeVFSAsyncHandle *handle,
			    gpointer buffer,
			    guint bytes,
			    GnomeVFSAsyncReadCallback callback,
			    gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = (GnomeVFSSlaveProcess *) handle;

	RETURN_IF_SLAVE_BUSY (slave);

	slave->callback = callback;
	slave->callback_data = callback_data;

	slave->op_info.file.buffer = buffer;
	slave->op_info.file.buffer_size = bytes;

	GNOME_VFS_Slave_FileHandle_read (slave->file_handle_objref,
					 bytes,
					 &slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_READ;
	return GNOME_VFS_OK;
}

GnomeVFSResult	 
corba_gnome_vfs_async_write (GnomeVFSAsyncHandle *handle,
			     gconstpointer buffer,
			     guint bytes,
			     GnomeVFSAsyncWriteCallback callback,
			     gpointer callback_data);
GnomeVFSResult	 
corba_gnome_vfs_async_write (GnomeVFSAsyncHandle *handle,
			     gconstpointer buffer,
			     guint bytes,
			     GnomeVFSAsyncWriteCallback callback,
			     gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;
	GNOME_VFS_Buffer *corba_buffer;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = (GnomeVFSSlaveProcess *) handle;

	RETURN_IF_SLAVE_BUSY (slave);

	slave->callback = callback;
	slave->callback_data = callback_data;

	slave->op_info.file.buffer = (gpointer) buffer; /* Dammit */
	slave->op_info.file.buffer_size = bytes;

	corba_buffer = GNOME_VFS_Buffer__alloc ();
	corba_buffer->_buffer = CORBA_sequence_CORBA_octet_allocbuf (bytes);
	corba_buffer->_maximum = bytes;
	CORBA_sequence_set_release (corba_buffer, TRUE);

	GNOME_VFS_Slave_FileHandle_write (slave->file_handle_objref,
					  corba_buffer,
					  &slave->ev);

	CORBA_free (corba_buffer);

	if (slave->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_WRITE;
	return GNOME_VFS_OK;
}

#define COUNT_ELEMENTS(count, array, term)				\
G_STMT_START{								\
	if ((array) == NULL)						\
		(count) = 0;						\
	else								\
		for ((count) = 0; array[(count)] != (term); (count)++)	\
			;						\
}G_STMT_END

GnomeVFSResult
corba_gnome_vfs_async_get_file_info (GnomeVFSAsyncHandle **handle_return,
				     const char *uri,
				     GnomeVFSFileInfoOptions options,
				     const char *meta_keys[],
				     GnomeVFSAsyncGetFileInfoCallback callback,
				     gpointer callback_data);
GnomeVFSResult
corba_gnome_vfs_async_get_file_info (GnomeVFSAsyncHandle **handle_return,
				     const char *uri,
				     GnomeVFSFileInfoOptions options,
				     const char *meta_keys[],
				     GnomeVFSAsyncGetFileInfoCallback callback,
				     gpointer callback_data)
{
	GnomeVFSSlaveProcess *slave;
	GNOME_VFS_MetadataKeyList my_meta_keys;
	int num_meta_keys;
	GnomeVFSAsyncDirectoryOpInfo *op_info; /* piggyback */

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	COUNT_ELEMENTS (num_meta_keys, meta_keys, NULL);

	op_info = &slave->op_info.directory;
	op_info->num_meta_keys = num_meta_keys;
	if (num_meta_keys == 0) {
		op_info->meta_keys = NULL;
	} else {
		int i;
		op_info->meta_keys = g_new (gchar *, num_meta_keys);
		for (i = 0; i < num_meta_keys; i++)
			op_info->meta_keys[i] = g_strdup (meta_keys[i]);
	}

	slave->callback = callback;
	slave->callback_data = callback_data;

	my_meta_keys._length = my_meta_keys._maximum = num_meta_keys;
	my_meta_keys._buffer = (char **)meta_keys;
	CORBA_sequence_set_release (&my_meta_keys, FALSE);

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_GET_FILE_INFO;

	GNOME_VFS_Slave_Request_get_file_info (slave->request_objref,
					       uri,
					       options,
					       &my_meta_keys,
					       &slave->ev);
	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	*handle_return = (GnomeVFSAsyncHandle *) slave;

	return GNOME_VFS_OK;
}

GnomeVFSResult
corba_gnome_vfs_async_load_directory (GnomeVFSAsyncHandle **handle_return,
				      const gchar *uri,
				      GnomeVFSFileInfoOptions options,
				      const gchar *meta_keys[],
				      GnomeVFSDirectorySortRule sort_rules[],
				      gboolean reverse_order,
				      GnomeVFSDirectoryFilterType filter_type,
				      GnomeVFSDirectoryFilterOptions filter_options,
				      const gchar *filter_pattern,
				      guint items_per_notification,
				      GnomeVFSAsyncDirectoryLoadCallback callback,
				      gpointer callback_data);
GnomeVFSResult
corba_gnome_vfs_async_load_directory (GnomeVFSAsyncHandle **handle_return,
				      const gchar *uri,
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
	GNOME_VFS_Slave_DirectoryFilter my_filter;
	GNOME_VFS_Slave_DirectorySortRuleList my_sort_rules;
	GNOME_VFS_MetadataKeyList my_meta_keys;
	GnomeVFSSlaveProcess *slave;
	guint num_meta_keys, num_sort_rules;
	guint i;
	GnomeVFSAsyncDirectoryOpInfo *op_info;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new ();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	COUNT_ELEMENTS (num_meta_keys, meta_keys, NULL);
	COUNT_ELEMENTS (num_sort_rules, sort_rules,
			GNOME_VFS_DIRECTORY_SORT_NONE);

	/* Initialize slave for directory operation.  */

	slave->callback = callback;
	slave->callback_data = callback_data;

	op_info = &slave->op_info.directory;

	op_info->list = NULL;
	op_info->num_meta_keys = num_meta_keys;
	if (num_meta_keys == 0) {
		op_info->meta_keys = NULL;
	} else {
		op_info->meta_keys = g_new (gchar *, num_meta_keys);
		for (i = 0; i < num_meta_keys; i++)
			op_info->meta_keys[i] = g_strdup (meta_keys[i]);
	}

	/* Prepare the CORBA parameters.  */

	my_meta_keys._length = my_meta_keys._maximum = num_meta_keys;
	my_meta_keys._buffer = (char **)meta_keys;
	CORBA_sequence_set_release (&my_meta_keys, FALSE);

	my_filter.type = filter_type;
	my_filter.options = filter_options;
	if (filter_pattern == NULL)
		my_filter.pattern = "";
	else
		my_filter.pattern = (char *)filter_pattern;

	my_sort_rules._length = my_sort_rules._maximum = num_sort_rules;
	my_sort_rules._buffer = (GNOME_VFS_Slave_DirectorySortRule *)sort_rules;
	CORBA_sequence_set_release (&my_sort_rules, FALSE);

	/* Here we go...  */

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_LOAD_DIRECTORY;

	GNOME_VFS_Slave_Request_load_directory (slave->request_objref,
						uri,
						options,
						&my_meta_keys,
						&my_filter,
						&my_sort_rules,
						reverse_order,
						items_per_notification,
						&slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	*handle_return = (GnomeVFSAsyncHandle *) slave;

	return GNOME_VFS_OK;
}

GnomeVFSResult
corba_gnome_vfs_async_load_directory_uri (GnomeVFSAsyncHandle **handle_return,
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
					  gpointer callback_data);
GnomeVFSResult
corba_gnome_vfs_async_load_directory_uri (GnomeVFSAsyncHandle **handle_return,
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
	char *str_uri;
	GnomeVFSResult retval;

	str_uri = gnome_vfs_uri_to_string(uri, 0);

	retval = corba_gnome_vfs_async_load_directory(handle_return, str_uri, options, meta_keys,
						sort_rules, reverse_order, filter_type,
						filter_options,
						filter_pattern,
						items_per_notification,
						callback,
						callback_data);

	g_free(str_uri);

	return retval;
}

#if 0
static GNOME_VFS_Slave_FileNameList *
g_list_to_file_name_list (const GList *list)
{
	GNOME_VFS_Slave_FileNameList *new;
	const GList *p;
	guint length, i;

	length = g_list_length ((GList *) list); /* FIXME dammit glib */

	/* FIXME it is a waste to re-alloc all the strings, but I am not sure
           how to free the sequence if I don't do so otherwise. */

	new = GNOME_VFS_Slave_FileNameList__alloc ();
	new->_maximum = length;
	new->_length = length;
	new->_buffer = CORBA_sequence_CORBA_string_allocbuf (length);

	for (i = 0, p = list; i < length; i++, p = p->next)
		new->_buffer[i] = CORBA_string_dup (p->data);

	CORBA_sequence_set_release (new, TRUE);

	return new;
}
#endif

GnomeVFSResult
corba_gnome_vfs_async_xfer (GnomeVFSAsyncHandle **handle_return,
			    const gchar *source_dir,
			    const GList *source_name_list,
			    const gchar *target_dir,
			    const GList *target_name_list,
			    GnomeVFSXferOptions xfer_options,
			    GnomeVFSXferErrorMode error_mode,
			    GnomeVFSXferOverwriteMode overwrite_mode,
			    GnomeVFSAsyncXferProgressCallback progress_callback,
			    gpointer data);
GnomeVFSResult
corba_gnome_vfs_async_xfer (GnomeVFSAsyncHandle **handle_return,
			    const gchar *source_dir,
			    const GList *source_name_list,
			    const gchar *target_dir,
			    const GList *target_name_list,
			    GnomeVFSXferOptions xfer_options,
			    GnomeVFSXferErrorMode error_mode,
			    GnomeVFSXferOverwriteMode overwrite_mode,
			    GnomeVFSAsyncXferProgressCallback progress_callback,
			    gpointer data)
{
	GNOME_VFS_Slave_FileNameList corba_source_list;
	GNOME_VFS_Slave_FileNameList corba_target_list;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSSlaveProcess *slave;
	int i;
	const GList *cur;

	g_return_val_if_fail (handle_return != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_dir != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_name_list != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (target_dir != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (progress_callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = gnome_vfs_slave_process_new ();
	if (slave == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	corba_source_list._length = g_list_length((GList *)source_name_list);
	corba_source_list._buffer = alloca(corba_source_list._length * sizeof(char *));
	for(i = 0, cur = source_name_list; i < corba_source_list._length; i++, cur = cur->next) {
		corba_source_list._buffer[i] = cur->data;
	}
	corba_target_list._length = g_list_length((GList *)target_name_list);
	corba_target_list._buffer = alloca(corba_target_list._length * sizeof(char *));
	for(i = 0, cur = target_name_list; i < corba_target_list._length; i++, cur = cur->next) {
		corba_target_list._buffer[i] = cur->data;
	}

	GNOME_VFS_Slave_Request_xfer (slave->request_objref,
				      source_dir, &corba_source_list,
				      target_dir, &corba_target_list,
				      xfer_options, overwrite_mode,
				      &slave->ev);

	if (slave->ev._major != CORBA_NO_EXCEPTION) {
		gnome_vfs_slave_process_destroy (slave);
		return GNOME_VFS_ERROR_INTERNAL;
	}

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_XFER;
	slave->callback = progress_callback;
	slave->callback_data = data;

	op_info = &slave->op_info.xfer;
	op_info->xfer_options = xfer_options;
	op_info->error_mode = error_mode;
	op_info->overwrite_mode = overwrite_mode;

	progress_info = &op_info->progress_info;
	progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_OK;
	progress_info->vfs_status = GNOME_VFS_OK;
	progress_info->phase = GNOME_VFS_XFER_PHASE_COLLECTING;
	progress_info->source_name = NULL;
	progress_info->target_name = NULL;
	progress_info->file_index = 0;
	progress_info->files_total = 0;
	progress_info->bytes_total = 0;
	progress_info->file_size = 0;
	progress_info->bytes_copied = 0;
	progress_info->total_bytes_copied = 0;

	*handle_return = (GnomeVFSAsyncHandle *) slave;

	return GNOME_VFS_OK;
}


GnomeVFSResult
corba_gnome_vfs_async_cancel (GnomeVFSAsyncHandle *handle);
GnomeVFSResult
corba_gnome_vfs_async_cancel (GnomeVFSAsyncHandle *handle)
{
	GnomeVFSSlaveProcess *slave;

	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);

	slave = (GnomeVFSSlaveProcess *) handle;

	if (slave->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	slave->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	gnome_vfs_slave_process_destroy (slave);
	
	/* return GNOME_VFS_OK; */

	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

guint
corba_gnome_vfs_async_add_status_callback (GnomeVFSAsyncHandle *handle,
					   GnomeVFSStatusCallback callback,
					   gpointer user_data);
guint
corba_gnome_vfs_async_add_status_callback (GnomeVFSAsyncHandle *handle,
					   GnomeVFSStatusCallback callback,
					   gpointer user_data)
{
	GnomeVFSSlaveProcess *slave;
	
	g_return_val_if_fail (handle != NULL, 0);
	g_return_val_if_fail (callback != NULL, 0);

	slave = (GnomeVFSSlaveProcess *) handle;

	return gnome_vfs_message_callbacks_add(gnome_vfs_context_get_message_callbacks(slave->context), callback, user_data);
}

void
gnome_vfs_async_remove_status_callback (GnomeVFSAsyncHandle *handle,
					guint callback_id)
{
	GnomeVFSSlaveProcess *slave;
	
	g_return_if_fail (handle != NULL);
	g_return_if_fail (callback_id > 0);

	slave = (GnomeVFSSlaveProcess *) handle;
	
	gnome_vfs_message_callbacks_remove(gnome_vfs_context_get_message_callbacks(slave->context), callback_id);
}
