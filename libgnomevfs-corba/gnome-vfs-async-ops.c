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
#include <stdio.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-corba.h"
#include "gnome-vfs-slave.h"
#include "gnome-vfs-slave-launch.h"


struct _NotifyServant {
	POA_GNOME_VFS_Slave_Notify servant;
	GnomeVFSAsyncContext *context;
};
typedef struct _NotifyServant NotifyServant;

/* FIXME: We are duplicating GList here.  */
struct _GnomeVFSAsyncHandle {
	GNOME_VFS_Slave_FileHandle file_handle_objref;
	GnomeVFSAsyncHandle *next, *prev;
};

struct _GnomeVFSAsyncFileOpInfo {
	GnomeVFSAsyncHandle *current_handle;
	gpointer buffer;
	GnomeVFSFileSize buffer_size;
};
typedef struct _GnomeVFSAsyncFileOpInfo GnomeVFSAsyncFileOpInfo;

struct _GnomeVFSAsyncDirectoryOpInfo {
	gchar **meta_keys;
	guint num_meta_keys;
	GnomeVFSDirectoryList *list;
};
typedef struct _GnomeVFSAsyncDirectoryOpInfo GnomeVFSAsyncDirectoryOpInfo;

struct _GnomeVFSAsyncXferOpInfo {
	GnomeVFSXferProgressInfo progress_info;
	GnomeVFSXferOptions xfer_options;
	GnomeVFSXferErrorMode error_mode;
	GnomeVFSXferOverwriteMode overwrite_mode;
};
typedef struct _GnomeVFSAsyncXferOpInfo GnomeVFSAsyncXferOpInfo;

enum _GnomeVFSAsyncOperation {
	GNOME_VFS_ASYNC_OP_NONE,
	GNOME_VFS_ASYNC_OP_RESET,
	GNOME_VFS_ASYNC_OP_OPEN,
	GNOME_VFS_ASYNC_OP_CREATE,
	GNOME_VFS_ASYNC_OP_CLOSE,
	GNOME_VFS_ASYNC_OP_READ,
	GNOME_VFS_ASYNC_OP_WRITE,
	GNOME_VFS_ASYNC_OP_LOAD_DIRECTORY,
	GNOME_VFS_ASYNC_OP_XFER
};
typedef enum _GnomeVFSAsyncOperation GnomeVFSAsyncOperation;

struct _GnomeVFSAsyncContext {
	GNOME_VFS_Slave_Notify notify_objref;
	NotifyServant *notify_servant;

	GNOME_VFS_Slave_Request request_objref;

	GnomeVFSAsyncHandle *handles;

	GnomeVFSAsyncOperation operation_in_progress;

	union {
		GnomeVFSAsyncFileOpInfo file;
		GnomeVFSAsyncDirectoryOpInfo directory;
		GnomeVFSAsyncXferOpInfo xfer;
	} op_info;

	gpointer callback;
	gpointer callback_data;

	CORBA_Environment ev;
};


#define RETURN_IF_CONTEXT_BUSY(context)					    \
	g_return_val_if_fail						    \
		(context->operation_in_progress == GNOME_VFS_ASYNC_OP_NONE, \
		 GNOME_VFS_ERROR_INPROGRESS)


inline static GnomeVFSAsyncContext *
context_from_servant (PortableServer_Servant servant)
{
	NotifyServant *notify_servant;

	notify_servant = (NotifyServant *) servant;
	return notify_servant->context;
}

static void
free_servant (PortableServer_Servant servant)
{
	g_free (servant);
}

static GnomeVFSAsyncHandle *
alloc_handle (GnomeVFSAsyncContext *context,
	      GNOME_VFS_Slave_FileHandle file_handle_objref)
{
	GnomeVFSAsyncHandle *new_handle;

	new_handle = g_new (GnomeVFSAsyncHandle, 1);
	new_handle->file_handle_objref = file_handle_objref;

	if (context->handles == NULL) {
		new_handle->prev = NULL;
		new_handle->next = NULL;
		context->handles = new_handle;
	} else {
		new_handle->prev = NULL;
		new_handle->next = context->handles;
		context->handles->prev = new_handle;
		context->handles = new_handle;
	}

	return new_handle;
}

