/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */ /*
gnome-vfs-job.c - Jobs for asynchronous operation of the GNOME Virtual File
System (version for POSIX threads).

   Copyright (C) 1999 Free Software Foundation
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

   Authors: 
   	Ettore Perazzoli <ettore@gnu.org> 
  	Pavel Cisler <pavel@eazel.com> 
  	Darin Adler <darin@eazel.com> 

   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs-job.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "gnome-vfs-job-slave.h"

#if GNOME_VFS_JOB_DEBUG

/* FIXME bugzilla.eazel.com 1130
 * - this is should use the correct static mutex initialization macro.
 * However glibconfig.h is broken and the supplied macro gives a warning.
 * Since this id debug only, just use what the macro should be here.
 * even though it is not portable.
 */
GStaticMutex debug_mutex = { NULL, { { } } };
#endif

static void gnome_vfs_job_release_current_op (GnomeVFSJob *job);
static void gnome_vfs_job_release_notify_op  (GnomeVFSJob *job);
static void gnome_vfs_job_finish_destroy     (GnomeVFSJob *job);

static int job_count = 0;

static void
set_fl (int fd, int flags)
{
	int val;

	val = fcntl (fd, F_GETFL, 0);
	if (val < 0) {
		g_warning ("fcntl() F_GETFL failed: %s", strerror (errno));
		return;
	}

	val |= flags;
	
	val = fcntl (fd, F_SETFL, val);
	if (val < 0) {
		g_warning ("fcntl() F_SETFL failed: %s", strerror (errno));
		return;
	}
}

static void
clr_fl (int fd, int flags)
{
	int val;

	val = fcntl (fd, F_GETFL, 0);
	if (val < 0) {
		g_warning ("fcntl() F_GETFL failed: %s", strerror (errno));
		return;
	}

	val &= ~flags;
	
	val = fcntl (fd, F_SETFL, val);
	if (val < 0) {
		g_warning ("fcntl() F_SETFL failed: %s", strerror (errno));
		return;
	}
}

static void
job_signal_ack_condition (GnomeVFSJob *job)
{
	g_mutex_lock (job->notify_ack_lock);
	JOB_DEBUG (("Ack needed: signaling condition. %p", job));
	g_cond_signal (job->notify_ack_condition);
	JOB_DEBUG (("Ack needed: unlocking notify ack. %p", job));
	g_mutex_unlock (job->notify_ack_lock);
}

/* This is used by the master thread to notify the slave thread that it got the
   notification.  */
static void
job_ack_notify (GnomeVFSJob *job)
{
	JOB_DEBUG (("Checking if ack is needed. %p", job));
	if (job->want_notify_ack) {
		JOB_DEBUG (("Ack needed: lock notify ack. %p", job));
		job_signal_ack_condition (job);
	}

	JOB_DEBUG (("unlocking wake up channel. %p", job));

	g_assert (job->notify_op == NULL);
	g_mutex_unlock (job->wakeup_channel_lock);
}

#if GNOME_VFS_JOB_DEBUG
static char debug_wake_channel_out = 'a';
#endif

static gboolean
wake_up (GnomeVFSJob *job, void *wakeup_context)
{
	guint bytes_written;

	JOB_DEBUG (("Wake up! %p, context %p", job, wakeup_context));

	/* Wake up the main thread.  */

	g_io_channel_write (job->wakeup_channel_out, (char *) &wakeup_context, 
		sizeof (void *), &bytes_written);

	JOB_DEBUG (("sent wakeup %c %p", debug_wake_channel_out, job));
	if (bytes_written != sizeof (void *)) {
		JOB_DEBUG (("problems sending a wakeup! %p", job));
		g_warning (_("Error writing to the wakeup GnomeVFSJob channel."));
		return FALSE;
	}

	return TRUE;
}


/* This notifies the master thread asynchronously, without waiting for an
   acknowledgment.  */
static gboolean
job_oneway_notify (GnomeVFSJob *job, void *wakeup_context)
{

	JOB_DEBUG (("lock channel %p", job));
	g_mutex_lock (job->wakeup_channel_lock);

	/* Record which op we want notified. */
	g_assert (job->notify_op == NULL || job->current_op == NULL);
	if (job->notify_op == NULL)
		job->notify_op = job->current_op;

	job->want_notify_ack = FALSE;

	return wake_up (job, wakeup_context);
}


/* This notifies the master threads, waiting until it acknowledges the
   notification.  */
static gboolean
job_notify (GnomeVFSJob *job, void *wakeup_context)
{
	gboolean retval;

	if (gnome_vfs_context_check_cancellation (job->current_op->context)) {
		JOB_DEBUG (("job cancelled, bailing %p", job));
		return FALSE;
	}

	JOB_DEBUG (("Locking wakeup channel - %p", job));
	g_mutex_lock (job->wakeup_channel_lock);

	/* Record which op we want notified. */
	g_assert (job->notify_op == NULL);
	job->notify_op = job->current_op;

	JOB_DEBUG (("Locking notification lock %p", job));
	/* Lock notification, so that the master cannot send the signal until
           we are ready to receive it.  */
	g_mutex_lock (job->notify_ack_lock);

	job->want_notify_ack = TRUE;

	/* Send the notification.  This will wake up the master thread, which
           will in turn signal the notify condition.  */
	retval = wake_up (job, wakeup_context);

	JOB_DEBUG (("Wait notify condition %p", job));
	/* Wait for the notify condition.  */
	g_cond_wait (job->notify_ack_condition, job->notify_ack_lock);

	JOB_DEBUG (("Unlock notify ack lock %p", job));
	/* Acknowledgment got: unlock the mutex.  */
	g_mutex_unlock (job->notify_ack_lock);

	JOB_DEBUG (("Done %p", job));
	return retval;
}

/* This closes the job.  */
static void
job_close (GnomeVFSJob *job)
{
	job->is_empty = TRUE;
	JOB_DEBUG (("Unlocking access lock %p", job));
	g_mutex_unlock (job->access_lock);
}

static gboolean
job_oneway_notify_and_close (GnomeVFSJob *job, void *wakeup_context)
{
	gboolean retval;

	retval = job_oneway_notify (job, wakeup_context);
	job_close (job);

	return retval;
}

static gboolean
job_notify_and_close (GnomeVFSJob *job, void *wakeup_context)
{
	gboolean retval;

	retval = job_notify (job, wakeup_context);
	job_close (job);

	return retval;
}

static void
dispatch_open_callback (void *wakeup_context)
{
	GnomeVFSOpenOpResult *notify_result;

	notify_result = (GnomeVFSOpenOpResult *) wakeup_context;
	
	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result,
				     notify_result->callback_data);
}

