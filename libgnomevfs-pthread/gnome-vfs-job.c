/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-job.c - Jobs for asynchronous operation of the GNOME
   Virtual File System (version for POSIX threads).

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

   Author: Ettore Perazzoli <ettore@gnu.org> */

/* FIXME the slave threads do not die properly.  */
/* FIXME check that all the data is freed properly //in the callback dispatching
   functions//.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-job.h"

#if 0
#include <stdio.h>
#define JOB_DEBUG(x)				\
G_STMT_START{					\
	fputs (__FUNCTION__ ": ", stdout);	\
	printf x;				\
	fputc ('\n', stdout);			\
	fflush (stdout);			\
}G_STMT_END
#else
#define JOB_DEBUG(x)
#endif


/* Stevens functions */
static void
set_fl(int fd, int flags)
{
	int val;

	if ( (val = fcntl(fd, F_GETFL, 0)) < 0 ) {
		g_warning("fcntl() F_GETFL failed: %s", strerror(errno));
		return;
	}

	val |= flags;
	
	if ( (val = fcntl(fd, F_SETFL, val)) < 0 ) {
		g_warning("fcntl() F_SETFL failed: %s", strerror(errno));
		return;
	}		
}

static void
clr_fl(int fd, int flags)
{
	int val;

	if ( (val = fcntl(fd, F_GETFL, 0)) < 0 ) {
		g_warning("fcntl() F_GETFL failed: %s", strerror(errno));
		return;
	}

	val &= ~flags;
	
	if ( (val = fcntl(fd, F_SETFL, val)) < 0 ) {
		g_warning("fcntl() F_SETFL failed: %s", strerror(errno));
		return;
	}		
}


/* This is used by the master thread to notify the slave thread that it got the
   notification.  */
static void
job_ack_notify (GnomeVFSJob *job)
{
	JOB_DEBUG (("Checking if ack is needed."));
	if (job->want_notify_ack) {
		JOB_DEBUG (("Ack needed: lock notify ack."));
		g_mutex_lock (job->notify_ack_lock);
		JOB_DEBUG (("Ack needed: signaling condition."));
		g_cond_signal (job->notify_ack_condition);
		JOB_DEBUG (("Ack needed: unlocking notify ack."));
		g_mutex_unlock (job->notify_ack_lock);
	}

	JOB_DEBUG (("unlocking wakeup channel."));
	g_mutex_unlock (job->wakeup_channel_lock);
}

static gboolean
wakeup (GnomeVFSJob *job)
{
	gboolean retval;
	guint bytes_written;

	JOB_DEBUG (("Wake up!"));

	/* Wake up the main thread.  */
	g_io_channel_write (job->wakeup_channel_out, "a", 1, &bytes_written);
	if (bytes_written != 1) {
		g_warning (_("Error writing to the wakeup GnomeVFSJob channel."));
		retval = FALSE;
	} else {
		retval = TRUE;
	}

	return retval;
}

/* This notifies the master thread asynchronously, without waiting for an
   acknowledgment.  */
static gboolean
job_oneway_notify (GnomeVFSJob *job)
{
	JOB_DEBUG (("lock channel"));
	g_mutex_lock (job->wakeup_channel_lock);

	return wakeup (job);
}

/* This notifies the master threads, waiting until it acknowledges the
   notification.  */
static gboolean
job_notify (GnomeVFSJob *job)
{
	gboolean retval;

	JOB_DEBUG (("Locking wakeup channel"));
	g_mutex_lock (job->wakeup_channel_lock);

	JOB_DEBUG (("Locking notification lock"));
	/* Lock notification, so that the master cannot send the signal until
           we are ready to receive it.  */
	g_mutex_lock (job->notify_ack_lock);

	job->want_notify_ack = TRUE;

	/* Send the notification.  This will wake up the master thread, which
           will in turn signal the notify condition.  */
	retval = wakeup (job);

	JOB_DEBUG (("Wait notify condition"));
	/* Wait for the notify condition.  */
	g_cond_wait (job->notify_ack_condition, job->notify_ack_lock);
	job->want_notify_ack = FALSE;

	JOB_DEBUG (("Unlock notify ack lock"));
	/* Acknowledgment got: unlock the mutex.  */
	g_mutex_unlock (job->notify_ack_lock);

	JOB_DEBUG (("Done"));
	return retval;
}