static void
free_handle (GnomeVFSAsyncContext *context,
	     GnomeVFSAsyncHandle *handle)
{
	CORBA_Object_release (handle->file_handle_objref, &context->ev);

	g_free (handle);
}

static void
remove_handle (GnomeVFSAsyncContext *context,
	       GnomeVFSAsyncHandle *handle)
{
	if (handle->prev)
		handle->prev->next = handle->next;
	if (handle->next)
		handle->next->prev = handle->prev;

	if (handle == context->handles)
		context->handles = context->handles->next;

	free_handle (context, handle);
}

static void
free_handle_list (GnomeVFSAsyncContext *context)
{
	GnomeVFSAsyncHandle *p, *pnext;

	for (p = context->handles; p != NULL; p = pnext) {
		pnext = p->next;
		free_handle (context, p);
	}
}


/* Freeing the operation info.  */

static void
free_op_info (GnomeVFSAsyncContext *context)
{
	switch (context->operation_in_progress) {
	case GNOME_VFS_ASYNC_OP_OPEN:
	case GNOME_VFS_ASYNC_OP_CREATE:
		break;
	case GNOME_VFS_ASYNC_OP_CLOSE:
		break;
	case GNOME_VFS_ASYNC_OP_READ:
		break;
	case GNOME_VFS_ASYNC_OP_WRITE:
		break;
	case GNOME_VFS_ASYNC_OP_LOAD_DIRECTORY:
		{
			GnomeVFSAsyncDirectoryOpInfo *info;
			int i;

			info = &context->op_info.directory;
			for (i = 0; i < info->num_meta_keys; i++)
				g_free (info->meta_keys[i]);
			g_free (info->meta_keys);

			/* Notice that we don't really do this, because the
                           programmer might want to keep the directory
                           information obtained so far.  */
#if 0
			if (info->list != NULL)
				gnome_vfs_directory_list_destroy (info->list);
#endif
		}
		break;
	case GNOME_VFS_ASYNC_OP_XFER:
	case GNOME_VFS_ASYNC_OP_NONE:
	default:
		break;
	}
}


/* Basic methods in the Notify interface.  */

static void
impl_Notify_reset (PortableServer_Servant *servant,
		   CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncContextResetCallback callback;

	g_warning ("Slave has been reset.");

	context = context_from_servant (servant);
	callback = context->callback;

	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_RESET)
		g_warning ("Unexpected reset!?");

	free_handle_list (context);

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	(* callback) (context, context->callback_data);
}

static void
impl_Notify_dying (PortableServer_Servant servant,
		   CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;

	g_warning ("Our slave is dying.");

	context = context_from_servant (servant);

	if (context->request_objref != CORBA_OBJECT_NIL)
		CORBA_Object_release (context->request_objref, ev);

	if (context->notify_objref != CORBA_OBJECT_NIL) {
		POA_GNOME_VFS_Slave_Notify__fini
			((POA_GNOME_VFS_Slave_Notify *) context->notify_servant,
			 ev);
		if (ev->_major != CORBA_NO_EXCEPTION)
			g_warning (_("Cannot kill GNOME::VFS::Slave::Notify -- exception %s"),
				   CORBA_exception_id (ev));
		free_servant (context->notify_servant);
		CORBA_Object_release (context->notify_objref, ev);
	}

	CORBA_exception_free (&context->ev);

	free_handle_list (context);

	g_free (context);

	g_warning ("Context cleanup done");
}

static void
impl_Notify_open (PortableServer_Servant servant,
		  const GNOME_VFS_Result result,
		  const GNOME_VFS_Slave_FileHandle handle,
		  CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncOpenCallback callback;
	GnomeVFSAsyncHandle *new_handle;

	context = context_from_servant (servant);

	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_OPEN
	    && context->operation_in_progress != GNOME_VFS_ASYNC_OP_CREATE)
		return;

	new_handle = alloc_handle (context, handle);
	context->op_info.file.current_handle = new_handle;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	callback = (GnomeVFSAsyncOpenCallback) context->callback;
	(* callback) (context, new_handle, (GnomeVFSResult) result,
		      context->callback_data);
}

