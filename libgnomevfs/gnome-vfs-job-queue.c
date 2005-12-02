/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-job-queue.c - Job queue for asynchronous GnomeVFSJobs
   
   Copyright (C) 2005 Christian Kellner
   
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

   Author: Christian Kellner <gicmo@gnome.org>
*/

#include <config.h>
#include "gnome-vfs-job-queue.h"
#include "gnome-vfs-async-job-map.h"
#include <libgnomevfs/gnome-vfs-job-limit.h>

#ifndef DEFAULT_THREAD_COUNT_LIMIT
#define DEFAULT_THREAD_COUNT_LIMIT 10
#endif

#ifndef MIN_THREADS
#define MIN_THREADS 2
#endif

static GThreadPool *thread_pool = NULL;

static volatile gboolean gnome_vfs_quitting = FALSE;

static void
thread_entry_point (gpointer data, gpointer user_data)
{
	guint id;
	GnomeVFSJob *job;
	GnomeVFSAsyncHandle *job_handle;
	gboolean complete;

	job_handle = (GnomeVFSAsyncHandle *) data;

	id = GPOINTER_TO_UINT (job_handle);
	/* job map must always be locked before the job_lock
	 * if both locks are needed */
	_gnome_vfs_async_job_map_lock ();
	
	job = _gnome_vfs_async_job_map_get_job (job_handle);
	
	if (job == NULL) {
		JOB_DEBUG (("job already dead, bail %u", id));
		_gnome_vfs_async_job_map_unlock ();
		return;
	}
	
	JOB_DEBUG (("locking job_lock %u", id));
	g_mutex_lock (job->job_lock);
	_gnome_vfs_async_job_map_unlock ();

	_gnome_vfs_job_execute (job);
	complete = _gnome_vfs_job_complete (job);
	
	JOB_DEBUG (("Unlocking access lock %u", id));
	g_mutex_unlock (job->job_lock);

	if (complete) {
		_gnome_vfs_async_job_map_lock ();
		JOB_DEBUG (("job %u done, removing from map and destroying", id));
		_gnome_vfs_async_job_completed (job_handle);
		_gnome_vfs_job_destroy (job);
		_gnome_vfs_async_job_map_unlock ();
	}
}


void 
_gnome_vfs_job_queue_init (void)
{
	thread_pool = g_thread_pool_new (thread_entry_point,
					 NULL,
					 DEFAULT_THREAD_COUNT_LIMIT,
					 FALSE,
					 NULL);
}


gboolean
_gnome_vfs_job_schedule (GnomeVFSJob *job)
{
	GError *err = NULL;
	
	if (G_UNLIKELY (gnome_vfs_quitting)) {
		/* The application is quitting, the threadpool might already
		 * be dead, just return FALSE 
		 * We are also not calling _gnome_vfs_async_job_completed 
		 * because the job map might also be dead */
		g_warning ("Starting of GnomeVFS async calls after quit.");
		return FALSE;
	}

	g_thread_pool_push (thread_pool, job->job_handle, &err);

	if (G_UNLIKELY (err != NULL)) {
		g_warning ("Could not push thread %s into pool\n",
			   err->message);

		/* thread did not start up, remove the job from the hash table */
		_gnome_vfs_async_job_completed (job->job_handle);
		
		return FALSE;
	}

	return TRUE;	
}

/**
 * gnome_vfs_async_set_job_limit:
 * @limit: maximum number of allowable threads.
 *
 * Restrict the number of worker threads used by async operations
 * to @limit.
 */
void
gnome_vfs_async_set_job_limit (int limit)
{
	if (limit < MIN_THREADS) {
		g_warning ("Attempt to set the thread_count_limit below %d", 
			   MIN_THREADS);
		return;
	}

	g_thread_pool_set_max_threads (thread_pool, limit, NULL);
}

/**
 * gnome_vfs_async_get_job_limit:
 * 
 * Get the current maximum allowable number of
 * worker threads for async operations.
 *
 * Return value: current maximum number of threads.
 */
int
gnome_vfs_async_get_job_limit (void)
{
	return g_thread_pool_get_max_threads (thread_pool);
}

void
_gnome_vfs_job_queue_shutdown (void)
{
	g_thread_pool_free (thread_pool, FALSE, FALSE);

	gnome_vfs_quitting = TRUE;

	while (gnome_vfs_job_get_count () != 0) {
		
		g_main_context_iteration (NULL, FALSE);
		g_usleep (20000);

	}

	_gnome_vfs_async_job_map_shutdown ();
}

