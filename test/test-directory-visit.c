/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-directory-visit.c - Test program for the directory visiting functions
   of the GNOME Virtual File System.

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


static const gchar *
type_to_string (GnomeVFSFileType type)
{
	switch (type) {
	case GNOME_VFS_FILE_TYPE_UNKNOWN:
		return _("Unknown");
	case GNOME_VFS_FILE_TYPE_REGULAR:
		return _("Regular");
	case GNOME_VFS_FILE_TYPE_DIRECTORY:
		return _("Directory");
	case GNOME_VFS_FILE_TYPE_BROKENSYMLINK:
		return _("Broken symlink");
	case GNOME_VFS_FILE_TYPE_FIFO:
		return _("FIFO");
	case GNOME_VFS_FILE_TYPE_SOCKET:
		return _("Socket");
	case GNOME_VFS_FILE_TYPE_CHARDEVICE:
		return _("Character device");
	case GNOME_VFS_FILE_TYPE_BLOCKDEVICE:
		return _("Block device");
	default:
		return _("???");
	}
}

static gboolean
directory_visit_callback (const gchar *rel_path,
			  GnomeVFSFileInfo *info,
			  gboolean recursing_will_loop,
			  gpointer data,
			  gboolean *recurse)
{
	printf ("directory_visit_callback -- rel_path `%s' data `%s'\n",
		rel_path, (gchar *) data);

	printf (_("  File `%s'%s (%s, %s), size %ld, mode %04o\n"),
		info->name,
		info->is_symlink ? " [link]" : "",
		type_to_string (info->type),
		gnome_vfs_file_info_get_mime_type (info),
		info->size, info->permissions);

	if (info->name[0] != '.'
	    || (info->name[1] != '.' && info->name[1] != 0)
	    || info->name[2] != 0) {
		if (recursing_will_loop) {
			printf ("Loop detected\n");
			exit (1);
		}
		*recurse = TRUE;
	} else {
		*recurse = FALSE;
	}

	return TRUE;
}


int
main (int argc, char **argv)
{
	GnomeVFSResult result;

	if (argc != 2) {
		fprintf (stderr, "Usage: %s <directory>\n", argv[0]);
		return 1;
	}

	gnome_vfs_init ();

	result = gnome_vfs_directory_visit
		(argv[1],
		 (GNOME_VFS_FILE_INFO_FOLLOWLINKS
		  | GNOME_VFS_FILE_INFO_FASTMIMETYPE),
		 NULL,
		 NULL,
		 GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK,
		 directory_visit_callback,
		 "stringa");

	printf ("Result: %s\n", gnome_vfs_result_to_string (result));

	return 0;
}