/* This closes the job.  */
static void
job_close (GnomeVFSJob *job)
{
	job->is_empty = TRUE;
	JOB_DEBUG (("Unlocking access lock"));
	g_mutex_unlock (job->access_lock);
}

static gboolean
job_oneway_notify_and_close (GnomeVFSJob *job)
{
	gboolean retval;

	retval = job_oneway_notify (job);
	job_close (job);

	return retval;
}

static gboolean
job_notify_and_close (GnomeVFSJob *job)
{
	gboolean retval;

	retval = job_notify (job);
	job_close (job);

	return retval;
}


static void
dispatch_open_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncOpenCallback callback;
	GnomeVFSOpenJob *open_job;

	open_job = &job->info.open;

	gnome_vfs_uri_unref (open_job->request.uri);

	callback = (GnomeVFSAsyncOpenCallback) job->callback;
	(* callback) ((GnomeVFSAsyncHandle *) job,
		      open_job->notify.result,
		      job->callback_data);
}

static void
dispatch_create_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncCreateCallback callback;
	GnomeVFSCreateJob *create_job;

	create_job = &job->info.create;

	gnome_vfs_uri_unref (create_job->request.uri);

	callback = (GnomeVFSAsyncCreateCallback) job->callback;
	(* callback) ((GnomeVFSAsyncHandle *) job,
		      create_job->notify.result,
		      job->callback_data);
}

static void
dispatch_open_as_channel_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncOpenAsChannelCallback callback;
	GnomeVFSOpenAsChannelJob *open_as_channel_job;

	open_as_channel_job = &job->info.open_as_channel;
	gnome_vfs_uri_unref (open_as_channel_job->request.uri);

	callback = (GnomeVFSAsyncOpenAsChannelCallback) job->callback;
	(* callback) ((GnomeVFSAsyncHandle *) job,
		      open_as_channel_job->notify.channel,
		      open_as_channel_job->notify.result,
		      job->callback_data);
}

static void
dispatch_create_as_channel_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncCreateAsChannelCallback callback;
	GnomeVFSCreateAsChannelJob *create_as_channel_job;

	create_as_channel_job = &job->info.create_as_channel;

	gnome_vfs_uri_unref (create_as_channel_job->request.uri);

	callback = (GnomeVFSAsyncCreateAsChannelCallback) job->callback;
	(* callback) ((GnomeVFSAsyncHandle *) job,
		      create_as_channel_job->notify.channel,
		      create_as_channel_job->notify.result,
		      job->callback_data);
}

static void
dispatch_close_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncCloseCallback callback;
	GnomeVFSCloseJob *close_job;

	close_job = &job->info.close;

	callback = (GnomeVFSAsyncCloseCallback) job->callback;
	(* callback) ((GnomeVFSAsyncHandle *) job,
		      close_job->notify.result,
		      job->callback_data);

	gnome_vfs_job_destroy (job);
}

static void
dispatch_read_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncReadCallback callback;
	GnomeVFSReadJob *read_job;

	callback = (GnomeVFSAsyncReadCallback) job->callback;

	read_job = &job->info.read;

	(* callback) ((GnomeVFSAsyncHandle *) job,
		      read_job->notify.result,
		      read_job->request.buffer,
		      read_job->request.num_bytes,
		      read_job->notify.bytes_read,
		      job->callback_data);
}

static void
dispatch_write_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncWriteCallback callback;
	GnomeVFSWriteJob *write_job;

	callback = (GnomeVFSAsyncWriteCallback) job->callback;

	write_job = &job->info.write;

	(* callback) ((GnomeVFSAsyncHandle *) job,
		      write_job->notify.result,
		      write_job->request.buffer,
		      write_job->request.num_bytes,
		      write_job->notify.bytes_written,
		      job->callback_data);
}

