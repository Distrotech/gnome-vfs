/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-seek.c - Test for the seek emulation functionality of the GNOME Virtual
   File System library.

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

   Author: Michael Meeks <michael@imaginator.com> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "gnome-vfs.h"

static void
show_result (GnomeVFSResult result, const gchar *what, const gchar *text_uri)
{
	fprintf (stderr, "%s `%s': %s\n",
		 what, text_uri, gnome_vfs_result_to_string (result));
	if (result != GNOME_VFS_OK)
		exit (1);
}

static gboolean
show_if_error (GnomeVFSResult result, const gchar *what)
{
	if (result != GNOME_VFS_OK) {
		fprintf (stderr, "%s: `%s'\n",
			 what, gnome_vfs_result_to_string (result));
		return TRUE;
	} else
		return FALSE;
}

static gboolean
seek_test_chunk (GnomeVFSHandle      *handle,
		 FILE                *ref,
		 GnomeVFSFileOffset   vfs_offset,
		 GnomeVFSSeekPosition whence,
		 GnomeVFSFileSize     length)
{
	GnomeVFSResult result;
	const char *txt;
	
	switch (whence) {
	case GNOME_VFS_SEEK_START:
		txt = "seek_start";
		break;
	case GNOME_VFS_SEEK_CURRENT:
		txt = "seek_current";
		break;
	case GNOME_VFS_SEEK_END:
		txt = "seek_end";
		break;
	default:
		g_warning ("Unknown seek type");
		return FALSE;
	}
	printf ("offset %d, whence '%s', length %d\n", (int)vfs_offset, txt, (int)length);

	{ /* Preliminary tell */
		GnomeVFSFileSize offset;
		long ref_off;
		result  = gnome_vfs_tell (handle, &offset);
		if (show_if_error (result, "head gnome_vfs_tell"))
			return FALSE;

		ref_off = ftell (ref);
		if (ref_off < 0) {
			g_warning ("Wierd ftell failure");
			return FALSE;
		}

		if (ref_off != offset) {
			g_warning ("Offset mismatch %d should be %d", (int)offset, (int)ref_off);
			return FALSE;
		}
	}
	
	{ /* Tail tell */
		GnomeVFSFileSize offset;
		long ref_off;
		result  = gnome_vfs_tell (handle, &offset);
		if (show_if_error (result, "tail gnome_vfs_tell"))
			return FALSE;

		ref_off = ftell (ref);
		if (ref_off < 0) {
			g_warning ("Wierd ftell failure");
			return FALSE;
		}

		if (ref_off != offset) {
			g_warning ("Offset mismatch %d should be %d", (int)offset, (int)ref_off);
			return FALSE;
		}
	}
	

	return TRUE;
}

int
main (int argc, char **argv)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	FILE *ref;
	int i;

	if (! gnome_vfs_init ()) {
		fprintf (stderr, "Cannot initialize gnome-vfs.\n");
		return 1;
	}

	if (argc != 3) {
		fprintf (stderr, "This is a program to test seek emulation on linear filesystems\n");
		fprintf (stderr, "Usage: %s <source file uri> <seekable local reference fname>\n",
			 argv[0]);
		return 1;
	}

	result = gnome_vfs_open (&handle, argv[1], GNOME_VFS_OPEN_READ|GNOME_VFS_OPEN_RANDOM);
	show_result (result, "gnome_vfs_open", argv[1]);

	if (!(ref = fopen (argv[2], "r"))) {
		fprintf (stderr, "Failed to open '%s' to compare seek history\n", argv[2]);
		exit (1);
	}

	for (i = 0; i < 1000; i++) {
		GnomeVFSFileSize     length  = (1000.0 * rand () / (RAND_MAX + 1.0));
		GnomeVFSFileOffset   seekpos = (1000.0 * rand () / (RAND_MAX + 1.0));
		GnomeVFSSeekPosition w = (int)(2.0 * rand () / (RAND_MAX + 1.0));

		if (!seek_test_chunk (handle, ref, seekpos, w, length))
			g_warning ("test failed\n");
	}

	result = gnome_vfs_close (handle);
	show_result (result, "gnome_vfs_close", argv[1]);
	
	return 0;
}
