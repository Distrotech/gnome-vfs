/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-async-directory.c - Test program for asynchronous directory
   reading with the GNOME Virtual File System.

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

#include <stdio.h>

#include <gnome.h>

#include <orb/orbit.h>
#include <libgnorba/gnorba.h>

#include "gnome-vfs.h"


static int measure_speed = 0;
static int sort = 0;
static int items_per_notification = 1;


struct poptOption options[] = {
	{
		"chunk-size",
		'c',
		POPT_ARG_INT,
		&items_per_notification,
		0,
		"Number of items to send for every notification",
	        "NUM_ITEMS"
	},
	{
		"measure-speed",
		'm',
		POPT_ARG_NONE,
		&measure_speed,
		0,
		"Measure speed without displaying anything",
		NULL
	},
	{
		"sort",
		's',
		POPT_ARG_NONE,
		&sort,
		0,
		"Sort entries",
		NULL
	},
	{
		NULL,
		0,
		0,
		NULL,
		0,
		NULL,
		NULL
	}
};


static const gchar *
type_to_string (GnomeVFSFileType type)
{
	switch (type) {
	case GNOME_VFS_FILE_TYPE_UNKNOWN:
		return "Unknown";
	case GNOME_VFS_FILE_TYPE_REGULAR:
		return "Regular";
	case GNOME_VFS_FILE_TYPE_DIRECTORY:
		return "Directory";
	case GNOME_VFS_FILE_TYPE_BROKENSYMLINK:
		return "Broken symlink";
	case GNOME_VFS_FILE_TYPE_FIFO:
		return "FIFO";
	case GNOME_VFS_FILE_TYPE_SOCKET:
		return "Socket";
	case GNOME_VFS_FILE_TYPE_CHARDEVICE:
		return "Character device";
	case GNOME_VFS_FILE_TYPE_BLOCKDEVICE:
		return "Block device";
	default:
		return "???";
	}
}

static void
print_list (GnomeVFSDirectoryList *list, guint num_entries)
{
	GnomeVFSFileInfo *info;
	guint i;

	info = gnome_vfs_directory_list_current (list);
	for (i = 0; i < num_entries && info != NULL; i++) {
		printf ("  File `%s'%s (%s, %s), "
			"size %"GNOME_VFS_SIZE_FORMAT_STR", mode %04o\n",
			info->name,
			(info->flags & GNOME_VFS_FILE_FLAGS_SYMLINK) ? " [link]" : "",
			type_to_string (info->type),
			gnome_vfs_file_info_get_mime_type (info),
			info->size, info->permissions);
		fflush (stdout);

		info = gnome_vfs_directory_list_next (list);
	}
}

static void
directory_load_callback (GnomeVFSAsyncHandle *handle,
			 GnomeVFSResult result,
			 GnomeVFSDirectoryList *list,
			 guint entries_read,
			 gpointer callback_data)
{
	guint *num_entries_ptr;

	if (! measure_speed) {
#if 0
		printf ("Directory load callback: %s, %d entries, callback_data `%s'\n",
			gnome_vfs_result_to_string (result),
			entries_read,
			(gchar *) callback_data);
#endif

		if (list != NULL)
			print_list (list, entries_read);
	}

	num_entries_ptr = (guint *) callback_data;
	*num_entries_ptr += entries_read;

	if (result != GNOME_VFS_OK)
		gtk_main_quit ();
}

int
main (int argc, char **argv)
{
	GnomeVFSAsyncHandle *handle;
	poptContext popt_context;
	const char **args;
	gchar *text_uri;
	GnomeVFSDirectorySortRule sort_rules[] = {
		GNOME_VFS_DIRECTORY_SORT_BYNAME,
		GNOME_VFS_DIRECTORY_SORT_NONE
	};
	GTimer *timer;
	guint num_entries;
#ifdef WITH_CORBA
	CORBA_Environment ev;
#endif

#ifdef WITH_PTHREAD
	puts ("Initializing threads...");
	g_thread_init (NULL);
#endif

#ifdef WITH_CORBA
	CORBA_exception_init (&ev);
	puts ("Initializing gnome-libs with CORBA...");
	gnome_CORBA_init_with_popt_table ("test-vfs", "0.0", &argc, argv,
					  options, 0, &popt_context, 0, &ev);
#else
	puts ("Initializing gnome-libs...");
	gnome_init_with_popt_table ("test-vfs", "0.0", argc, argv,
				    options, 0, &popt_context);
#endif

	args = poptGetArgs (popt_context);
	if (args == NULL || args[1] != NULL) {
		fprintf (stderr, "Usage: %s [<options>] <uri>\n", argv[0]);
		return 1;
	}

	text_uri = g_strdup (args[0]);
	poptFreeContext (popt_context);

	puts ("Initializing gnome-vfs...");
	gnome_vfs_init ();

	printf ("%d item(s) per notification\n", items_per_notification);

	if (measure_speed) {
		timer = g_timer_new ();
		g_timer_start (timer);
	} else {
		timer = NULL;
	}

	num_entries = 0;
	gnome_vfs_async_load_directory
		(&handle, /* handle */
		 text_uri, /* text_uri */
		 (GNOME_VFS_FILE_INFO_GETMIMETYPE
		  | GNOME_VFS_FILE_INFO_FASTMIMETYPE
		  | GNOME_VFS_FILE_INFO_FOLLOWLINKS), /* options */
		 NULL, /* meta_keys */
		 sort ? sort_rules : NULL, /* sort_rules */
		 FALSE, /* reverse_order */
		 GNOME_VFS_DIRECTORY_FILTER_NONE, /* filter_type */
		 0, /* filter_options */
		 NULL, /* filter_pattern */
		 items_per_notification, /* items_per_notification */
		 directory_load_callback, /* callback */
		 &num_entries); /* callback_data */

	if (! measure_speed)
		puts ("GTK+ main loop running.");

	gtk_main ();

	if (measure_speed) {
		gdouble elapsed_seconds;

		g_timer_stop (timer);
		elapsed_seconds = g_timer_elapsed (timer, NULL);
		printf ("%.5f seconds for %d entries, %.5f entries/sec.\n",
			elapsed_seconds, num_entries,
			(double) num_entries / elapsed_seconds);
	}

	if (! measure_speed)
		puts ("GTK+ main loop finished."); fflush (stdout);

#ifdef WITH_CORBA
	CORBA_exception_free (&ev);
#endif

	puts ("All done");

	while (1)
		;

	return 0;
}
