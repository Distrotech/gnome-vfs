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

   Author: Darin Adler <darin@eazel.com>
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
test_escape (const char *input, const char *expected_output)
{
	char *output;

	output = gnome_vfs_escape_string (input, GNOME_VFS_URI_ENCODING_XALPHAS);
	if (strcmp (output, expected_output) == 0) {
		g_free (output);
		return;
	}

	test_failed ("escaping %s resulted in %s intead of %s",
		     input,
		     output,
		     expected_output);
	g_free (output);
}

int
main (int argc, char **argv)
{
	make_asserts_break ("GnomeVFS");

	/* Initialize the libraries we use. */
	g_thread_init (NULL);
	gnome_vfs_init ();

	test_escape ("", "");
	test_escape ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_escape ("abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	test_escape ("0123456789", "0123456789");
#if 0
	test_escape ("!@#$%^&*()-=_+", "!@#$%^&*()-=_+");
#endif

	/* Report to "make check" on whether it all worked or not. */
	return at_least_one_test_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
