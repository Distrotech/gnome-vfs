/* test-mime.c - Test for the mime type sniffing features of GNOME
   Virtual File System Library

   Copyright (C) 2000 Eazel

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

   Author: Pavel Cisler <pavel@eazel.com>
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-mime.h"

#include <stdio.h>

int
main (int argc, char **argv)
{
	GnomeVFSURI *uri;
	gboolean magic_only;
	gboolean suffix_only;
	const char *result;

	magic_only = FALSE;
	suffix_only = FALSE;
	
	if (!gnome_vfs_init ()) {
		fprintf (stderr, "Cannot initialize gnome-vfs.\n");
		return 1;
	}

	if (argc == 1) {
		fprintf (stderr, "Usage: %s [--magicOnly | --suffixOnly] fileToCheck1 [fileToCheck2 ...] \n", *argv);
		return 1;
	}

	++argv;
	if (strcmp (*argv, "--magicOnly") == 0) {
		magic_only = TRUE;
		++argv;
	} else if (strcmp (*argv, "--suffixOnly") == 0) {
		suffix_only = TRUE;
		++argv;
	}
	
	for (; *argv != NULL; argv++) {
		uri = gnome_vfs_uri_new (*argv);

		if (magic_only) {
			result = gnome_vfs_get_mime_type_from_file_data (uri);
		} else if (suffix_only) {
			result = gnome_vfs_get_mime_type_from_name (uri);
		} else {
			result = gnome_vfs_get_mime_type (uri);
		}
	
		printf ("looks like %s is %s\n", *argv, result);
		gnome_vfs_uri_unref (uri);
	}
	
	return 0;
}
