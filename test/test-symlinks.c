/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-directory-visit.c - Test program for the directory visiting functions
   of the GNOME Virtual File System.

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

   Author: Seth Nickell <snickell@stanford.edu> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <gnome.h>

#include "gnome-vfs.h"


static int
make_link (gchar *uri, gchar *target_reference, gchar *target_uri, GnomeVFSResult expected_result, gboolean unlink)
{
	GnomeVFSURI *real_uri, *real_uri_target;
	GnomeVFSResult result, error;
	char read_buffer[1024];
	const char *write_buffer = "this is test data...we should read the same thing";
	GnomeVFSHandle *handle;
	GnomeVFSFileSize bytes_written, temp;

	const gchar *result_string;
	int return_value = 1;

	real_uri = gnome_vfs_uri_new (uri);
	real_uri_target = gnome_vfs_uri_new (target_uri);

	result = gnome_vfs_create_symbolic_link (real_uri, target_reference);

	if (result != expected_result) {
		result_string = gnome_vfs_result_to_string (result);
		printf ("creating a link from %s to %s returned %s instead of %s.\n", uri, target_reference,
			result_string, gnome_vfs_result_to_string (expected_result));
		return_value = 0;
	} else if (result == GNOME_VFS_OK) { 
		/* our link seems to have been created correctly - lets see if its real */
		error = gnome_vfs_open_uri (&handle, real_uri_target, GNOME_VFS_OPEN_WRITE);
		if (error == GNOME_VFS_ERROR_NOT_FOUND) 
			error = gnome_vfs_create_uri (&handle, real_uri_target, GNOME_VFS_OPEN_WRITE, 0, GNOME_VFS_PERM_USER_ALL);
		if (error == GNOME_VFS_OK) {
			/* write stuff to our link location */
			error = gnome_vfs_write (handle, write_buffer, strlen (write_buffer) + 1, &bytes_written);
			error = gnome_vfs_close (handle);
			error = gnome_vfs_open_uri (&handle, real_uri, GNOME_VFS_OPEN_READ);
			if (error == GNOME_VFS_OK) {
				error = gnome_vfs_read (handle, read_buffer, bytes_written, &temp);
				read_buffer[temp] = 0;
				error = gnome_vfs_close (handle);
				if (strcmp (read_buffer, write_buffer) != 0) {
					printf ("Symlink problem: value written is not the same as the value read!\n");
					printf ("Written to %s: #%s# \nRead from link %s: #%s#\n", target_uri, write_buffer, uri, read_buffer);
					return_value = 0;
				}
			}
		}
	}
	if (unlink) {
		gnome_vfs_unlink_from_uri (real_uri_target);
		gnome_vfs_unlink_from_uri (real_uri);
	}
	
	gnome_vfs_uri_unref (real_uri);
	gnome_vfs_uri_unref (real_uri_target);

	return return_value;
}

int
main (int argc, char **argv)
{
	GnomeVFSURI *directory;
	
	if (argc != 2) {
		fprintf (stderr, "Usage: %s <directory>\n", argv[0]);
		return 1;
	}

	gnome_vfs_init ();
	directory = gnome_vfs_uri_new ("file:///tmp/tmp");

	gnome_vfs_make_directory_for_uri (directory, GNOME_VFS_PERM_USER_ALL);

	make_link ("file:///tmp/link_to_ditz", "file:///tmp/ditz", "file:///tmp/ditz", GNOME_VFS_OK, TRUE);
	make_link ("file:///tmp/link_to_ditz_relative", "ditz", "file:///tmp/ditz", GNOME_VFS_OK, TRUE);
	make_link ("file:///tmp/tmp/link_to_ditz", "../ditz", "file:///tmp/ditz", GNOME_VFS_OK, FALSE);
	make_link ("file:///tmp/link_to_link", "tmp/link_to_ditz", "file:///tmp/tmp/link_to_ditz", GNOME_VFS_OK, TRUE);
				
	gnome_vfs_remove_directory_from_uri (directory);

	make_link ("file:///tmp/link_to_ditz_offfs", "http://www.a.com/ditz", "http://www.a.com/ditz", GNOME_VFS_ERROR_NOT_SUPPORTED, TRUE);
	make_link ("http://www.eazel.com/link_to_ditz", "file:///tmp/ditz", "file:///tmp/ditz", GNOME_VFS_ERROR_NOT_SUPPORTED, TRUE);
	make_link ("http://www.a.com/link_to_ditz_relative", "ditz", "http://www.a.com/ditz", GNOME_VFS_ERROR_NOT_SUPPORTED, TRUE);

	gnome_vfs_uri_unref (directory);
	gnome_vfs_shutdown ();
	return 0;
}