static void
dispatch_create_callback (void *wakeup_context)
{
	GnomeVFSCreateOpResult *notify_result;

	notify_result = (GnomeVFSCreateOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result,
				     notify_result->callback_data);
}

static void
dispatch_open_as_channel_callback (void *wakeup_context)
{
	GnomeVFSOpenAsChannelOpResult *notify_result;

	notify_result = (GnomeVFSOpenAsChannelOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->channel,
				     notify_result->result,
				     notify_result->callback_data);
}

static void
dispatch_create_as_channel_callback (void *wakeup_context)
{
	GnomeVFSCreateAsChannelOpResult *notify_result;

	notify_result = (GnomeVFSCreateAsChannelOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->channel,
				     notify_result->result,
				     notify_result->callback_data);
}

static void
dispatch_close_callback (void *wakeup_context)
{
	GnomeVFSCloseOpResult *notify_result;

	notify_result = (GnomeVFSCloseOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result,
				     notify_result->callback_data);
}

static void
dispatch_read_callback (void *wakeup_context)
{
	GnomeVFSReadOpResult *notify_result;

	notify_result = (GnomeVFSReadOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result,
				     notify_result->buffer,
				     notify_result->num_bytes,
				     notify_result->bytes_read,
				     notify_result->callback_data);
}

static void
dispatch_write_callback (void *wakeup_context)
{
	GnomeVFSWriteOpResult *notify_result;

	notify_result = (GnomeVFSWriteOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result,
				     notify_result->buffer,
				     notify_result->num_bytes,
				     notify_result->bytes_written,
				     notify_result->callback_data);
}

static void
dispatch_load_directory_callback (void *wakeup_context)
{
	GnomeVFSLoadDirectoryOpResult *notify_result;

	notify_result = (GnomeVFSLoadDirectoryOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result,
				     notify_result->list,
				     notify_result->entries_read,
				     notify_result->callback_data);
}

static void
dispatch_get_file_info_callback (void *wakeup_context)
{
	GnomeVFSGetFileInfoOpResult *notify_result;

	notify_result = (GnomeVFSGetFileInfoOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result_list,
				     notify_result->callback_data);
}

static void
dispatch_find_directory_callback (void *wakeup_context)
{
	GnomeVFSFindDirectoryOpResult *notify_result;

	notify_result = (GnomeVFSFindDirectoryOpResult *) wakeup_context;

	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->result_list,
				     notify_result->callback_data);
}

static void
dispatch_set_file_info_callback (void *wakeup_context)
{
	gboolean new_info_is_valid;
	GnomeVFSSetFileInfoOpResult *notify_result;
	
	notify_result = (GnomeVFSSetFileInfoOpResult *) wakeup_context;

	new_info_is_valid = notify_result->set_file_info_result == GNOME_VFS_OK
		&& notify_result->get_file_info_result == GNOME_VFS_OK;
		
	(* notify_result->callback) (notify_result->job_handle,
				     notify_result->set_file_info_result,
				     new_info_is_valid ? &notify_result->info : NULL,
				     notify_result->callback_data);
}

static void
dispatch_xfer_callback (void *wakeup_context)
{
	GnomeVFSXferOpResult *notify_result;

	notify_result = (GnomeVFSXferOpResult *) wakeup_context;

	notify_result->reply = (* notify_result->callback) (notify_result->job_handle,
							    notify_result->progress_info,
						            notify_result->callback_data);
}

static void
close_callback (GnomeVFSAsyncHandle *handle,
		GnomeVFSResult result,
		gpointer callback_data)
{
}

static void
handle_cancelled_open (GnomeVFSJob *job, GnomeVFSOp *op)
{
	gnome_vfs_job_prepare (job, GNOME_VFS_OP_CLOSE,
			       (GFunc) close_callback, NULL);
	gnome_vfs_job_go (job);
}

static void
free_get_file_info_notify_result (GnomeVFSGetFileInfoOpResult *notify_result)
{
	GList *p;
	GnomeVFSGetFileInfoResult *result_item;
	
	for (p = notify_result->result_list; p != NULL; p = p->next) {
		result_item = p->data;

		gnome_vfs_uri_unref (result_item->uri);
		gnome_vfs_file_info_unref (result_item->file_info);
		g_free (result_item);
	}
	g_list_free (notify_result->result_list);
	g_free (notify_result);
}

static void
free_find_directory_notify_result (GnomeVFSFindDirectoryOpResult *notify_result)
{
	GList *p;
	GnomeVFSFindDirectoryResult *result_item;

	for (p = notify_result->result_list; p != NULL; p = p->next) {
		result_item = p->data;

		if (result_item->uri != NULL) {
			gnome_vfs_uri_unref (result_item->uri);
		}
		g_free (result_item);
	}
	g_list_free (notify_result->result_list);
	g_free (notify_result);
}

static void
gnome_vfs_job_destroy_notify_result (guint32 op_type, void *notify_result)
{
	switch (op_type) {
		case GNOME_VFS_OP_CLOSE:
		case GNOME_VFS_OP_CREATE:
		case GNOME_VFS_OP_CREATE_AS_CHANNEL:
		case GNOME_VFS_OP_CREATE_SYMBOLIC_LINK:
		case GNOME_VFS_OP_WRITE:
		case GNOME_VFS_OP_LOAD_DIRECTORY:
		case GNOME_VFS_OP_OPEN:
		case GNOME_VFS_OP_OPEN_AS_CHANNEL:
		case GNOME_VFS_OP_READ:
			g_free (notify_result);
			break;
			
		case GNOME_VFS_OP_FIND_DIRECTORY:
			free_find_directory_notify_result ((GnomeVFSFindDirectoryOpResult *)notify_result);
			break;
			
		case GNOME_VFS_OP_GET_FILE_INFO:
			free_get_file_info_notify_result ((GnomeVFSGetFileInfoOpResult *)notify_result);
			break;
			
		case GNOME_VFS_OP_SET_FILE_INFO:
			gnome_vfs_file_info_clear (&((GnomeVFSSetFileInfoOpResult *) notify_result)->info);
			g_free (notify_result);
			break;
			
		case GNOME_VFS_OP_XFER:
			/* don't destroy anything, xfer is fully synchronous */
			break;
	}
}

