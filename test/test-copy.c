/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-copy.c - Test for the copying functionality of the GNOME Virtual File
   System library.

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

#include "gnome-vfs.h"


static void
show_result (GnomeVFSResult result, const gchar *what, const gchar *text_uri)
{
	fprintf (stderr, "%s `%s': %s\n",
		 what, text_uri, gnome_vfs_result_to_string (result));
	if (result != GNOME_VFS_OK)
		exit (1);
}

static gint
xfer_progress_callback (const GnomeVFSXferProgressInfo *info,
			gpointer data)
{
	switch (info->status) {
	case GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR:
		printf ("VFS Error: %s\n",
			gnome_vfs_result_to_string (info->vfs_status));
		exit (1);
		break;
	case GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE:
		printf ("Overwriting `%s' with `%s'\n",
			info->target_name, info->source_name);
		exit (1);
		break;
	case GNOME_VFS_XFER_PROGRESS_STATUS_OK:
		printf ("Status: OK\n");
		switch (info->phase) {
		case GNOME_VFS_XFER_PHASE_READYTOGO:
			printf ("Ready to go!\n");
			return TRUE;
		case GNOME_VFS_XFER_PHASE_XFERRING:
			printf ("Transferring `%s' to `%s' (file %ld/%ld, byte %ld/%ld in file, %ld/%ld total)\n",
				info->source_name,
				info->target_name,
				info->file_index,
				info->files_total,
				info->bytes_copied,
				info->file_size,
				info->total_bytes_copied,
				info->bytes_total);
			return TRUE;
		case GNOME_VFS_XFER_PHASE_FILECOMPLETED:
			printf ("Done with `%s' -> `%s', going next\n",
				info->source_name, info->target_name);
			return TRUE;
		case GNOME_VFS_XFER_PHASE_COMPLETED:
			printf ("All done.\n");
			return TRUE;
		default:
			printf ("Unexpected phase %d\n", info->phase);
			return FALSE;
		}
	}

	printf ("Boh!\n");
	return FALSE;
}


int
main (int argc, char **argv)
{
	GnomeVFSResult result;
	GList *name_list;

	if (! gnome_vfs_init ()) {
		fprintf (stderr, "Cannot initialize gnome-vfs.\n");
		return 1;
	}

	if (argc != 4) {
		fprintf (stderr, "Usage: %s <source dir> <file name> <dest dir>\n",
			 argv[0]);
		return 1;
	}

	name_list = g_list_alloc ();
	name_list->data = argv[2];

	result = gnome_vfs_xfer (argv[1], name_list, argv[3], NULL,
				 GNOME_VFS_XFER_RECURSIVE,
				 GNOME_VFS_XFER_ERROR_MODE_QUERY,
				 GNOME_VFS_XFER_OVERWRITE_MODE_QUERY,
				 xfer_progress_callback,
				 NULL);

	show_result (result, "gnome_vfs_xfer", argv[1]);

	return 0;
}