static void
dispatch_load_directory_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncDirectoryLoadCallback callback;
	GnomeVFSLoadDirectoryJob *load_directory_job;

	load_directory_job = &job->info.load_directory;

	if (load_directory_job->notify.result != GNOME_VFS_OK)
		gnome_vfs_uri_unref (load_directory_job->request.uri);

	callback = (GnomeVFSAsyncDirectoryLoadCallback) job->callback;
	(* callback) ((GnomeVFSAsyncHandle *) job,
		      load_directory_job->notify.result,
		      load_directory_job->notify.list,
		      load_directory_job->notify.entries_read,
		      job->callback_data);
}

static void
dispatch_get_file_info_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncGetFileInfoCallback callback = (GnomeVFSAsyncGetFileInfoCallback)job->callback;

	(* callback) ((GnomeVFSAsyncHandle *) job,
		      job->info.get_file_info.notify.result,
		      job->info.get_file_info.notify.file_info,
		      job->callback_data);
	gnome_vfs_file_info_unref(job->info.get_file_info.notify.file_info);
}

static void
dispatch_xfer_callback (GnomeVFSJob *job)
{
	GnomeVFSAsyncXferProgressCallback callback;
	GnomeVFSXferJob *xfer_job;
	gint callback_retval;

	callback = (GnomeVFSAsyncXferProgressCallback) job->callback;

	xfer_job = &job->info.xfer;

	callback_retval = (* callback) ((GnomeVFSAsyncHandle *) job,
					xfer_job->notify.progress_info,
					job->callback_data);

	xfer_job->notify_answer.value = callback_retval;
}

static gboolean
dispatch_job_callback (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
	GnomeVFSJob *job;
	gchar c;
	guint bytes_read;
	gboolean retval;

	job = (GnomeVFSJob *) data;

	g_io_channel_read (job->wakeup_channel_in, &c, 1, &bytes_read);

	/* First check if we are being cancelled.  If so, we have to kill the
           job (i.e. return FALSE) and we are done.  */
	if (gnome_vfs_cancellation_check (gnome_vfs_context_get_cancellation(job->context))) {
		job_ack_notify (job);
		return FALSE;
	}

	switch (job->type) {
	case GNOME_VFS_JOB_OPEN:
		dispatch_open_callback (job);
		break;
	case GNOME_VFS_JOB_OPEN_AS_CHANNEL:
		dispatch_open_as_channel_callback (job);
		break;
	case GNOME_VFS_JOB_CREATE:
		dispatch_create_callback (job);
		break;
	case GNOME_VFS_JOB_CREATE_AS_CHANNEL:
		dispatch_create_as_channel_callback (job);
		break;
	case GNOME_VFS_JOB_CLOSE:
		dispatch_close_callback (job);
		break;
	case GNOME_VFS_JOB_READ:
		dispatch_read_callback (job);
		break;
	case GNOME_VFS_JOB_WRITE:
		dispatch_write_callback (job);
		break;
	case GNOME_VFS_JOB_LOAD_DIRECTORY:
		dispatch_load_directory_callback (job);
		break;
	case GNOME_VFS_JOB_XFER:
		dispatch_xfer_callback (job);
		break;
	case GNOME_VFS_JOB_GET_FILE_INFO:
		dispatch_get_file_info_callback (job);
		break;
	default:
		g_warning (_("Unknown job ID %d"), job->type);
		retval = FALSE;
	}

	job_ack_notify (job);
	return TRUE;
}


