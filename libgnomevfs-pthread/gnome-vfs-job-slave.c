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

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-job-slave.h"


static void *
thread_routine (void *data)
{
	GnomeVFSJobSlave *slave;
	guint bytes_written;

	slave = (GnomeVFSJobSlave *) data;

	/* Let the main thread know we are alive.  */
	g_io_channel_write (slave->job->wakeup_channel_out, "a",
			    1, &bytes_written);
 
	while (gnome_vfs_job_execute (slave->job))
		;

	/* FIXME: What cleanup?  */

	return NULL;
}


GnomeVFSJobSlave *
gnome_vfs_job_slave_new (GnomeVFSJob *job)
{
	GnomeVFSJobSlave *new;

	g_return_val_if_fail (job != NULL, NULL);

	new = g_new (GnomeVFSJobSlave, 1);

	new->job = job;

	pthread_attr_init (&new->pthread_attr);
	pthread_attr_setdetachstate (&new->pthread_attr,
				     PTHREAD_CREATE_DETACHED);

	if (pthread_create (&new->pthread, &new->pthread_attr,
			    thread_routine, new) != 0) {
		g_warning ("Impossible to allocate a new GnomeVFSJob thread.");
		return NULL;
	}

	return new;
}

void
gnome_vfs_job_slave_cancel (GnomeVFSJobSlave *slave)
{
	g_return_if_fail (slave != NULL);

	pthread_cancel (slave->pthread);
}

/* FIXME: This is wrong.  */
void
gnome_vfs_job_slave_destroy (GnomeVFSJobSlave *slave)
{
	g_return_if_fail (slave != NULL);

	gnome_vfs_job_slave_cancel (slave);

	g_free (slave);
}
