/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-async-cancel.c - Test program for the GNOME Virtual File System.

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

   Authors: 
   	Darin Adler <darin@eazel.com>
	Ian McKellar <yakk@yakk.net.au>
*/

#include <config.h>

#include "gnome-vfs.h"
#include <stdlib.h>

#define TEST_ASSERT(expression, message) \
	G_STMT_START { if (!(expression)) test_failed message; } G_STMT_END

static void
stop_after_log (const char *domain, GLogLevelFlags level, 
	const char *message, gpointer data)
{
	void (* saved_handler) (int);
	
	g_log_default_handler (domain, level, message, data);

	saved_handler = signal (SIGINT, SIG_IGN);
	raise (SIGINT);
	signal (SIGINT, saved_handler);
}

static void
make_asserts_break (const char *domain)
{
	g_log_set_handler
		(domain, 
		 (GLogLevelFlags) (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING),
		 stop_after_log, NULL);
}

static gboolean at_least_one_test_failed = FALSE;

static void
test_failed (const char *format, ...)
{
	va_list arguments;
	char *message;

	va_start (arguments, format);
	message = g_strdup_vprintf (format, arguments);
	va_end (arguments);

	g_message ("test failed: %s", message);
	at_least_one_test_failed = TRUE;
}

static void
test_uri_to_string (const char *input,
		    const char *expected_output,
		    GnomeVFSURIHideOptions hide_options)
{
	GnomeVFSURI *uri;
	char *output;

	uri = gnome_vfs_uri_new (input);
	if (uri == NULL) {
		output = g_strdup ("NULL");
	} else {
		output = gnome_vfs_uri_to_string (uri, hide_options);
		gnome_vfs_uri_unref (uri);
	}

	if (strcmp (output, expected_output) != 0) {
		test_failed ("gnome_vfs_uri_to_string (%s, %d) resulted in %s instead of %s",
			     input, hide_options, output, expected_output);
	}

	g_free (output);
}

int
main (int argc, char **argv)
{
	make_asserts_break ("GLib");
	make_asserts_break ("GnomeVFS");

	/* Initialize the libraries we use. */
	g_thread_init (NULL);
	gnome_vfs_init ();

	test_uri_to_string ("", "NULL", GNOME_VFS_URI_HIDE_NONE);

	test_uri_to_string ("http://www.eazel.com", "http://www.eazel.com/", GNOME_VFS_URI_HIDE_NONE);
	test_uri_to_string ("http://www.eazel.com/", "http://www.eazel.com/", GNOME_VFS_URI_HIDE_NONE);
	test_uri_to_string ("http://www.eazel.com/dir", "http://www.eazel.com/dir", GNOME_VFS_URI_HIDE_NONE);
	test_uri_to_string ("http://www.eazel.com/dir/", "http://www.eazel.com/dir/", GNOME_VFS_URI_HIDE_NONE);
	test_uri_to_string ("http://yakk:womble@www.eazel.com:42/blah/", "http://yakk:womble@www.eazel.com:42/blah/", GNOME_VFS_URI_HIDE_NONE);

	test_uri_to_string ("http://yakk:womble@www.eazel.com:42/blah/", "http://:womble@www.eazel.com:42/blah/", GNOME_VFS_URI_HIDE_USER_NAME);

	/* FIXME: Do we want GnomeVFSURI to just refuse to deal with
         * URIs that we don't have a module for?
	 */
	test_uri_to_string ("glorp:", "NULL", GNOME_VFS_URI_HIDE_NONE);

	/* FIXME: Is this the correct behavior for these cases? */
	test_uri_to_string ("file:", "file://", GNOME_VFS_URI_HIDE_NONE);
	test_uri_to_string ("http:", "http://", GNOME_VFS_URI_HIDE_NONE);
	test_uri_to_string ("file:/", "file:///", GNOME_VFS_URI_HIDE_NONE);

	/* FIXME: URI schemes are not supposed to be case sensitive. */
	test_uri_to_string ("FILE://", "NULL" /* "file://" */, GNOME_VFS_URI_HIDE_NONE);

	/* FIXME: Do we really want to add the "///" in this case? */
	test_uri_to_string ("pipe:gnome-info2html2 as", "pipe:///gnome-info2html2 as", GNOME_VFS_URI_HIDE_NONE);

	/* Report to "make check" on whether it all worked or not. */
	return at_least_one_test_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