GnomeVFSJob *
gnome_vfs_job_new (void)
{
	GnomeVFSJobSlave *slave;
	GnomeVFSJob *new;
	gint pipefd[2];
	gchar c;
	guint bytes_read;

	if (pipe (pipefd) != 0) {
		g_warning ("Cannot create pipe for the new GnomeVFSJob: %s",
			   g_strerror (errno));
		return NULL;
	}

	new = g_new (GnomeVFSJob, 1);

	new->handle = NULL;

	new->access_lock = g_mutex_new ();
	new->execution_condition = g_cond_new ();
	new->notify_ack_condition = g_cond_new ();
	new->notify_ack_lock = g_mutex_new ();

	new->is_empty = TRUE;

	new->wakeup_channel_in = g_io_channel_unix_new (pipefd[0]);
	new->wakeup_channel_out = g_io_channel_unix_new (pipefd[1]);
	new->wakeup_channel_lock = g_mutex_new ();

	new->context = gnome_vfs_context_new ();

	g_io_add_watch_full (new->wakeup_channel_in, G_PRIORITY_LOW, G_IO_IN,
			     dispatch_job_callback, new, NULL);

	slave = gnome_vfs_job_slave_new (new);
	if (slave == NULL) {
		g_warning ("Cannot create job slave.");
		g_free (new);
		return NULL;
	}

	new->slave = slave;

	/* Wait for the thread to come up.  */
	g_io_channel_read (new->wakeup_channel_in, &c, 1, &bytes_read);

	return new;
}

/* WARNING: This might fail if the job is being executed.  */
gboolean
gnome_vfs_job_destroy (GnomeVFSJob *job)
{
	if (! g_mutex_trylock (job->access_lock))
		return FALSE;

	if (! job->is_empty) {
		g_mutex_unlock (job->access_lock);
		return FALSE;
	}

	g_mutex_unlock (job->access_lock);

	gnome_vfs_job_slave_cancel (job->slave);

	g_mutex_free (job->access_lock);

	/* This needs to be fixed.  Basically, we might still have the slave
           thread waiting on this condition, and we cannot just free it
	   in this case.  */
	/*  g_cond_free (job->execution_condition); FIXME */

	g_cond_free (job->notify_ack_condition);
	g_mutex_free (job->notify_ack_lock);

	g_io_channel_close (job->wakeup_channel_in);
	g_io_channel_unref (job->wakeup_channel_in);
	g_io_channel_close (job->wakeup_channel_out);
	g_io_channel_unref (job->wakeup_channel_out);

	g_mutex_free (job->wakeup_channel_lock);

	gnome_vfs_context_unref (job->context);

	g_free (job);

	return TRUE;
}