static void
impl_Notify_close (PortableServer_Servant servant,
		   const GNOME_VFS_Result result,
		   CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncCloseCallback callback;

	context = context_from_servant (servant);

	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_CLOSE)
		return;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	callback = (GnomeVFSAsyncCloseCallback) context->callback;
	(* callback) (context, (GnomeVFSResult) result,
		      context->callback_data);

	remove_handle (context, context->op_info.file.current_handle);
}

static void
impl_Notify_read (PortableServer_Servant servant,
		  const GNOME_VFS_Result result,
		  const GNOME_VFS_Buffer *data,
		  CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncReadCallback callback;

	context = context_from_servant (servant);

	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_READ)
		return;

	memcpy (context->op_info.file.buffer, data->_buffer, data->_length);

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	callback = (GnomeVFSAsyncReadCallback) context->callback;
	(* callback) (context,
		      context->op_info.file.current_handle,
		      (GnomeVFSResult) result,
		      context->op_info.file.buffer,
		      context->op_info.file.buffer_size,
		      data->_length,
		      context->callback_data);
}

static void
impl_Notify_write (PortableServer_Servant servant,
		   const GNOME_VFS_Result result,
		   const CORBA_unsigned_long bytes_written,
		   CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncReadCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_WRITE)
		return;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	callback = (GnomeVFSAsyncReadCallback) context->callback;
	(* callback) (context,
		      context->op_info.file.current_handle,
		      (GnomeVFSResult) result,
		      context->op_info.file.buffer,
		      context->op_info.file.buffer_size,
		      bytes_written,
		      context->callback_data);
}


/* Transfer-related Notify methods.  */

static void
free_progress (GnomeVFSXferProgressInfo *info)
{
	g_free (info->source_name);
	g_free (info->target_name);
}

static CORBA_boolean
impl_Notify_xfer_start (PortableServer_Servant servant,
			const CORBA_unsigned_long files_total,
			const CORBA_unsigned_long bytes_total,
			CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return CORBA_FALSE;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	progress_info->phase = GNOME_VFS_XFER_PHASE_READYTOGO;
	progress_info->files_total = files_total;
	progress_info->bytes_total = bytes_total;

	return (* callback) (context, progress_info, context->callback_data);
}

static CORBA_boolean
impl_Notify_xfer_file_start (PortableServer_Servant servant,
			     const CORBA_char *source_uri,
			     const CORBA_char *target_uri,
			     const CORBA_unsigned_long bytes_to_copy,
			     CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return CORBA_FALSE;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	g_free (progress_info->source_name);
	g_free (progress_info->target_name);

	progress_info->phase = GNOME_VFS_XFER_PHASE_XFERRING;
	progress_info->file_index++;
	progress_info->source_name = g_strdup (source_uri);
	progress_info->target_name = g_strdup (target_uri);
	progress_info->file_size = bytes_to_copy;
	progress_info->bytes_copied = 0;

	return (* callback) (context, progress_info, context->callback_data);
}

static CORBA_boolean
impl_Notify_xfer_file_progress (PortableServer_Servant servant,
				const CORBA_unsigned_long bytes_copied,
				const CORBA_unsigned_long total_bytes_copied,
				CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return CORBA_FALSE;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	progress_info->phase = GNOME_VFS_XFER_PHASE_XFERRING;
	progress_info->bytes_copied = bytes_copied;
	progress_info->total_bytes_copied = total_bytes_copied;

	return (* callback) (context, progress_info, context->callback_data);
}

static CORBA_boolean
impl_Notify_xfer_file_done (PortableServer_Servant servant,
			    CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return CORBA_FALSE;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	progress_info->phase = GNOME_VFS_XFER_PHASE_FILECOMPLETED;

	return (* callback) (context, progress_info, context->callback_data);
}

static void
impl_Notify_xfer_done (PortableServer_Servant servant,
		       CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	progress_info->phase = GNOME_VFS_XFER_PHASE_COMPLETED;
	(* callback) (context, progress_info, context->callback_data);

	free_progress (progress_info);
}

