/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-job-slave.c - Thread for asynchronous GnomeVFSJobs
   (version for POSIX threads).

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

#include "gnome-vfs-job-slave.h"

#include "gnome-vfs-private.h"
#include "gnome-vfs.h"
#include <gtk/gtk.h>
#include <pthread.h>
#include <unistd.h>

/* FIXME bugzilla.eazel.com 1130:
 * - would use a GStaticMutex here but the initializer macro is broken.
 * Just use a dynamically allocated one for now.
 */
static GMutex *gnome_vfs_thread_count_mutex = NULL;
static volatile int gnome_vfs_slave_thread_count = 0;
static volatile gboolean gnome_vfs_quitting = FALSE;
static volatile gboolean gnome_vfs_done_quitting = FALSE;

static void *
thread_routine (void *data)
{
	GnomeVFSJob *job;

	job = (GnomeVFSJob *) data;
 
	while (gnome_vfs_job_execute (job))
		;

	gnome_vfs_job_destroy (job);
	
	g_mutex_lock (gnome_vfs_thread_count_mutex);
	/* the thread is done, take it out of the count */
	gnome_vfs_slave_thread_count--;
	g_mutex_unlock (gnome_vfs_thread_count_mutex);

	return NULL;
}

gboolean
gnome_vfs_job_create_slave (GnomeVFSJob *job)
{
	gboolean abort_early;
	pthread_attr_t thread_attr;
	pthread_t thread;
	
	g_return_val_if_fail (job != NULL, FALSE);


	if (gnome_vfs_thread_count_mutex == NULL) {
		/* FIXME bugzilla.eazel.com 1130:
		 * - would use a GStaticMutex but the initializer macro 
		 * is broken. There is no real danger here as the 
		 * gnome_vfs_thread_count_mutex only gets created by the master 
		 * thread but it would still be cleaner to use a static one.
		 */
		gnome_vfs_thread_count_mutex = g_mutex_new ();
	}

	if (gnome_vfs_quitting) {
		g_warning ("Someone still starting up GnomeVFS async calls after quit.");
	}

	g_mutex_lock (gnome_vfs_thread_count_mutex);
	abort_early = gnome_vfs_done_quitting;
	if (!abort_early) {
		/* count the new thread */
		gnome_vfs_slave_thread_count++;
	}
	g_mutex_unlock (gnome_vfs_thread_count_mutex);
	
	if (abort_early) {
		/* The application is quitting, we have already returned from
		 * gnome_vfs_wait_for_slave_threads, we can't start any more threads
		 * because they would potentially block indefinitely and prevent the
		 * app from quitting.
		 */
		return FALSE;
	}
	
	pthread_attr_init (&thread_attr);
	pthread_attr_setdetachstate (&thread_attr,
				     PTHREAD_CREATE_DETACHED);

	if (pthread_create (&thread, &thread_attr,
			    thread_routine, job) != 0) {
		g_warning ("Impossible to allocate a new GnomeVFSJob thread.");
		
		g_mutex_lock (gnome_vfs_thread_count_mutex);
		/* we didn't really get a new thread so take it off the count */
		gnome_vfs_slave_thread_count--;
		g_mutex_unlock (gnome_vfs_thread_count_mutex);
		
		return FALSE;
	}
	
	return TRUE;
}

void
gnome_vfs_thread_backend_shutdown (void)
{
	gboolean done;
	int debug_outstanding_count;
	int count;
	
	done = FALSE;

	gnome_vfs_quitting = TRUE;

	JOB_DEBUG (("###### shutting down"));

	if (gnome_vfs_thread_count_mutex == NULL) {
		/* must have never used a single async call */
		return;
	}
		
	for (count = 0;; count++) {
		/* Check if it is OK to quit - no pending slave threads
		 * are still trying to quit.
		 */
		g_mutex_lock (gnome_vfs_thread_count_mutex);
		g_assert (!gnome_vfs_done_quitting);
		if (gnome_vfs_slave_thread_count == 0) {
			done = TRUE;
			gnome_vfs_done_quitting = TRUE;
		}
		debug_outstanding_count = gnome_vfs_slave_thread_count;
		g_mutex_unlock (gnome_vfs_thread_count_mutex);

		if (done) {
			return;
		}

		/* Some threads are still trying to quit, wait a bit until they
		 * are done.
		 */
#if GNOME_PLATFORM_VERSION >= 1095000
		g_main_iteration (FALSE);
#else
		gtk_main_iteration_do (FALSE);
#endif
		usleep (20000);		
	}
}

#include "stdio.h"
int
gnome_vfs_debug_get_thread_count (void)
{
	int result;

	if (gnome_vfs_thread_count_mutex == NULL) {
		/* must have never used a single async call */
		return 0;
	}
		
	g_mutex_lock (gnome_vfs_thread_count_mutex);
	if (gnome_vfs_done_quitting) {
		result = 0;
	} else {
		result = gnome_vfs_slave_thread_count;
	}
	g_mutex_unlock (gnome_vfs_thread_count_mutex);

	return result;		
}