static gboolean
dispatch_job_callback (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
	GnomeVFSJob *job;
	GnomeVFSOp *op;
	void *notify_result;
	guint bytes_read;

	job = (GnomeVFSJob *) data;

	JOB_DEBUG (("waiting for channel wakeup %p", job));
	for (;;) {
		g_io_channel_read (job->wakeup_channel_in, (char *)&notify_result, sizeof (void *), &bytes_read);
		if (bytes_read > 0) {
			break;
		}
	}

	g_assert (bytes_read == sizeof (void *));

	JOB_DEBUG (("got channel wakeup %p notify_result %p %d", job, notify_result, bytes_read));

	op = job->notify_op;

	/* The last notify is the one that tells us to go away. */
	if (op == NULL) {
		JOB_DEBUG (("no op left %p", job));
		g_assert (job->current_op == NULL);
		g_assert (!job->want_notify_ack);
		job_ack_notify (job);
		gnome_vfs_job_finish_destroy (job);
		return FALSE;
	}
	
	g_assert (notify_result != NULL);
	
	JOB_DEBUG (("dispatching %p", job));
	/* Do the callback, but not if this operation has been cancelled. */

	if (gnome_vfs_context_check_cancellation (op->context)) {
		switch (op->type) {
		case GNOME_VFS_OP_CREATE:
			if (((GnomeVFSCreateOpResult *) notify_result)->result == GNOME_VFS_OK) {
				handle_cancelled_open (job, op);
			}
			break;
		case GNOME_VFS_OP_OPEN:
			if (((GnomeVFSOpenOpResult *) notify_result)->result == GNOME_VFS_OK) {
				handle_cancelled_open (job, op);
			}
			break;
		case GNOME_VFS_OP_CREATE_AS_CHANNEL:
			if (((GnomeVFSCreateAsChannelOpResult *) notify_result)->result == GNOME_VFS_OK) {
				handle_cancelled_open (job, op);
			}
			break;
		case GNOME_VFS_OP_OPEN_AS_CHANNEL:
			if (((GnomeVFSOpenAsChannelOpResult *) notify_result)->result == GNOME_VFS_OK) {
				handle_cancelled_open (job, op);
			}
			break;
		case GNOME_VFS_OP_CLOSE:
		case GNOME_VFS_OP_CREATE_SYMBOLIC_LINK:
		case GNOME_VFS_OP_FIND_DIRECTORY:
		case GNOME_VFS_OP_GET_FILE_INFO:
		case GNOME_VFS_OP_LOAD_DIRECTORY:
		case GNOME_VFS_OP_READ:
		case GNOME_VFS_OP_SET_FILE_INFO:
		case GNOME_VFS_OP_WRITE:
		case GNOME_VFS_OP_XFER:
			break;
		}
	} else {
		switch (op->type) {
		case GNOME_VFS_OP_CLOSE:
			dispatch_close_callback (notify_result);
			break;
		case GNOME_VFS_OP_CREATE:
			dispatch_create_callback (notify_result);
			break;
		case GNOME_VFS_OP_CREATE_AS_CHANNEL:
			dispatch_create_as_channel_callback (notify_result);
			break;
		case GNOME_VFS_OP_CREATE_SYMBOLIC_LINK:
			dispatch_create_callback (notify_result);
			break;
		case GNOME_VFS_OP_FIND_DIRECTORY:
			dispatch_find_directory_callback (notify_result);
			break;
		case GNOME_VFS_OP_GET_FILE_INFO:
			dispatch_get_file_info_callback (notify_result);
			break;
		case GNOME_VFS_OP_LOAD_DIRECTORY:
			dispatch_load_directory_callback (notify_result);
			break;
		case GNOME_VFS_OP_OPEN:
			dispatch_open_callback (notify_result);
			break;
		case GNOME_VFS_OP_OPEN_AS_CHANNEL:
			dispatch_open_as_channel_callback (notify_result);
			break;
		case GNOME_VFS_OP_READ:
			dispatch_read_callback (notify_result);
			break;
		case GNOME_VFS_OP_SET_FILE_INFO:
			dispatch_set_file_info_callback (notify_result);
			break;
		case GNOME_VFS_OP_WRITE:
			dispatch_write_callback (notify_result);
			break;
		case GNOME_VFS_OP_XFER:
			dispatch_xfer_callback (notify_result);
			break;
		}
	}

	JOB_DEBUG (("dispatch callback - done %p", job));

	gnome_vfs_job_destroy_notify_result (op->type, notify_result);
	gnome_vfs_job_release_notify_op (job);
	job_ack_notify (job);

	return TRUE;
}

GnomeVFSJob *
gnome_vfs_job_new (void)
{
	GnomeVFSJob *new_job;
	gint pipefd[2];
	
	if (pipe (pipefd) != 0) {
		g_warning ("Cannot create pipe for the new GnomeVFSJob: %s",
			   g_strerror (errno));
		return NULL;
	}
	
	new_job = g_new0 (GnomeVFSJob, 1);
	
	new_job->access_lock = g_mutex_new ();
	new_job->execution_condition = g_cond_new ();
	new_job->notify_ack_condition = g_cond_new ();
	new_job->notify_ack_lock = g_mutex_new ();
	
	new_job->is_empty = TRUE;
	
	new_job->wakeup_channel_in = g_io_channel_unix_new (pipefd[0]);
	new_job->wakeup_channel_out = g_io_channel_unix_new (pipefd[1]);
	new_job->wakeup_channel_lock = g_mutex_new ();

	/* Add the new job into the job hash table. This also assigns
	 * the job a unique id
	 */
	gnome_vfs_async_job_map_add_job (new_job);
	
	g_io_add_watch_full (new_job->wakeup_channel_in, G_PRIORITY_HIGH, G_IO_IN,
			     dispatch_job_callback, new_job, NULL);
	
	if (!gnome_vfs_job_create_slave (new_job)) {
		g_warning ("Cannot create job slave.");
		/* FIXME bugzilla.eazel.com 3833: A lot of leaked objects here. */
		g_free (new_job);
		return NULL;
	}
	
	JOB_DEBUG (("new job %p", new_job));

	job_count++;

	return new_job;
}

void
gnome_vfs_job_destroy (GnomeVFSJob *job)
{
	JOB_DEBUG (("job %p", job));

	gnome_vfs_job_release_current_op (job);

	job_oneway_notify (job, NULL);

	JOB_DEBUG (("done %p", job));
	/* We'll finish destroying on the main thread. */
}

static void
gnome_vfs_job_finish_destroy (GnomeVFSJob *job)
{
	g_assert (job->is_empty);

	g_mutex_free (job->access_lock);

	g_cond_free (job->execution_condition);

	g_cond_free (job->notify_ack_condition);
	g_mutex_free (job->notify_ack_lock);

	g_io_channel_close (job->wakeup_channel_in);
	g_io_channel_unref (job->wakeup_channel_in);
	g_io_channel_close (job->wakeup_channel_out);
	g_io_channel_unref (job->wakeup_channel_out);

	g_mutex_free (job->wakeup_channel_lock);

	JOB_DEBUG (("job %p terminated cleanly", job));

	g_free (job);

	job_count--;
}

int
gnome_vfs_job_get_count (void)
{
	return job_count;
}