static void
impl_Notify_xfer_error (PortableServer_Servant servant,
			const GNOME_VFS_Result result,
			CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	progress_info->status = GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR;
	progress_info->vfs_status = result;

	(* callback) (context, progress_info, context->callback_data);

	free_progress (progress_info);
}

static CORBA_unsigned_short
impl_Notify_xfer_query_for_error (PortableServer_Servant servant,
				  const GNOME_VFS_Result result,
				  const GNOME_VFS_Slave_XferPhase phase,
				  CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;
	GnomeVFSXferErrorAction action;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	action = (* callback) (context, progress_info, context->callback_data);

	if (action == GNOME_VFS_XFER_ERROR_ACTION_ABORT)
		context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	return action;
}

static CORBA_unsigned_short
impl_Notify_xfer_query_for_overwrite (PortableServer_Servant servant,
				      const CORBA_char *source_uri,
				      const CORBA_char *target_uri,
				      CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncXferOpInfo *op_info;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferProgressCallback callback;
	GnomeVFSXferOverwriteAction action;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_XFER)
		return GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT;

	op_info = &context->op_info.xfer;
	progress_info = &op_info->progress_info;
	callback = (GnomeVFSAsyncXferProgressCallback) context->callback;

	action = (* callback) (context, progress_info, context->callback_data);

	if (action == GNOME_VFS_XFER_OVERWRITE_MODE_ABORT)
		context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	return action;
}


/* This is not very efficient, as every `gnome_vfs_file_info_set_metadata()'
   implies a linear search for the existance of the new key, but it's fine with
   us for now.  */
static void
set_metadata_from_response (GnomeVFSFileInfo *info,
			    GNOME_VFS_Slave_MetadataResponseList
			            *response_list,
			    gchar **meta_keys)
{
	guint i;

	for (i = 0; i < response_list->_length; i++) {
		GNOME_VFS_Slave_MetadataResponse *response;

		response = response_list->_buffer + i;
		if (response->found) {
			gpointer value;
			guint value_size;

			value_size = response->value._length;
			value = g_malloc (value_size);
			memcpy (value, response->value._buffer, value_size);

			gnome_vfs_file_info_set_metadata
				(info, meta_keys[i], value, value_size);
		}
	}
}

static void
impl_Notify_load_directory (PortableServer_Servant servant,
			    const GNOME_VFS_Result result,
			    const GNOME_VFS_Slave_FileInfoList *files,
			    CORBA_Environment *ev)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSAsyncDirectoryOpInfo *op_info;
	GnomeVFSDirectoryList *list;
	GnomeVFSAsyncDirectoryLoadCallback callback;
	gchar **meta_keys;
	guint i;

	context = context_from_servant (servant);
	if (context->operation_in_progress != GNOME_VFS_ASYNC_OP_LOAD_DIRECTORY)
		return;

	op_info = &context->op_info.directory;
	list = op_info->list;
	meta_keys = op_info->meta_keys;

	if (list == NULL) {
		if (files->_length > 0)
			list = gnome_vfs_directory_list_new ();
	} else {
		gnome_vfs_directory_list_last (list);
	}

	for (i = 0; i < files->_length; i++) {
		GNOME_VFS_Slave_FileInfo *slave_info;
		GnomeVFSFileInfo *info;

		slave_info = files->_buffer + i;

		info = gnome_vfs_file_info_new ();
		memcpy (info, (GnomeVFSFileInfo *) slave_info->data._buffer,
			slave_info->data._length);

		if (slave_info->mime_type[0] == 0)
			info->mime_type = NULL;
		else
			info->mime_type = g_strdup (slave_info->mime_type);

		if (slave_info->symlink_name[0] == 0)
			info->symlink_name = NULL;
		else
			info->symlink_name
				= g_strdup (slave_info->symlink_name);

		info->metadata_list = NULL;
		set_metadata_from_response (info,
					    &slave_info->metadata_response,
					    meta_keys);

		gnome_vfs_directory_list_append (list, info);
	}

	if (result != GNOME_VFS_OK) {
		context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

		for (i = 0; i < op_info->num_meta_keys; i++)
			g_free (op_info->meta_keys[i]);
		g_free (op_info->meta_keys);
	}

	/* Make sure we have set a current position on the list.  */
	if (list != NULL
	    && gnome_vfs_directory_list_get_position (list) == NULL)
		gnome_vfs_directory_list_first (list);

	callback = (GnomeVFSAsyncDirectoryLoadCallback) context->callback;
	(* callback) (context, result, list, files->_length,
		      context->callback_data);
}