void
gnome_vfs_job_prepare (GnomeVFSJob *job)
{
	g_mutex_lock (job->access_lock);
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
	
	if (advised_block_size == 0)
		advised_block_size = DEFAULT_BUFFER_SIZE;

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
						     buffer + filled_bytes_in_buffer,
						     MIN(advised_block_size, (current_buffer_size - filled_bytes_in_buffer)),
						     &bytes_read, context);

		if (result == GNOME_VFS_ERROR_CANCELLED)
			goto end;
		else if (result == GNOME_VFS_ERROR_INTERRUPTED)
			continue;
		else if (result != GNOME_VFS_OK)
			goto end;
		
		filled_bytes_in_buffer += bytes_read;
		
		if (filled_bytes_in_buffer == 0)
			goto end;

		g_assert(written_bytes_in_buffer <= filled_bytes_in_buffer);

		if (gnome_vfs_context_check_cancellation(context))
			goto end;
		
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
							buffer + written_bytes_in_buffer,
							filled_bytes_in_buffer - written_bytes_in_buffer,							
							&bytes_written);

			if (gnome_vfs_context_check_cancellation(context))
				goto end;
			
			if (io_result == G_IO_ERROR_AGAIN) {
				/* if bytes_read == 0 then we reached
				   EOF so there's no point reading
				   again. So turn off nonblocking and
				   do a blocking write next time through. */
				if (bytes_read == 0) {
					int fd;

					fd = g_io_channel_unix_get_fd(channel_out);
					
					clr_fl(fd, O_NONBLOCK);
				} else {
					if (written_bytes_in_buffer > 0) {
						/* Need to shift the unwritten bytes
						   to the start of the buffer */
						g_memmove(buffer,
							  buffer + written_bytes_in_buffer,
							  filled_bytes_in_buffer - written_bytes_in_buffer);
						filled_bytes_in_buffer =
							filled_bytes_in_buffer - written_bytes_in_buffer;
						
						written_bytes_in_buffer = 0;
					}
					
 				        /* If the buffer is more than half
					   full, double its size */
					if (filled_bytes_in_buffer*2 > current_buffer_size) {
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
	g_free(buffer);
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
	gpointer buffer;
	guint buffer_size;

	buffer_size = DEFAULT_BUFFER_SIZE;
	buffer = alloca (buffer_size);

	while (1) {
		GnomeVFSResult result;
		GIOError io_result;
		guint bytes_read;
		guint bytes_to_write;
		GnomeVFSFileSize bytes_written;
		gchar *p;

		io_result = g_io_channel_read (channel_in, buffer, buffer_size,
					       &bytes_read);
		if (io_result == G_IO_ERROR_AGAIN)
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
			if (result == GNOME_VFS_ERROR_INTERRUPTED)
				continue;
			if (result != GNOME_VFS_OK || bytes_written == 0)
				goto end;

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
	GnomeVFSOpenJob *open_job;
	gboolean notify_retval;

	open_job = &job->info.open;

	result = gnome_vfs_open_uri_cancellable (&handle, open_job->request.uri,
						 open_job->request.open_mode,
						 job->context);

	job->handle = handle;
	open_job->notify.result = result;

	notify_retval = job_oneway_notify_and_close (job);

	if (result == GNOME_VFS_OK)
		return notify_retval;
	else
		return FALSE;
}

static gboolean
execute_open_as_channel (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSOpenAsChannelJob *open_as_channel_job;
	GnomeVFSOpenMode open_mode;
	GIOChannel *channel_in, *channel_out;
	gint pipefd[2];

	open_as_channel_job = &job->info.open_as_channel;

	result = gnome_vfs_open_uri_cancellable
		(&handle,
		 open_as_channel_job->request.uri,
		 open_as_channel_job->request.open_mode,
		 job->context);

	if (result != GNOME_VFS_OK) {
		open_as_channel_job->notify.channel = NULL;
		open_as_channel_job->notify.result = result;
		job_oneway_notify_and_close (job);
		return FALSE;
	}

	if (pipe (pipefd) < 0) {
		g_warning (_("Cannot create pipe for open GIOChannel: %s"),
			   g_strerror (errno));
		open_as_channel_job->notify.channel = NULL;
		open_as_channel_job->notify.result = GNOME_VFS_ERROR_INTERNAL;
		job_oneway_notify_and_close (job);
		return FALSE;
	}

	/* Set up the pipe for nonblocking writes, so if the main
           thread is blocking for some reason the slave can keep
           reading data. */
	set_fl(pipefd[1], O_NONBLOCK);
	
	channel_in = g_io_channel_unix_new (pipefd[0]);
	channel_out = g_io_channel_unix_new (pipefd[1]);

	open_mode = open_as_channel_job->request.open_mode;
	
	if (open_mode & GNOME_VFS_OPEN_READ)
		open_as_channel_job->notify.channel = channel_in;
	else
		open_as_channel_job->notify.channel = channel_out;
	
	open_as_channel_job->notify.result = GNOME_VFS_OK;

	if (! job_notify (job))
		return FALSE;

	if (open_mode & GNOME_VFS_OPEN_READ)
		serve_channel_read (handle, channel_in, channel_out,
				    open_as_channel_job->request.advised_block_size,
				    job->context);
	else
		serve_channel_write (handle, channel_in, channel_out,
				     job->context);

	job_close (job);

	return FALSE;
}

static gboolean
execute_create (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSCreateJob *create_job;
	gboolean notify_retval;

	create_job = &job->info.create;

	result = gnome_vfs_create_uri_cancellable
						(&handle,
						 create_job->request.uri,
						 create_job->request.open_mode,
						 create_job->request.exclusive,
						 create_job->request.perm,
						 job->context);

	job->handle = handle;
	create_job->notify.result = result;

	notify_retval = job_oneway_notify_and_close (job);

	if (result == GNOME_VFS_OK)
		return notify_retval;
	else
		return FALSE;
}

static gboolean
execute_create_as_channel (GnomeVFSJob *job)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	GnomeVFSCreateAsChannelJob *create_as_channel_job;
	GIOChannel *channel_in, *channel_out;
	gint pipefd[2];

	create_as_channel_job = &job->info.create_as_channel;

	result = gnome_vfs_open_uri_cancellable
		(&handle,
		 create_as_channel_job->request.uri,
		 create_as_channel_job->request.open_mode,
		 job->context);

	if (result != GNOME_VFS_OK) {
		create_as_channel_job->notify.channel = NULL;
		create_as_channel_job->notify.result = result;
		job_oneway_notify_and_close (job);
		return FALSE;
	}

	if (pipe (pipefd) < 0) {
		g_warning (_("Cannot create pipe for open GIOChannel: %s"),
			   g_strerror (errno));
		create_as_channel_job->notify.channel = NULL;
		create_as_channel_job->notify.result = GNOME_VFS_ERROR_INTERNAL;
		job_oneway_notify_and_close (job);
		return FALSE;
	}

	
	
	channel_in = g_io_channel_unix_new (pipefd[0]);
	channel_out = g_io_channel_unix_new (pipefd[1]);

	create_as_channel_job->notify.channel = channel_out;
	create_as_channel_job->notify.result = GNOME_VFS_OK;

	if (! job_notify (job))
		return FALSE;

	serve_channel_write (handle, channel_in, channel_out, job->context);

	job_close (job);

	return FALSE;
}

static gboolean
execute_close (GnomeVFSJob *job)
{
	GnomeVFSCloseJob *close_job;

	close_job = &job->info.close;

	close_job->notify.result
		= gnome_vfs_close_cancellable (job->handle, job->context);

	job_notify_and_close (job);

	return FALSE;
}

static gboolean
execute_read (GnomeVFSJob *job)
{
	GnomeVFSReadJob *read_job;

	read_job = &job->info.read;

	read_job->notify.result
		= gnome_vfs_read_cancellable (job->handle,
					      read_job->request.buffer,
					      read_job->request.num_bytes,
					      &read_job->notify.bytes_read,
					      job->context);

	return job_oneway_notify_and_close (job);
}

static gboolean
execute_write (GnomeVFSJob *job)
{
	GnomeVFSWriteJob *write_job;

	write_job = &job->info.write;

	write_job->notify.result
		= gnome_vfs_write_cancellable (job->handle,
					       write_job->request.buffer,
					       write_job->request.num_bytes,
					       &write_job->notify.bytes_written,
					       job->context);

	return job_oneway_notify_and_close (job);
}


static gboolean
execute_load_directory_not_sorted (GnomeVFSJob *job,
				   GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSLoadDirectoryJob *load_directory_job;
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSDirectoryList *directory_list;
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	guint count;

	load_directory_job = &job->info.load_directory;

	result = gnome_vfs_directory_open_from_uri
		(&handle,
		 load_directory_job->request.uri,
		 load_directory_job->request.options,
		 (const char **)load_directory_job->request.meta_keys,
		 filter);

	if (result != GNOME_VFS_OK) {
		load_directory_job->notify.result = result;
		load_directory_job->notify.list = NULL;
		load_directory_job->notify.entries_read = 0;
		job_notify_and_close (job);
		return FALSE;
	}

	directory_list = gnome_vfs_directory_list_new ();
	load_directory_job->notify.list = directory_list;

	count = 0;
	while (1) {
		info = gnome_vfs_file_info_new ();
		result = gnome_vfs_directory_read_next (handle, info);

		if (result == GNOME_VFS_OK) {
			gnome_vfs_directory_list_append (directory_list, info);
			count++;
		}

		if (count == load_directory_job->request.items_per_notification
		    || result != GNOME_VFS_OK) {
			load_directory_job->notify.result = result;
			load_directory_job->notify.entries_read = count;

			/* If we have not set a position yet, it means this is
                           the first iteration, so we must position on the
                           first element.  Otherwise, the last time we got here
                           we positioned on the last element with
                           `gnome_vfs_directory_list_last()', so we have to go
                           to the next one.  */
			if (gnome_vfs_directory_list_get_position
			    (directory_list) == NULL)
				gnome_vfs_directory_list_first (directory_list);
			else
				gnome_vfs_directory_list_next (directory_list);

			job_notify (job);

			if (result != GNOME_VFS_OK)
				break;

			count = 0;
			gnome_vfs_directory_list_last (directory_list);
		}
	}

	gnome_vfs_directory_close (handle);

	job_close (job);

	return FALSE;
}

static gboolean
execute_load_directory_sorted (GnomeVFSJob *job,
			       GnomeVFSDirectoryFilter *filter)
{
	GnomeVFSLoadDirectoryJob *load_directory_job;
	GnomeVFSDirectoryList *directory_list;
	GnomeVFSDirectoryListPosition previous_p, p;
	GnomeVFSResult result;
	guint count;

	load_directory_job = &job->info.load_directory;

	result = gnome_vfs_directory_list_load_from_uri
		(&directory_list,
		 load_directory_job->request.uri,
		 load_directory_job->request.options,
		 (const char **)load_directory_job->request.meta_keys,
		 filter);

	if (result != GNOME_VFS_OK) {
		load_directory_job->notify.result = result;
		load_directory_job->notify.list = NULL;
		load_directory_job->notify.entries_read = 0;
		job_notify (job);
		return FALSE;
	}

	gnome_vfs_directory_list_sort
		(directory_list,
		 load_directory_job->request.reverse_order,
		 load_directory_job->request.sort_rules);

	load_directory_job->notify.result = GNOME_VFS_OK;
	load_directory_job->notify.list = directory_list;

	count = 0;
	p = gnome_vfs_directory_list_get_first_position (directory_list);

	if (p == NULL) {
		load_directory_job->notify.result = GNOME_VFS_ERROR_EOF;
		load_directory_job->notify.entries_read = 0;
		job_notify (job);
		return FALSE;
	}

	previous_p = p;
	while (p != NULL) {
		count++;
		p = gnome_vfs_directory_list_position_next (p);
		if (p == NULL
		    || count == load_directory_job->request.items_per_notification) {
			gnome_vfs_directory_list_set_position (directory_list,
							       previous_p);
			if (p == NULL)
				load_directory_job->notify.result = GNOME_VFS_ERROR_EOF;
			else
				load_directory_job->notify.result = GNOME_VFS_OK;
			load_directory_job->notify.entries_read = count;
			job_notify (job);
			count = 0;
			previous_p = p;
		}
	}

	return FALSE;
}

static gboolean
execute_get_file_info (GnomeVFSJob *job)
{
	GnomeVFSGetFileInfoJob *gijob;
	GnomeVFSResult notify_res;

	gijob = &job->info.get_file_info;
	gijob->notify.file_info = gnome_vfs_file_info_new();
	gijob->notify.result = gnome_vfs_get_file_info_uri_cancellable(gijob->request.uri,
								       gijob->notify.file_info,
								       gijob->request.options,
								       (const char **)gijob->request.meta_keys,
								       job->context);
	gnome_vfs_uri_unref(gijob->request.uri);
	g_strfreev(gijob->request.meta_keys);

	notify_res = job_oneway_notify_and_close (job);
	if(notify_res == GNOME_VFS_OK)
		return notify_res;
	else
		return FALSE;
}

static gboolean
execute_load_directory (GnomeVFSJob *job)
{
	GnomeVFSLoadDirectoryJob *load_directory_job;
	GnomeVFSDirectorySortRule *sort_rules;
	GnomeVFSDirectoryFilter *filter;
	gboolean retval;

	load_directory_job = &job->info.load_directory;

	filter = gnome_vfs_directory_filter_new
		(load_directory_job->request.filter_type,
		 load_directory_job->request.filter_options,
		 load_directory_job->request.filter_pattern);

	sort_rules = load_directory_job->request.sort_rules;
	if (sort_rules == NULL
	    || sort_rules[0] == GNOME_VFS_DIRECTORY_SORT_NONE)
		retval = execute_load_directory_not_sorted (job, filter);
	else
		retval = execute_load_directory_sorted (job, filter);

	gnome_vfs_directory_filter_destroy (filter);

	job_close (job);

	g_free (load_directory_job->request.sort_rules);
	g_free (load_directory_job->request.filter_pattern);

	g_strfreev(load_directory_job->request.meta_keys);

	return FALSE;
}


static gint
xfer_callback (GnomeVFSXferProgressInfo *info,
	       gpointer data)
{
	GnomeVFSJob *job;
	GnomeVFSXferJob *xfer_job;

	job = (GnomeVFSJob *) data;
	xfer_job = &job->info.xfer;
	xfer_job->notify.progress_info = info;

	/* Forward the callback to the master thread, which will fill in the
           `notify_answer' member appropriately.  */
	job_notify (job);

	/* Pass the value returned from the callback in the master thread.  */
	return xfer_job->notify_answer.value;
}

static gboolean
execute_xfer (GnomeVFSJob *job)
{
	GnomeVFSXferJob *xfer_job;
	GnomeVFSResult result;

	xfer_job = &job->info.xfer;

	result = gnome_vfs_xfer_private (xfer_job->request.source_directory_uri,
					 xfer_job->request.source_name_list,
					 xfer_job->request.target_directory_uri,
					 xfer_job->request.target_name_list,
					 xfer_job->request.xfer_options,
					 xfer_job->request.error_mode,
					 xfer_job->request.overwrite_mode,
					 xfer_callback,
					 job,
					 xfer_job->request.progress_sync_callback,
					 xfer_job->request.sync_callback_data);

	/* If the xfer functions returns an error now, something really bad
           must have happened.  */
	if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_INTERRUPTED) {
		GnomeVFSXferProgressInfo info;


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

		xfer_job->notify.progress_info = &info;

		job_notify (job);
		return FALSE;
	}

	return FALSE;
}


/* This function is called by the slave thread to execute a
   GnomeVFSJob.  */
gboolean
gnome_vfs_job_execute (GnomeVFSJob *job)
{
	g_mutex_lock (job->access_lock);
	if (job->is_empty)
		g_cond_wait (job->execution_condition, job->access_lock);

	switch (job->type) {
	case GNOME_VFS_JOB_OPEN:
		return execute_open (job);
	case GNOME_VFS_JOB_OPEN_AS_CHANNEL:
		return execute_open_as_channel (job);
	case GNOME_VFS_JOB_CREATE:
		return execute_create (job);
	case GNOME_VFS_JOB_CREATE_AS_CHANNEL:
		return execute_create_as_channel (job);
	case GNOME_VFS_JOB_CLOSE:
		return execute_close (job);
	case GNOME_VFS_JOB_READ:
		return execute_read (job);
	case GNOME_VFS_JOB_WRITE:
		return execute_write (job);
	case GNOME_VFS_JOB_LOAD_DIRECTORY:
		return execute_load_directory (job);
	case GNOME_VFS_JOB_XFER:
		return execute_xfer (job);
	case GNOME_VFS_JOB_GET_FILE_INFO:
		return execute_get_file_info (job);
	default:
		g_warning (_("Unknown job ID %d"), job->type);
		return FALSE;
	}
}


GnomeVFSResult
gnome_vfs_job_cancel (GnomeVFSJob *job)
{
	GnomeVFSCancellation *cancellation;
	
	g_return_val_if_fail (job != NULL, GNOME_VFS_ERROR_BADPARAMS);

	cancellation = gnome_vfs_context_get_cancellation(job->context);

	if (cancellation)
		gnome_vfs_cancellation_cancel (cancellation);

	gnome_vfs_context_emit_message (job->context, _("Operation stopped"));
	
	return GNOME_VFS_OK;
}