static void
gnome_vfs_op_destroy (GnomeVFSOp *op)
{
	switch (op->type) {
	case GNOME_VFS_OP_CREATE:
		if (op->specifics.create.uri != NULL) {
			gnome_vfs_uri_unref (op->specifics.create.uri);
		}
		break;
	case GNOME_VFS_OP_CREATE_AS_CHANNEL:
		if (op->specifics.create_as_channel.uri != NULL) {
			gnome_vfs_uri_unref (op->specifics.create_as_channel.uri);
		}
		break;
	case GNOME_VFS_OP_CREATE_SYMBOLIC_LINK:
		gnome_vfs_uri_unref (op->specifics.create_symbolic_link.uri);
		g_free (op->specifics.create_symbolic_link.uri_reference);
		break;
	case GNOME_VFS_OP_FIND_DIRECTORY:
		gnome_vfs_uri_list_free (op->specifics.find_directory.uris);
		break;
	case GNOME_VFS_OP_GET_FILE_INFO:
		gnome_vfs_uri_list_free (op->specifics.get_file_info.uris);
		break;
	case GNOME_VFS_OP_LOAD_DIRECTORY:
		if (op->specifics.load_directory.uri != NULL) {
			gnome_vfs_uri_unref (op->specifics.load_directory.uri);
		}
		g_free (op->specifics.load_directory.sort_rules);
		g_free (op->specifics.load_directory.filter_pattern);
		break;
	case GNOME_VFS_OP_OPEN:
		if (op->specifics.open.uri != NULL) {
			gnome_vfs_uri_unref (op->specifics.open.uri);
		}
		break;
	case GNOME_VFS_OP_OPEN_AS_CHANNEL:
		if (op->specifics.open_as_channel.uri != NULL) {
			gnome_vfs_uri_unref (op->specifics.open_as_channel.uri);
		}
		break;
	case GNOME_VFS_OP_SET_FILE_INFO:
		gnome_vfs_uri_unref (op->specifics.set_file_info.uri);
		break;
	case GNOME_VFS_OP_XFER:
		gnome_vfs_uri_list_free (op->specifics.xfer.source_uri_list);
		gnome_vfs_uri_list_free (op->specifics.xfer.target_uri_list);
		break;
	case GNOME_VFS_OP_CLOSE:
	case GNOME_VFS_OP_READ:
	case GNOME_VFS_OP_WRITE:
		break;
	default:
		g_warning (_("Unknown job ID %d"), op->type);
	}
	
	gnome_vfs_context_unref (op->context);
	g_free (op);
}

static void
gnome_vfs_job_release_current_op (GnomeVFSJob *job)
{
	if (job->current_op == NULL) {
		return;
	}
	if (job->current_op != job->notify_op) {
		gnome_vfs_op_destroy (job->current_op);
	}
	job->current_op = NULL;
}

static void
gnome_vfs_job_release_notify_op (GnomeVFSJob *job)
{
	if (job->current_op != job->notify_op) {
		gnome_vfs_op_destroy (job->notify_op);
	}
	job->notify_op = NULL;
}

void
gnome_vfs_job_prepare (GnomeVFSJob *job,
		       GnomeVFSOpType type,
		       GFunc callback,
		       gpointer callback_data)
{
	GnomeVFSOp *op;

	g_mutex_lock (job->access_lock);

	op = g_new (GnomeVFSOp, 1);
	op->type = type;
	op->callback = callback;
	op->callback_data = callback_data;
	op->context = gnome_vfs_context_new ();

	gnome_vfs_job_release_current_op (job);
	job->current_op = op;
	JOB_DEBUG (("%p %d", job, job->current_op->type));
}

void
gnome_vfs_job_go (GnomeVFSJob *job)
{
	job->is_empty = FALSE;
	g_cond_signal (job->execution_condition);
	g_mutex_unlock (job->access_lock);
}

#define DEFAULT_BUFFER_SIZE 16384

static void
serve_channel_read (GnomeVFSHandle *handle,
		    GIOChannel *channel_in,
		    GIOChannel *channel_out,
		    gulong advised_block_size,
		    GnomeVFSContext *context)
{
	gpointer buffer;
	guint filled_bytes_in_buffer;
	guint written_bytes_in_buffer;
	guint current_buffer_size;
	
	if (advised_block_size == 0) {
		advised_block_size = DEFAULT_BUFFER_SIZE;
	}

	current_buffer_size = advised_block_size;
	buffer = g_malloc(current_buffer_size);
	filled_bytes_in_buffer = 0;
	written_bytes_in_buffer = 0;

	while (1) {
		GnomeVFSResult result;
		GIOError io_result;
		GnomeVFSFileSize bytes_read;
		
	restart_toplevel_loop:
		
		g_assert(filled_bytes_in_buffer <= current_buffer_size);
		g_assert(written_bytes_in_buffer == 0);
		
		result = gnome_vfs_read_cancellable (handle,
						     (char *) buffer + filled_bytes_in_buffer,
						     MIN (advised_block_size, (current_buffer_size - filled_bytes_in_buffer)),
						     &bytes_read, context);

		if (result == GNOME_VFS_ERROR_INTERRUPTED) {
			continue;
		} else if (result != GNOME_VFS_OK) {
			goto end;
		}
	
		filled_bytes_in_buffer += bytes_read;
		
		if (filled_bytes_in_buffer == 0) {
			goto end;
		}
		
		g_assert(written_bytes_in_buffer <= filled_bytes_in_buffer);

		if (gnome_vfs_context_check_cancellation(context)) {
			goto end;
		}

		while (written_bytes_in_buffer < filled_bytes_in_buffer) {
			guint bytes_written;
			
			/* channel_out is nonblocking; if we get
			   EAGAIN (G_IO_ERROR_AGAIN) then we tried to
			   write but the pipe was full. In this case, we
			   want to enlarge our buffer and go back to
			   reading for one iteration, so we can keep
			   collecting data while the main thread is
			   busy. */
			
			io_result = g_io_channel_write (channel_out,
							(char *) buffer + written_bytes_in_buffer,
							filled_bytes_in_buffer - written_bytes_in_buffer,
							&bytes_written);
			
			if (gnome_vfs_context_check_cancellation(context)) {
				goto end;
			}
			
			if (io_result == G_IO_ERROR_AGAIN) {
				/* if bytes_read == 0 then we reached
				   EOF so there's no point reading
				   again. So turn off nonblocking and
				   do a blocking write next time through. */
				if (bytes_read == 0) {
					int fd;

					fd = g_io_channel_unix_get_fd (channel_out);
					
					clr_fl (fd, O_NONBLOCK);
				} else {
					if (written_bytes_in_buffer > 0) {
						/* Need to shift the unwritten bytes
						   to the start of the buffer */
						g_memmove(buffer,
							  (char *) buffer + written_bytes_in_buffer,
							  filled_bytes_in_buffer - written_bytes_in_buffer);
						filled_bytes_in_buffer =
							filled_bytes_in_buffer - written_bytes_in_buffer;
						
						written_bytes_in_buffer = 0;
					}
					
 				        /* If the buffer is more than half
					   full, double its size */
					if (filled_bytes_in_buffer * 2 > current_buffer_size) {
						current_buffer_size *= 2;
						buffer = g_realloc(buffer, current_buffer_size);
					}

					/* Leave this loop, start reading again */
					goto restart_toplevel_loop;

				} /* end of else (bytes_read != 0) */
				
			} else if (io_result != G_IO_ERROR_NONE || bytes_written == 0) {
				goto end;
			}

			written_bytes_in_buffer += bytes_written;
		}

		g_assert(written_bytes_in_buffer == filled_bytes_in_buffer);
		
		/* Reset, we wrote everything */
		written_bytes_in_buffer = 0;
		filled_bytes_in_buffer = 0;
	}

 end:
	g_free (buffer);
	g_io_channel_close (channel_out);
	g_io_channel_unref (channel_out);
	g_io_channel_unref (channel_in);
}