static PortableServer_ServantBase__epv base_epv;
static POA_GNOME_VFS_Slave_Notify__epv Notify_epv;
static POA_GNOME_VFS_Slave_Notify__vepv Notify_vepv;

static gboolean
create_notify_object (GnomeVFSAsyncContext *context)
{
	NotifyServant *servant;

	base_epv._private = NULL;
	base_epv.finalize = NULL;
	base_epv.default_POA = NULL;

	Notify_epv.open  = impl_Notify_open;
	Notify_epv.close = impl_Notify_close;
	Notify_epv.read  = impl_Notify_read;
	Notify_epv.write = impl_Notify_write;
	Notify_epv.load_directory = impl_Notify_load_directory;
	Notify_epv.dying = impl_Notify_dying;
	Notify_epv.reset = impl_Notify_reset;

	Notify_epv.xfer_start = impl_Notify_xfer_start;
	Notify_epv.xfer_file_start = impl_Notify_xfer_file_start;
	Notify_epv.xfer_file_progress = impl_Notify_xfer_file_progress;
	Notify_epv.xfer_file_done = impl_Notify_xfer_file_done;
	Notify_epv.xfer_done = impl_Notify_xfer_done;
	Notify_epv.xfer_error = impl_Notify_xfer_error;
	Notify_epv.xfer_query_for_overwrite = impl_Notify_xfer_query_for_overwrite;
	Notify_epv.xfer_query_for_error = impl_Notify_xfer_query_for_error;

	Notify_vepv._base_epv = &base_epv;
	Notify_vepv.GNOME_VFS_Slave_Notify_epv = &Notify_epv;

	servant = g_new0 (NotifyServant, 1);
	servant->servant.vepv = &Notify_vepv;
	servant->context = context;

	POA_GNOME_VFS_Slave_Notify__init ((PortableServer_Servant) servant,
					  &context->ev);
	if (context->ev._major != CORBA_NO_EXCEPTION){
		g_warning (_("Cannot initialize GNOME::VFS:Slave::Notify"));
		g_free (servant);
		return FALSE;
	}

	CORBA_free (PortableServer_POA_activate_object (gnome_vfs_poa,
							servant,
							&context->ev));

	context->notify_objref
		= PortableServer_POA_servant_to_reference (gnome_vfs_poa,
							   servant,
							   &context->ev);

	context->notify_servant = servant;

	return TRUE;
}


GnomeVFSAsyncContext *
gnome_vfs_async_context_new (void)
{
	GnomeVFSAsyncContext *new;
	GnomeVFSProcess *process;

	if (! gnome_vfs_corba_init ())
		return NULL;

	new = g_new (GnomeVFSAsyncContext, 1);

	new->notify_objref = CORBA_OBJECT_NIL;
	new->notify_servant = NULL;

	new->request_objref = CORBA_OBJECT_NIL;

	new->handles = NULL;

	new->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	new->callback = NULL;
	new->callback_data = NULL;

	CORBA_exception_init (&new->ev);

	if (! create_notify_object (new)) {
		gnome_vfs_async_context_destroy (new);
		return NULL;
	}

	process = gnome_vfs_slave_launch (new->notify_objref,
					  &new->request_objref);
	if (process == CORBA_OBJECT_NIL) {
		gnome_vfs_async_context_destroy (new);
		return NULL;
	}

	/* We don't use this for now.  */
	gnome_vfs_process_free (process);

	return new;
}

