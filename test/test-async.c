/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-vfs.c - Test program for the GNOME Virtual File System.

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

   Author: Ettore Perazzoli <ettore@comm2000.it>
*/

#include <config.h>

#include <glib/gmain.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <stdio.h>

static GMainLoop *main_loop;

/* Callbacks.  */
static void
close_callback (GnomeVFSAsyncHandle *handle,
		GnomeVFSResult result,
		gpointer callback_data)
{
	printf ("Close: %s.\n", gnome_vfs_result_to_string (result));
	g_main_loop_quit (main_loop);
}

static void
read_callback (GnomeVFSAsyncHandle *handle,
	       GnomeVFSResult result,
               gpointer buffer,
               GnomeVFSFileSize bytes_requested,
	       GnomeVFSFileSize bytes_read,
               gpointer callback_data)
{
	if (result != GNOME_VFS_OK) {
		printf ("Read failed: %s", gnome_vfs_result_to_string (result));
	} else {
		printf ("%"GNOME_VFS_SIZE_FORMAT_STR"/"
			"%"GNOME_VFS_SIZE_FORMAT_STR" "
			"byte(s) read, callback data `%s'\n",
			bytes_read, bytes_requested, (gchar *) callback_data);
		*((gchar *) buffer + bytes_read) = 0;
		puts (buffer);
	}

	printf ("Now closing the file.\n");
	gnome_vfs_async_close (handle, close_callback, "close");
	g_main_loop_quit (main_loop);
}

static void
open_callback  (GnomeVFSAsyncHandle *handle,
		GnomeVFSResult result,
                gpointer callback_data)
{
	if (result != GNOME_VFS_OK) {
		printf ("Open failed: %s.\n",
			gnome_vfs_result_to_string (result));
		g_main_loop_quit (main_loop);
	} else {
		gchar *buffer;
		const gulong buffer_size = 1024;

		printf ("File opened correctly, data `%s'.\n",
			(gchar *) callback_data);

		buffer = g_malloc (buffer_size);
		gnome_vfs_async_read (handle,
				      buffer,
				      buffer_size - 1,
				      read_callback,
				      "read_callback");
	}
}

int
main (int argc, char **argv)
{
	GnomeVFSAsyncHandle *handle;

	if (argc < 2) {
		fprintf (stderr, "Usage: %s <uri>\n", argv[0]);
		return 1;
	}

	puts ("Initializing gnome-vfs...");
	gnome_vfs_init ();

	puts ("Creating async context...");

	printf ("Starting open for `%s'...\n", argv[1]);
	gnome_vfs_async_open (&handle, argv[1], GNOME_VFS_OPEN_READ,
			      open_callback, "open_callback");

	puts ("Main loop running.");
	main_loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (main_loop);
	g_main_loop_unref (main_loop);

	puts ("Main loop finished.");

	puts ("All done");

	while (1)
		;

	return 0;
}