static void
serve_channel_write (GnomeVFSHandle *handle,
		     GIOChannel *channel_in,
		     GIOChannel *channel_out,
		     GnomeVFSContext *context)
{
	gchar buffer[DEFAULT_BUFFER_SIZE];
	guint buffer_size;

	buffer_size = DEFAULT_BUFFER_SIZE;

	while (1) {
		GnomeVFSResult result;
		GIOError io_result;
		guint bytes_read;
		guint bytes_to_write;
		GnomeVFSFileSize bytes_written;
		gchar *p;

		io_result = g_io_channel_read (channel_in, buffer, buffer_size,
					       &bytes_read);
		if (io_result == G_IO_ERROR_AGAIN || io_result == G_IO_ERROR_UNKNOWN)
			/* we will get G_IO_ERROR_UNKNOWN if a signal occurrs */
			continue;
		if (io_result != G_IO_ERROR_NONE || bytes_read == 0)
			goto end;

		p = buffer;
		bytes_to_write = bytes_read;
		while (bytes_to_write > 0) {
			result = gnome_vfs_write_cancellable (handle,
							      p,
							      bytes_to_write,
							      &bytes_written,
							      context);
			if (result == GNOME_VFS_ERROR_INTERRUPTED) {
				continue;
			}
			
			if (result != GNOME_VFS_OK || bytes_written == 0) {
				goto end;
			}

			p += bytes_written;
			bytes_to_write -= bytes_written;
		}
	}

 end:
	g_io_channel_close (channel_in);
	g_io_channel_unref (channel_in);
	g_io_channel_unref (channel_out);
}

/* Job execution.  This is performed by the slave thread.  */