void
gnome_vfs_async_context_reset (GnomeVFSAsyncContext *context,
			       GnomeVFSAsyncContextResetCallback callback,
			       gpointer callback_data)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (callback != NULL);

	free_op_info (context);

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_RESET;
	context->callback = callback;
	context->callback_data = callback_data;

	if (context->request_objref != CORBA_OBJECT_NIL) {
		g_warning ("Resetting context");
		GNOME_VFS_Slave_Request_reset (context->request_objref,
					       &context->ev);
		if (context->ev._major != CORBA_NO_EXCEPTION) {
			CORBA_char *ior;
			CORBA_Environment ev;

			CORBA_exception_init (&ev);
			ior = CORBA_ORB_object_to_string
				(gnome_vfs_orb, context->request_objref, &ev);

			if (context->ev._major != CORBA_NO_EXCEPTION) 
				g_warning (_("Cannot reset GNOME::VFS::Slave %s -- exception %s"),
					   ior, CORBA_exception_id (&context->ev));
			else
				g_warning (_("Cannot reset GNOME::VFS::Slave (IOR unknown) -- exception %s"),
					   CORBA_exception_id (&context->ev));

			CORBA_exception_free (&ev);
			CORBA_free (ior);
		}
	}
}

void
gnome_vfs_async_context_destroy (GnomeVFSAsyncContext *context)
{
	g_warning (_("Destroying context."));

	g_return_if_fail (context != NULL);

	free_op_info (context);

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_NONE;

	if (context->request_objref != CORBA_OBJECT_NIL) {
		g_warning ("Killing context");
		GNOME_VFS_Slave_Request_die (context->request_objref,
					     &context->ev);
		if (context->ev._major != CORBA_NO_EXCEPTION) {
			CORBA_char *ior;
			CORBA_Environment ev;

			CORBA_exception_init (&ev);
			ior = CORBA_ORB_object_to_string
				(gnome_vfs_orb, context->request_objref, &ev);

			if (context->ev._major != CORBA_NO_EXCEPTION) 
				g_warning (_("Cannot kill GNOME::VFS::Slave %s -- exception %s"),
					   ior, CORBA_exception_id (&context->ev));
			else
				g_warning (_("Cannot kill GNOME::VFS::Slave (IOR unknown) -- exception %s"),
					   CORBA_exception_id (&context->ev));

			CORBA_exception_free (&ev);
			CORBA_free (ior);
		}
	}
}


