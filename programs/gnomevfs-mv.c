/* gnomevfs-rm.c - Test for unlink() for gnome-vfs

   Copyright (C) 1999 Free Software Foundation
   Copyright (C) 2003, Red Hat

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

   Author: Ettore Perazzoli <ettore@gnu.org>
           Bastien Nocera <hadess@hadess.net>
*/

#include <libgnomevfs/gnome-vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
show_result (GnomeVFSResult result, const gchar *what, const gchar *from, const gchar *to)
{
	if (result != GNOME_VFS_OK) {
		fprintf (stderr, "%s `%s' `%s': %s\n",
				what, from, to,
				gnome_vfs_result_to_string (result));
		exit (1);
	}
}

int
main (int argc, char **argv)
{
	GnomeVFSResult    result;
	GnomeVFSURI 	 *from, *to;
	gchar            *text_from, *text_to;

	if (argc != 3) {
		fprintf (stderr, "Usage: %s <from> <to>\n", argv[0]);
		return 1;
	}

	if (! gnome_vfs_init ()) {
		fprintf (stderr, "Cannot initialize gnome-vfs.\n");
		return 1;
	}

	from = gnome_vfs_uri_new (argv[1]);
	if (from == NULL) {
		fprintf (stderr, "URI %s not valid.\n", argv[1]);
		return 1;
	}

	to = gnome_vfs_uri_new (argv[2]);
	if (to == NULL) {
		fprintf (stderr, "URI %s not valid.\n", argv[2]);
		return 1;
	}

	text_from = gnome_vfs_uri_to_string (from, GNOME_VFS_URI_HIDE_NONE);
	text_to = gnome_vfs_uri_to_string (to, GNOME_VFS_URI_HIDE_NONE);

	result = gnome_vfs_move_uri (from, to, TRUE);
	show_result (result, "move", text_from, text_to);

	g_free (text_from);
	g_free (text_to);

	return 0;
}