static gboolean
execute_open (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSOpenOp *open_op;
	gboolean notify_retval;
	GnomeVFSOpenOpResult *notify_result;

	open_op = &job->current_op->specifics.open;

	if (open_op->uri == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		result = gnome_vfs_open_uri_cancellable (&handle, open_op->uri,
							 open_op->open_mode,
							 job->current_op->context);
		job->handle = handle;
	}
	
	notify_result = g_new0 (GnomeVFSOpenOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->result = result;
	notify_result->callback = (GnomeVFSAsyncOpenCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	notify_retval = job_oneway_notify_and_close (job, notify_result);

	if (result != GNOME_VFS_OK) {
		return FALSE;
	}

	return notify_retval;
}

static gboolean
execute_open_as_channel (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSOpenAsChannelOp *open_as_channel_op;
	GnomeVFSOpenMode open_mode;
	GIOChannel *channel_in, *channel_out;
	gint pipefd[2];
	GnomeVFSOpenAsChannelOpResult *notify_result;

	open_as_channel_op = &job->current_op->specifics.open_as_channel;

	if (open_as_channel_op->uri == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		result = gnome_vfs_open_uri_cancellable
			(&handle,
			 open_as_channel_op->uri,
			 open_as_channel_op->open_mode,
			 job->current_op->context);
	}

	notify_result = g_new0 (GnomeVFSOpenAsChannelOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->result = result;
	notify_result->callback = (GnomeVFSAsyncOpenAsChannelCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	if (result != GNOME_VFS_OK) {
		job_oneway_notify_and_close (job, notify_result);
		return FALSE;
	}

	if (pipe (pipefd) < 0) {
		g_warning (_("Cannot create pipe for open GIOChannel: %s"),
			   g_strerror (errno));
		notify_result->result = GNOME_VFS_ERROR_INTERNAL;
		job_oneway_notify_and_close (job, notify_result);
		return FALSE;
	}

	/* Set up the pipe for nonblocking writes, so if the main
	 * thread is blocking for some reason the slave can keep
	 * reading data.
	 */
	set_fl (pipefd[1], O_NONBLOCK);
	
	channel_in = g_io_channel_unix_new (pipefd[0]);
	channel_out = g_io_channel_unix_new (pipefd[1]);

	open_mode = open_as_channel_op->open_mode;
	
	if (open_mode & GNOME_VFS_OPEN_READ) {
		notify_result->channel = channel_in;
	} else {
		notify_result->channel = channel_out;
	}

	notify_result->result = GNOME_VFS_OK;

	if (!job_notify (job, notify_result)) {
		return FALSE;
	}

	if (open_mode & GNOME_VFS_OPEN_READ) {
		serve_channel_read (handle, channel_in, channel_out,
				    open_as_channel_op->advised_block_size,
				    job->current_op->context);
	} else {
		serve_channel_write (handle, channel_in, channel_out,
				     job->current_op->context);
	}

	job_close (job);

	return FALSE;
}

static gboolean
execute_create (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSCreateOp *create_op;
	gboolean notify_retval;
	GnomeVFSCreateOpResult *notify_result;

	create_op = &job->current_op->specifics.create;

	if (create_op->uri == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		result = gnome_vfs_create_uri_cancellable
			(&handle,
			 create_op->uri,
			 create_op->open_mode,
			 create_op->exclusive,
			 create_op->perm,
			 job->current_op->context);
		
		job->handle = handle;
	}

	notify_result = g_new0 (GnomeVFSCreateOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->result = result;
	notify_result->callback = (GnomeVFSAsyncCreateCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	notify_retval = job_oneway_notify_and_close (job, notify_result);

	if (result != GNOME_VFS_OK) {
		return FALSE;
	}

	return notify_retval;
}

static gboolean
execute_create_symbolic_link (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSCreateLinkOp *create_op;
	gboolean notify_retval;
	GnomeVFSCreateOpResult *notify_result;

	create_op = &job->current_op->specifics.create_symbolic_link;

	result = gnome_vfs_create_symbolic_link_cancellable
		(create_op->uri,
		 create_op->uri_reference,
		 job->current_op->context);

	notify_result = g_new0 (GnomeVFSCreateOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->result = result;
	notify_result->callback = (GnomeVFSAsyncCreateCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	notify_retval = job_oneway_notify_and_close (job, notify_result);

	if (result != GNOME_VFS_OK) {
		return FALSE;
	}

	return notify_retval;
}
	
static gboolean
execute_create_as_channel (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSCreateAsChannelOp *create_as_channel_op;
	GIOChannel *channel_in, *channel_out;
	gint pipefd[2];
	GnomeVFSCreateAsChannelOpResult *notify_result;

	create_as_channel_op = &job->current_op->specifics.create_as_channel;

	if (create_as_channel_op->uri == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		result = gnome_vfs_open_uri_cancellable
			(&handle,
			 create_as_channel_op->uri,
			 create_as_channel_op->open_mode,
			 job->current_op->context);
	}
	
	notify_result = g_new0 (GnomeVFSCreateAsChannelOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->result = result;
	notify_result->callback = (GnomeVFSAsyncCreateAsChannelCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	if (result != GNOME_VFS_OK) {
		job_oneway_notify_and_close (job, notify_result);
		return FALSE;
	}

	if (pipe (pipefd) < 0) {
		g_warning (_("Cannot create pipe for open GIOChannel: %s"),
			   g_strerror (errno));
		notify_result->result = GNOME_VFS_ERROR_INTERNAL;
		job_oneway_notify_and_close (job, notify_result);
		return FALSE;
	}
	
	channel_in = g_io_channel_unix_new (pipefd[0]);
	channel_out = g_io_channel_unix_new (pipefd[1]);

	notify_result->channel = channel_out;

	if (!job_notify (job, notify_result)) {
		return FALSE;
	}

	serve_channel_write (handle, channel_in, channel_out, job->current_op->context);

	job_close (job);

	return FALSE;
}

static gboolean
execute_close (GnomeVFSJob *job)
{
	GnomeVFSCloseOp *close_op;
	GnomeVFSCloseOpResult *notify_result;

	close_op = &job->current_op->specifics.close;

	notify_result = g_new0 (GnomeVFSCloseOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->callback = (GnomeVFSAsyncCloseCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;
	notify_result->result
		= gnome_vfs_close_cancellable (job->handle, job->current_op->context);

	job_notify_and_close (job, notify_result);

	return FALSE;
}

static gboolean
execute_read (GnomeVFSJob *job)
{
	GnomeVFSReadOp *read_op;
	GnomeVFSReadOpResult *notify_result;
	
	read_op = &job->current_op->specifics.read;

	notify_result = g_new0 (GnomeVFSReadOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->callback = (GnomeVFSAsyncReadCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;
	notify_result->buffer = read_op->buffer;
	notify_result->num_bytes = read_op->num_bytes;
	
	notify_result->result = gnome_vfs_read_cancellable (job->handle,
							    read_op->buffer,
							    read_op->num_bytes,
							    &notify_result->bytes_read,
							    job->current_op->context);


	return job_oneway_notify_and_close (job, notify_result);
}

static gboolean
execute_write (GnomeVFSJob *job)
{
	GnomeVFSWriteOp *write_op;
	GnomeVFSWriteOpResult *notify_result;
	
	write_op = &job->current_op->specifics.write;

	notify_result = g_new0 (GnomeVFSWriteOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->callback = (GnomeVFSAsyncWriteCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;
	notify_result->buffer = write_op->buffer;
	notify_result->num_bytes = write_op->num_bytes;

	notify_result->result = gnome_vfs_write_cancellable (job->handle,
							     write_op->buffer,
							     write_op->num_bytes,
							     &notify_result->bytes_written,
							     job->current_op->context);


	return job_oneway_notify_and_close (job, notify_result);
}

static gboolean
execute_load_directory_not_sorted (GnomeVFSJob *job,
				   GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSLoadDirectoryOp *load_directory_op;
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSDirectoryList *directory_list;
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	guint count;
	GnomeVFSLoadDirectoryOpResult *notify_result;

	JOB_DEBUG (("%p", job));
	load_directory_op = &job->current_op->specifics.load_directory;
	
	if (load_directory_op->uri == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		result = gnome_vfs_directory_open_from_uri
			(&handle,
			 load_directory_op->uri,
			 load_directory_op->options,
			 filter);
	}

	if (result != GNOME_VFS_OK) {
		notify_result = g_new0 (GnomeVFSLoadDirectoryOpResult, 1);
		notify_result->job_handle = job->job_handle;
		notify_result->result = result;
		notify_result->callback = (GnomeVFSAsyncDirectoryLoadCallback) job->current_op->callback;
		notify_result->callback_data = job->current_op->callback_data;
		job_notify_and_close (job, notify_result);
		return FALSE;
	}

	directory_list = gnome_vfs_directory_list_new ();

	count = 0;
	while (1) {
		if (gnome_vfs_context_check_cancellation (job->current_op->context)) {
			JOB_DEBUG (("cancelled, bailing %p", job));
			result = GNOME_VFS_ERROR_CANCELLED;
			break;
		}
		
		info = gnome_vfs_file_info_new ();

		result = gnome_vfs_directory_read_next (handle, info);

		if (result == GNOME_VFS_OK) {
			gnome_vfs_directory_list_append (directory_list, info);
			count++;
		} else {
			gnome_vfs_file_info_unref (info);
		}

		if (count == load_directory_op->items_per_notification
			|| result != GNOME_VFS_OK) {

			notify_result = g_new0 (GnomeVFSLoadDirectoryOpResult, 1);
			notify_result->job_handle = job->job_handle;
			notify_result->result = result;
			notify_result->entries_read = count;
			notify_result->list = directory_list;
			notify_result->callback = (GnomeVFSAsyncDirectoryLoadCallback) job->current_op->callback;
			notify_result->callback_data = job->current_op->callback_data;

			/* If we have not set a position yet, it means this is
                           the first iteration, so we must position on the
                           first element.  Otherwise, the last time we got here
                           we positioned on the last element with
                           `gnome_vfs_directory_list_last()', so we have to go
                           to the next one.  */
			if (gnome_vfs_directory_list_get_position
			    (directory_list) == NULL) {
				gnome_vfs_directory_list_first (directory_list);
			} else {
				gnome_vfs_directory_list_next (directory_list);
			}

			if (!job_notify (job, notify_result)) {
				break;
			}
			

			if (result != GNOME_VFS_OK) {
				break;
			}

			count = 0;
			gnome_vfs_directory_list_last (directory_list);
		}
	}

	gnome_vfs_directory_list_destroy (directory_list);
	gnome_vfs_directory_close (handle);

	job_close (job);

	return FALSE;
}

static gboolean
execute_load_directory_sorted (GnomeVFSJob *job,
			       GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSLoadDirectoryOp *load_directory_op;
	GnomeVFSDirectoryList *directory_list;
	GnomeVFSDirectoryListPosition previous_p, p;
	GnomeVFSResult result;
	guint count;
	GnomeVFSLoadDirectoryOpResult *notify_result;

	JOB_DEBUG (("%p", job));
	load_directory_op = &job->current_op->specifics.load_directory;
	
	if (load_directory_op->uri == NULL) {
		result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		result = gnome_vfs_directory_list_load_from_uri
			(&directory_list,
			 load_directory_op->uri,
			 load_directory_op->options,
			 filter);
	}


	if (result != GNOME_VFS_OK) {
		notify_result = g_new0 (GnomeVFSLoadDirectoryOpResult, 1);
		notify_result->job_handle = job->job_handle;
		notify_result->result = result;
		notify_result->callback = (GnomeVFSAsyncDirectoryLoadCallback) job->current_op->callback;
		notify_result->callback_data = job->current_op->callback_data;
		job_notify (job, notify_result);
		return FALSE;
	}

	gnome_vfs_directory_list_sort
		(directory_list,
		 load_directory_op->reverse_order,
		 load_directory_op->sort_rules);

	p = gnome_vfs_directory_list_get_first_position (directory_list);

	if (p == NULL) {
		notify_result = g_new0 (GnomeVFSLoadDirectoryOpResult, 1);
		notify_result->job_handle = job->job_handle;
		notify_result->result = GNOME_VFS_ERROR_EOF;
		notify_result->callback = (GnomeVFSAsyncDirectoryLoadCallback) job->current_op->callback;
		notify_result->callback_data = job->current_op->callback_data;
		job_notify (job, notify_result);
		return FALSE;
	}

	count = 0;
	previous_p = p;
	while (p != NULL) {
		count++;
		p = gnome_vfs_directory_list_position_next (p);
		if (p == NULL
		    || count == load_directory_op->items_per_notification) {
			gnome_vfs_directory_list_set_position (directory_list,
							       previous_p);

			notify_result = g_new0 (GnomeVFSLoadDirectoryOpResult, 1);
			notify_result->job_handle = job->job_handle;
			notify_result->entries_read = count;
			notify_result->list = directory_list;
			notify_result->callback = (GnomeVFSAsyncDirectoryLoadCallback) job->current_op->callback;
			notify_result->callback_data = job->current_op->callback_data;

			if (p == NULL) {
				notify_result->result = GNOME_VFS_ERROR_EOF;
			} else {
				notify_result->result = GNOME_VFS_OK;
			}
			if (!job_notify (job, notify_result)) {
				break;
			}
			count = 0;
			previous_p = p;
		}
	}

	return FALSE;
}

static gboolean
execute_get_file_info (GnomeVFSJob *job)
{
	GnomeVFSGetFileInfoOp *get_file_info_op;
	GList *p;
	GnomeVFSGetFileInfoResult *result_item;
	GnomeVFSGetFileInfoOpResult *notify_result;

	get_file_info_op = &job->current_op->specifics.get_file_info;

	notify_result = g_new0 (GnomeVFSGetFileInfoOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->callback = (GnomeVFSAsyncGetFileInfoCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	for (p = get_file_info_op->uris; p != NULL; p = p->next) {
		result_item = g_new (GnomeVFSGetFileInfoResult, 1);

		result_item->uri = gnome_vfs_uri_ref (p->data);
		result_item->file_info = gnome_vfs_file_info_new ();

		result_item->result = gnome_vfs_get_file_info_uri_cancellable
			(result_item->uri,
			 result_item->file_info,
			 get_file_info_op->options,
			 job->current_op->context);

		notify_result->result_list = g_list_prepend (notify_result->result_list, result_item);
	}
	notify_result->result_list = g_list_reverse (notify_result->result_list);

	job_oneway_notify_and_close (job, notify_result);
	return FALSE;
}

static gboolean
execute_set_file_info (GnomeVFSJob *job)
{
	GnomeVFSSetFileInfoOp *set_file_info_op;
	GnomeVFSURI *parent_uri, *uri_after;
	GnomeVFSSetFileInfoOpResult *notify_result;

	set_file_info_op = &job->current_op->specifics.set_file_info;

	notify_result = g_new0 (GnomeVFSSetFileInfoOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->callback = (GnomeVFSAsyncSetFileInfoCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	notify_result->set_file_info_result =
		gnome_vfs_set_file_info_cancellable (set_file_info_op->uri,
			&set_file_info_op->info, set_file_info_op->mask,
		 	job->current_op->context);

	/* Get the new URI after the set_file_info. The name may have
	 * changed.
	 */
	uri_after = NULL;
	if (notify_result->set_file_info_result == GNOME_VFS_OK
	    && (set_file_info_op->mask & GNOME_VFS_SET_FILE_INFO_NAME) != 0) {
		parent_uri = gnome_vfs_uri_get_parent (set_file_info_op->uri);
		if (parent_uri != NULL) {
			uri_after = gnome_vfs_uri_append_file_name
				(parent_uri, set_file_info_op->info.name);
			gnome_vfs_uri_unref (parent_uri);
		}
	}
	if (uri_after == NULL) {
		uri_after = set_file_info_op->uri;
		gnome_vfs_uri_ref (uri_after);
	}

	/* Always get new file info, even if setter failed. Init here
	 * and clear in dispatch_set_file_info.
	 */
	gnome_vfs_file_info_init (&notify_result->info);
	if (uri_after == NULL) {
		notify_result->get_file_info_result = GNOME_VFS_ERROR_INVALID_URI;
	} else {
		notify_result->get_file_info_result
			= gnome_vfs_get_file_info_uri_cancellable
			(uri_after,
			 &notify_result->info,
			 set_file_info_op->options,
			 job->current_op->context);
		gnome_vfs_uri_unref (uri_after);
	}

	job_oneway_notify_and_close (job, notify_result);
	return FALSE;
}

static gboolean
execute_find_directory (GnomeVFSJob *job)
{
	GnomeVFSFindDirectoryOp *find_directory_op;
	GList *p;
	GnomeVFSGetFileInfoResult *result_item;
	GnomeVFSFindDirectoryOpResult *notify_result;

	notify_result = g_new0 (GnomeVFSFindDirectoryOpResult, 1);
	notify_result->job_handle = job->job_handle;
	notify_result->callback = (GnomeVFSAsyncFindDirectoryCallback) job->current_op->callback;
	notify_result->callback_data = job->current_op->callback_data;

	find_directory_op = &job->current_op->specifics.find_directory;
	for (p = find_directory_op->uris; p != NULL; p = p->next) {
		result_item = g_new0 (GnomeVFSGetFileInfoResult, 1);

		result_item->result = gnome_vfs_find_directory_cancellable
			((GnomeVFSURI *) p->data,
			 find_directory_op->kind,
			 &result_item->uri,
			 find_directory_op->create_if_needed,
			 find_directory_op->find_if_needed,
			 find_directory_op->permissions,
			 job->current_op->context);
		notify_result->result_list =
			g_list_prepend (notify_result->result_list, result_item);
	}

	notify_result->result_list = g_list_reverse (notify_result->result_list);
	
	job_oneway_notify_and_close (job, notify_result);
	return FALSE;
}

static gboolean
execute_load_directory (GnomeVFSJob *job)
{
	GnomeVFSLoadDirectoryOp *load_directory_op;
	GnomeVFSDirectorySortRule *sort_rules;
	GnomeVFSDirectoryFilter *filter;
	gboolean retval;

	load_directory_op = &job->current_op->specifics.load_directory;

	filter = gnome_vfs_directory_filter_new
		(load_directory_op->filter_type,
		 load_directory_op->filter_options,
		 load_directory_op->filter_pattern);

	sort_rules = load_directory_op->sort_rules;
	if (sort_rules == NULL || sort_rules[0] == GNOME_VFS_DIRECTORY_SORT_NONE)
		retval = execute_load_directory_not_sorted (job, filter);
	else
		retval = execute_load_directory_sorted (job, filter);

	gnome_vfs_directory_filter_destroy (filter);

	job_close (job);

	return FALSE;
}

static gint
xfer_callback (GnomeVFSXferProgressInfo *info,
	       gpointer data)
{
	GnomeVFSJob *job;
	GnomeVFSXferOpResult notify_result;

	job = (GnomeVFSJob *) data;

	/* xfer is fully synchronous, just allocate the notify result struct on the stack */
	notify_result.job_handle = job->job_handle;
	notify_result.progress_info = info;
	notify_result.callback = (GnomeVFSAsyncXferProgressCallback) job->current_op->callback;
	notify_result.callback_data = job->current_op->callback_data;

	job_notify (job, &notify_result);

	/* Pass the value returned from the callback in the master thread.  */
	return notify_result.reply;
}

static gboolean
execute_xfer (GnomeVFSJob *job)
{
	GnomeVFSXferOp *xfer_op;
	GnomeVFSResult result;
	GnomeVFSXferProgressInfo info;
	GnomeVFSXferOpResult notify_result;

	xfer_op = &job->current_op->specifics.xfer;

	result = gnome_vfs_xfer_private (xfer_op->source_uri_list,
					 xfer_op->target_uri_list,
					 xfer_op->xfer_options,
					 xfer_op->error_mode,
					 xfer_op->overwrite_mode,
					 xfer_callback,
					 job,
					 xfer_op->progress_sync_callback,
					 xfer_op->sync_callback_data);

	/* If the xfer functions returns an error now, something really bad
           must have happened.  */
	if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_INTERRUPTED) {


		info.status = GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR;
		info.vfs_status = result;
		info.phase = GNOME_VFS_XFER_PHASE_INITIAL;
		info.source_name = NULL;
		info.target_name = NULL;
		info.file_index = 0;
		info.files_total = 0;
		info.bytes_total = 0;
		info.file_size = 0;
		info.bytes_copied = 0;
		info.total_bytes_copied = 0;

		notify_result.job_handle = job->job_handle;
		notify_result.progress_info = &info;
		notify_result.callback = (GnomeVFSAsyncXferProgressCallback) job->current_op->callback;
		notify_result.callback_data = job->current_op->callback_data;

		job_notify (job, &notify_result);
	}

	job_close (job);

	return FALSE;
}

/* This function is called by the slave thread to execute a
   GnomeVFSJob.  */
gboolean
gnome_vfs_job_execute (GnomeVFSJob *job)
{
	JOB_DEBUG (("locking access_lock %p", job));
	g_mutex_lock (job->access_lock);
	if (job->is_empty) {
		JOB_DEBUG (("waiting for execution condition %p", job));
		g_cond_wait (job->execution_condition, job->access_lock);
	}

	JOB_DEBUG (("executing %p %d", job, job->current_op->type));

	switch (job->current_op->type) {
	case GNOME_VFS_OP_OPEN:
		return execute_open (job);
	case GNOME_VFS_OP_OPEN_AS_CHANNEL:
		return execute_open_as_channel (job);
	case GNOME_VFS_OP_CREATE:
		return execute_create (job);
	case GNOME_VFS_OP_CREATE_AS_CHANNEL:
		return execute_create_as_channel (job);
	case GNOME_VFS_OP_CREATE_SYMBOLIC_LINK:
		return execute_create_symbolic_link (job);
	case GNOME_VFS_OP_CLOSE:
		return execute_close (job);
	case GNOME_VFS_OP_READ:
		return execute_read (job);
	case GNOME_VFS_OP_WRITE:
		return execute_write (job);
	case GNOME_VFS_OP_LOAD_DIRECTORY:
		return execute_load_directory (job);
	case GNOME_VFS_OP_FIND_DIRECTORY:
		return execute_find_directory (job);
	case GNOME_VFS_OP_XFER:
		return execute_xfer (job);
	case GNOME_VFS_OP_GET_FILE_INFO:
		return execute_get_file_info (job);
	case GNOME_VFS_OP_SET_FILE_INFO:
		return execute_set_file_info (job);
	default:
		g_warning (_("Unknown job ID %d"), job->current_op->type);
		return FALSE;
	}
}

void
gnome_vfs_job_cancel (GnomeVFSJob *job)
{
	GnomeVFSOp *op;
	GnomeVFSCancellation *cancellation;

	JOB_DEBUG (("async cancel %p", job));

	g_return_if_fail (job != NULL);

	op = job->current_op;
	if (op == NULL) {
		op = job->notify_op;
	}

	g_return_if_fail (op != NULL);

	cancellation = gnome_vfs_context_get_cancellation (op->context);
	if (cancellation != NULL) {
		JOB_DEBUG (("cancelling %p", job));
		gnome_vfs_cancellation_cancel (cancellation);
	}

	/* handle the case when the job is stuck waiting in job_notify */
	JOB_DEBUG (("unlock job_notify %p", job));
	job_signal_ack_condition (job);
	
	gnome_vfs_context_emit_message (op->context, _("Operation stopped"));

	/* Since we are cancelling, we won't have anyone respond to notifications;
	 * set the expectations right.
	 */
	JOB_DEBUG (("done cancelling %p", job));
}

