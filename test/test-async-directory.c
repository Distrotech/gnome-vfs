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
print_list (GnomeVFSDirectoryList *list)
{
	GnomeVFSFileInfo *info;

	info = gnome_vfs_directory_list_current (list);
	while (info != NULL) {
		printf ("  File `%s'%s (%s, %s), size %ld, mode %04o\n",
			info->name,
			info->is_symlink ? " [link]" : "",
			type_to_string (info->type),
			gnome_vfs_file_info_get_mime_type (info),
			info->size, info->permissions);

		info = gnome_vfs_directory_list_next (list);
	}
}

static void
directory_load_callback (GnomeVFSAsyncContext *context,
			 GnomeVFSResult result,
			 GnomeVFSDirectoryList *list,
			 guint entries_read,
			 gpointer callback_data)
{
	printf ("Directory load callback: %s, %d entries, callback_data `%s'\n",
		gnome_vfs_result_to_string (result), entries_read,
		callback_data);

	if (list != NULL)
		print_list (list);

	if (result != GNOME_VFS_OK)
		gtk_main_quit ();
}

int
main (int argc, char **argv)
{
	GnomeVFSAsyncContext *context;
	GnomeVFSResult result;
	CORBA_Environment ev;
	guint num_iterations;
	GnomeVFSDirectorySortRule sort_rules[] = {
		GNOME_VFS_DIRECTORY_SORT_BYNAME,
		GNOME_VFS_DIRECTORY_SORT_NONE
	};

	if (argc < 2) {
		fprintf (stderr, "Usage: %s <uri>\n", argv[0]);
		return 1;
	}

	if (argc == 3)
		num_iterations = atoi (argv[2]);
	else
		num_iterations = 10;

	CORBA_exception_init (&ev);
	puts ("Initializing gnome-libs with CORBA...");
	gnome_CORBA_init ("test-vfs", "0.0", &argc, argv, 0, &ev);

	puts ("Initializing gnome-vfs...");
	gnome_vfs_init ();

	puts ("Creating async context...");
	context = gnome_vfs_async_context_new ();
	if (context == NULL) {
		fprintf (stderr, "Cannot create async context!\n");
#ifdef WITH_CORBA
		CORBA_exception_free (&ev);
#endif
		return 1;
	}

	result = gnome_vfs_async_load_directory
		(context, /* context */
		 argv[1], /* text_uri */
		 (GNOME_VFS_FILE_INFO_GETMIMETYPE
		  | GNOME_VFS_FILE_INFO_FASTMIMETYPE
		  | GNOME_VFS_FILE_INFO_FOLLOWLINKS), /* options */
		 NULL, /* meta_keys */
		 sort_rules, /* sort_rules */
		 FALSE, /* reverse_order */
		 GNOME_VFS_DIRECTORY_FILTER_NONE, /* filter_type */
		 0, /* filter_options */
		 NULL, /* filter_pattern */
		 num_iterations, /* items_per_notification */
		 directory_load_callback, /* callback */
		 "here we are"); /* callback_data */

	puts ("GTK+ main loop running.");
	gtk_main ();

	puts ("GTK+ main loop finished: destroying context.");
	gnome_vfs_async_context_destroy (context);

	return 0;
}