GnomeVFSResult	 
gnome_vfs_async_open (GnomeVFSAsyncContext *context,
		      const gchar *text_uri,
		      GnomeVFSOpenMode open_mode,
		      GnomeVFSAsyncOpenCallback callback,
		      gpointer callback_data)
{
	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	context->callback = callback;
	context->callback_data = callback_data;

	GNOME_VFS_Slave_Request_open (context->request_objref,
				      text_uri,
				      open_mode,
				      &context->ev);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_OPEN;
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
	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (text_uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	context->callback = callback;
	context->callback_data = callback_data;

	GNOME_VFS_Slave_Request_create (context->request_objref,
					text_uri,
					open_mode,
					exclusive,
					perm,
					&context->ev);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_CREATE;
	return GNOME_VFS_OK;
}


GnomeVFSResult	 
gnome_vfs_async_close (GnomeVFSAsyncContext *context,
		       GnomeVFSAsyncHandle *handle,
		       GnomeVFSAsyncCloseCallback callback,
		       gpointer callback_data)
{
	g_return_val_if_fail (context, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (handle, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	context->callback = callback;
	context->callback_data = callback_data;

	context->op_info.file.current_handle = handle;

	GNOME_VFS_Slave_FileHandle_close (handle->file_handle_objref,
					  &context->ev);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_CLOSE;
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
	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	context->callback = callback;
	context->callback_data = callback_data;

	context->op_info.file.current_handle = handle;
	context->op_info.file.buffer = buffer;
	context->op_info.file.buffer_size = bytes;

	GNOME_VFS_Slave_FileHandle_read (handle->file_handle_objref,
					 bytes,
					 &context->ev);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_READ;
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
	GNOME_VFS_Buffer *corba_buffer;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (handle != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (buffer != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	context->callback = callback;
	context->callback_data = callback_data;

	context->op_info.file.current_handle = handle;
	context->op_info.file.buffer = (gpointer) buffer; /* Dammit */
	context->op_info.file.buffer_size = bytes;

	corba_buffer = GNOME_VFS_Buffer__alloc ();
	corba_buffer->_buffer = CORBA_sequence_CORBA_octet_allocbuf (bytes);
	corba_buffer->_maximum = bytes;
	CORBA_sequence_set_release (corba_buffer, TRUE);

	GNOME_VFS_Slave_FileHandle_write (handle->file_handle_objref,
					  corba_buffer,
					  &context->ev);

	CORBA_free (corba_buffer);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_WRITE;
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
gnome_vfs_async_load_directory (GnomeVFSAsyncContext *context,
				const gchar *uri,
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
	GnomeVFSAsyncDirectoryOpInfo *op_info;
	GNOME_VFS_Slave_DirectoryFilter *my_filter;
	GNOME_VFS_Slave_DirectorySortRuleList *my_sort_rules;
	GNOME_VFS_MetadataKeyList *my_meta_keys;
	guint num_meta_keys, num_sort_rules;
	guint i;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (uri != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	COUNT_ELEMENTS (num_meta_keys, meta_keys, NULL);
	COUNT_ELEMENTS (num_sort_rules, sort_rules,
			GNOME_VFS_DIRECTORY_SORT_NONE);

	/* Initialize context for directory operation.  */

	context->callback = callback;
	context->callback_data = callback_data;

	op_info = &context->op_info.directory;

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

	my_meta_keys = GNOME_VFS_MetadataKeyList__alloc ();
	my_meta_keys->_length = my_meta_keys->_maximum = num_meta_keys;
	my_meta_keys->_buffer = meta_keys;

	my_filter = GNOME_VFS_Slave_DirectoryFilter__alloc ();
	my_filter->type = filter_type;
	my_filter->options = filter_options;
	if (filter_pattern == NULL)
		my_filter->pattern = CORBA_string_dup ("");
	else
		my_filter->pattern = CORBA_string_dup (filter_pattern);

	my_sort_rules = GNOME_VFS_Slave_DirectorySortRuleList__alloc ();
	my_sort_rules->_length = my_sort_rules->_maximum = num_sort_rules;
	my_sort_rules->_buffer
		= CORBA_sequence_GNOME_VFS_Slave_DirectorySortRule_allocbuf
			(num_sort_rules);
	for (i = 0; i < num_sort_rules; i++)
		my_sort_rules->_buffer[i] = sort_rules[i];
	CORBA_sequence_set_release (my_sort_rules, TRUE);

	/* Here we go...  */

	GNOME_VFS_Slave_Request_load_directory (context->request_objref,
						uri,
						options,
						my_meta_keys,
						my_filter,
						my_sort_rules,
						reverse_order,
						items_per_notification,
						&context->ev);

	CORBA_free (my_meta_keys);
	CORBA_free (my_filter);
	CORBA_free (my_sort_rules);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_LOAD_DIRECTORY;
	return GNOME_VFS_OK;
}


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

GnomeVFSResult
gnome_vfs_async_xfer (GnomeVFSAsyncContext *context,
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
	GNOME_VFS_Slave_FileNameList *corba_source_list;
	GNOME_VFS_Slave_FileNameList *corba_target_list;
	GnomeVFSXferProgressInfo *progress_info;
	GnomeVFSAsyncXferOpInfo *op_info;

	g_return_val_if_fail (context != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_dir != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (source_name_list != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (target_dir != NULL, GNOME_VFS_ERROR_BADPARAMS);
	g_return_val_if_fail (progress_callback != NULL, GNOME_VFS_ERROR_BADPARAMS);

	RETURN_IF_CONTEXT_BUSY (context);

	corba_source_list = g_list_to_file_name_list (source_name_list);
	corba_target_list = g_list_to_file_name_list (target_name_list);

	GNOME_VFS_Slave_Request_xfer (context->request_objref,
				      source_dir, corba_source_list,
				      target_dir, corba_target_list,
				      xfer_options, overwrite_mode,
				      &context->ev);

	if (context->ev._major != CORBA_NO_EXCEPTION)
		return GNOME_VFS_ERROR_INTERNAL;

	context->operation_in_progress = GNOME_VFS_ASYNC_OP_XFER;
	context->callback = progress_callback;
	context->callback_data = data;

	op_info = &context->op_info.xfer;
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

	CORBA_free (corba_source_list);
	CORBA_free (corba_target_list);

	return GNOME_VFS_OK;
}
