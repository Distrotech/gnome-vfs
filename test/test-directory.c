/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-directory.c - Test program for directory reading in the GNOME
   Virtual File System.

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

#include "gnome-vfs.h"


static void
show_result (GnomeVFSResult result, const gchar *what, const gchar *text_uri)
{
	fprintf (stderr, "%s `%s': %s\n",
		 what, text_uri, gnome_vfs_result_to_string (result));
	if (result != GNOME_VFS_OK)
		exit (1);
}

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

	info = gnome_vfs_directory_list_first (list);

	if (info == NULL) {
		printf ("  (No files)\n");
		return;
	}

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
sort_list (GnomeVFSDirectoryList *list)
{
	GnomeVFSDirectorySortRule rules[] = {
		GNOME_VFS_DIRECTORY_SORT_DIRECTORYFIRST,
		GNOME_VFS_DIRECTORY_SORT_BYNAME,
		GNOME_VFS_DIRECTORY_SORT_NONE
	};
		
	printf ("Now sorting...");
	fflush (stdout);

	gnome_vfs_directory_list_sort (list, FALSE, rules);

	printf ("  Done!\n");
}

static void
filter_list (GnomeVFSDirectoryList *list,
	     const gchar *pattern)
{
	GnomeVFSDirectoryFilter *filter;

	printf ("Filtering with `%s'...", pattern);
	fflush (stdout);

	filter = gnome_vfs_directory_filter_new
		(GNOME_VFS_DIRECTORY_FILTER_SHELLPATTERN,
		 GNOME_VFS_DIRECTORY_FILTER_NODOTFILES,
		 pattern);

	gnome_vfs_directory_list_filter (list, filter);
	gnome_vfs_directory_filter_destroy (filter);

	printf ("  Done!\n");
}


int
main (int argc, char **argv)
{
	GnomeVFSDirectoryList *list;
	GnomeVFSResult result;
	gchar *uri;

	if (argc < 2 || argc > 3) {
		fprintf (stderr, "Usage: %s <uri> [<pattern>]\n", argv[0]);
		return 1;
	}

	uri = argv[1];

	gnome_vfs_init ();

	printf ("Loading directory...");
	fflush (stdout);

	/* Load with no filters and without requesting any metadata.  */
	result = gnome_vfs_directory_load (&list, uri,
					   (GNOME_VFS_FILE_INFO_GETMIMETYPE
					    | GNOME_VFS_FILE_INFO_FASTMIMETYPE
					    | GNOME_VFS_FILE_INFO_FOLLOWLINKS),
					   NULL,
					   NULL);

	printf ("Ok\n");

	show_result (result, "load_directory", uri);

	printf ("Raw listing for `%s':\n", uri);
	print_list (list);

	sort_list (list);
	printf ("Sorted listing for `%s':\n", uri);
	print_list (list);

	if (argc >= 3) {
		filter_list (list, argv[2]);
		printf ("Filtered (`%s') listing for `%s':\n",
			argv[2], uri);
		print_list (list);
	}

	printf ("Destroying.\n");
	gnome_vfs_directory_list_destroy (list);

	printf ("Done.\n");

	return 0;
}
