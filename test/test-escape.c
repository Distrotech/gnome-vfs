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
test_escape (int mask, const char *input, const char *expected_output)
{
	char *output;

	output = gnome_vfs_escape_string (input, mask);

	if (strcmp (output, expected_output) != 0) {
		test_failed ("escaping %s resulted in %s instead of %s",
			     input, output, expected_output);
	}

	g_free (output);
}

static void
test_unescape (const char *input, const char *illegal, const char *expected_output)
{
	char *output;

	output = gnome_vfs_unescape_string (input, illegal);
	if (expected_output == NULL) {
		if (output != NULL) {
			test_failed ("escaping %s resulted in %s instead of NULL",
				     input, illegal, output);
		}
	} else {
		if (output == NULL) {
			test_failed ("escaping %s resulted in NULL instead of %s",
				     input, illegal, expected_output);
		} else if (strcmp (output, expected_output) != 0) {
			test_failed ("escaping %s with %s illegal resulted in %s instead of %s",
				     input, illegal, output, expected_output);
		}
	}

	g_free (output);
}

int
main (int argc, char **argv)
{
	make_asserts_break ("GnomeVFS");

	/* Initialize the libraries we use. */
	g_thread_init (NULL);
	gnome_vfs_init ();

	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "", "");

	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "0123456789", "0123456789");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "-_.!~*'()", "-_.!~*'()");

	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\x01\x02\x03\x04\x05\x06\x07", "%01%02%03%04%05%06%07");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F", "%08%09%0A%0B%0C%0D%0E%0F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, " \"#$%&+,/", "%20%22%23%24%25%26%2B%2C%2F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, ":;<=>?@", "%3A%3B%3C%3D%3E%3F%40");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "[\\]^`", "%5B%5C%5D%5E%60");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "{|}\x7F", "%7B%7C%7D%7F");

	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\x80\x81\x82\x83\x84\x85\x86\x87", "%80%81%82%83%84%85%86%87");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F", "%88%89%8A%8B%8C%8D%8E%8F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\x90\x91\x92\x93\x94\x95\x96\x97", "%90%91%92%93%94%95%96%97");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F", "%98%99%9A%9B%9C%9D%9E%9F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7", "%A0%A1%A2%A3%A4%A5%A6%A7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF", "%A8%A9%AA%AB%AC%AD%AE%AF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7", "%B0%B1%B2%B3%B4%B5%B6%B7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF", "%B8%B9%BA%BB%BC%BD%BE%BF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7", "%C0%C1%C2%C3%C4%C5%C6%C7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF", "%C8%C9%CA%CB%CC%CD%CE%CF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7", "%D0%D1%D2%D3%D4%D5%D6%D7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF", "%D8%D9%DA%DB%DC%DD%DE%DF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7", "%E0%E1%E2%E3%E4%E5%E6%E7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF", "%E8%E9%EA%EB%EC%ED%EE%EF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7", "%F0%F1%F2%F3%F4%F5%F6%F7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALL, "\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF", "%F8%F9%FA%FB%FC%FD%FE%FF");

	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "", "");

	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "0123456789", "0123456789");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "-_.!~*'()+", "-_.!~*'()+");

	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\x01\x02\x03\x04\x05\x06\x07", "%01%02%03%04%05%06%07");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F", "%08%09%0A%0B%0C%0D%0E%0F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, " \"#$%&,/", "%20%22%23%24%25%26%2C%2F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, ":;<=>?@", "%3A%3B%3C%3D%3E%3F%40");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "[\\]^`", "%5B%5C%5D%5E%60");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "{|}\x7F", "%7B%7C%7D%7F");

	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\x80\x81\x82\x83\x84\x85\x86\x87", "%80%81%82%83%84%85%86%87");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F", "%88%89%8A%8B%8C%8D%8E%8F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\x90\x91\x92\x93\x94\x95\x96\x97", "%90%91%92%93%94%95%96%97");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F", "%98%99%9A%9B%9C%9D%9E%9F");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7", "%A0%A1%A2%A3%A4%A5%A6%A7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF", "%A8%A9%AA%AB%AC%AD%AE%AF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7", "%B0%B1%B2%B3%B4%B5%B6%B7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF", "%B8%B9%BA%BB%BC%BD%BE%BF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7", "%C0%C1%C2%C3%C4%C5%C6%C7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF", "%C8%C9%CA%CB%CC%CD%CE%CF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7", "%D0%D1%D2%D3%D4%D5%D6%D7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF", "%D8%D9%DA%DB%DC%DD%DE%DF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7", "%E0%E1%E2%E3%E4%E5%E6%E7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF", "%E8%E9%EA%EB%EC%ED%EE%EF");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7", "%F0%F1%F2%F3%F4%F5%F6%F7");
	test_escape (GNOME_VFS_URI_UNSAFE_ALLOW_PLUS, "\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF", "%F8%F9%FA%FB%FC%FD%FE%FF");

	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "", "");

	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "0123456789", "0123456789");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "-_.!~*'()+/", "-_.!~*'()+/");

	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\x01\x02\x03\x04\x05\x06\x07", "%01%02%03%04%05%06%07");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F", "%08%09%0A%0B%0C%0D%0E%0F");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, " \"#$%&,", "%20%22%23%24%25%26%2C");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, ":;<=>?@", "%3A%3B%3C%3D%3E%3F%40");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "[\\]^`", "%5B%5C%5D%5E%60");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "{|}\x7F", "%7B%7C%7D%7F");

	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\x80\x81\x82\x83\x84\x85\x86\x87", "%80%81%82%83%84%85%86%87");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F", "%88%89%8A%8B%8C%8D%8E%8F");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\x90\x91\x92\x93\x94\x95\x96\x97", "%90%91%92%93%94%95%96%97");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F", "%98%99%9A%9B%9C%9D%9E%9F");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7", "%A0%A1%A2%A3%A4%A5%A6%A7");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF", "%A8%A9%AA%AB%AC%AD%AE%AF");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7", "%B0%B1%B2%B3%B4%B5%B6%B7");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF", "%B8%B9%BA%BB%BC%BD%BE%BF");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7", "%C0%C1%C2%C3%C4%C5%C6%C7");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF", "%C8%C9%CA%CB%CC%CD%CE%CF");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7", "%D0%D1%D2%D3%D4%D5%D6%D7");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF", "%D8%D9%DA%DB%DC%DD%DE%DF");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7", "%E0%E1%E2%E3%E4%E5%E6%E7");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF", "%E8%E9%EA%EB%EC%ED%EE%EF");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7", "%F0%F1%F2%F3%F4%F5%F6%F7");
	test_escape (GNOME_VFS_URI_UNSAFE_PATH, "\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF", "%F8%F9%FA%FB%FC%FD%FE%FF");
	
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "", "");

	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "0123456789", "0123456789");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "-_.!~*'()+/:", "-_.!~*'()+/:");

	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\x01\x02\x03\x04\x05\x06\x07", "%01%02%03%04%05%06%07");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F", "%08%09%0A%0B%0C%0D%0E%0F");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, " \"#$%&,", "%20%22%23%24%25%26%2C");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, ";<=>?@", "%3B%3C%3D%3E%3F%40");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "[\\]^`", "%5B%5C%5D%5E%60");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "{|}\x7F", "%7B%7C%7D%7F");

	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\x80\x81\x82\x83\x84\x85\x86\x87", "%80%81%82%83%84%85%86%87");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F", "%88%89%8A%8B%8C%8D%8E%8F");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\x90\x91\x92\x93\x94\x95\x96\x97", "%90%91%92%93%94%95%96%97");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F", "%98%99%9A%9B%9C%9D%9E%9F");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7", "%A0%A1%A2%A3%A4%A5%A6%A7");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF", "%A8%A9%AA%AB%AC%AD%AE%AF");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7", "%B0%B1%B2%B3%B4%B5%B6%B7");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF", "%B8%B9%BA%BB%BC%BD%BE%BF");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7", "%C0%C1%C2%C3%C4%C5%C6%C7");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF", "%C8%C9%CA%CB%CC%CD%CE%CF");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7", "%D0%D1%D2%D3%D4%D5%D6%D7");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF", "%D8%D9%DA%DB%DC%DD%DE%DF");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7", "%E0%E1%E2%E3%E4%E5%E6%E7");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF", "%E8%E9%EA%EB%EC%ED%EE%EF");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7", "%F0%F1%F2%F3%F4%F5%F6%F7");
	test_escape (GNOME_VFS_URI_UNSAFE_DOS_PATH, "\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF", "%F8%F9%FA%FB%FC%FD%FE%FF");

	test_unescape ("", NULL, "");
	test_unescape ("", "", "");
	test_unescape ("", "/", "");
	test_unescape ("", "/:", "");

	test_unescape ("/", "/", "/");
	test_unescape ("%2F", "/", NULL);

	test_unescape ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_unescape ("abcdefghijklmnopqrstuvwxyz", NULL, "abcdefghijklmnopqrstuvwxyz");
	test_unescape ("0123456789", NULL, "0123456789");
	test_unescape ("-_.!~*'()", NULL, "-_.!~*'()");

	test_unescape ("%01%02%03%04%05%06%07", NULL, "\x01\x02\x03\x04\x05\x06\x07");
	test_unescape ("%08%09%0A%0B%0C%0D%0E%0F", NULL, "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F");
	test_unescape ("%20%22%23%24%25%26%2B%2C%2F", NULL, " \"#$%&+,/");
	test_unescape ("%3A%3B%3C%3D%3E%3F%40", NULL, ":;<=>?@");
	test_unescape ("%5B%5C%5D%5E%60", NULL, "[\\]^`");
	test_unescape ("%7B%7C%7D%7F", NULL, "{|}\x7F");
	
	test_unescape ("%80%81%82%83%84%85%86%87", NULL, "\x80\x81\x82\x83\x84\x85\x86\x87");
	test_unescape ("%88%89%8A%8B%8C%8D%8E%8F", NULL, "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F");
	test_unescape ("%90%91%92%93%94%95%96%97", NULL, "\x90\x91\x92\x93\x94\x95\x96\x97");
	test_unescape ("%98%99%9A%9B%9C%9D%9E%9F", NULL, "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F");
	test_unescape ("%A0%A1%A2%A3%A4%A5%A6%A7", NULL, "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7");
	test_unescape ("%A8%A9%AA%AB%AC%AD%AE%AF", NULL, "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF");
	test_unescape ("%B0%B1%B2%B3%B4%B5%B6%B7", NULL, "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7");
	test_unescape ("%B8%B9%BA%BB%BC%BD%BE%BF", NULL, "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF");
	test_unescape ("%C0%C1%C2%C3%C4%C5%C6%C7", NULL, "\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7");
	test_unescape ("%C8%C9%CA%CB%CC%CD%CE%CF", NULL, "\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF");
	test_unescape ("%D0%D1%D2%D3%D4%D5%D6%D7", NULL, "\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7");
	test_unescape ("%D8%D9%DA%DB%DC%DD%DE%DF", NULL, "\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF");
	test_unescape ("%E0%E1%E2%E3%E4%E5%E6%E7", NULL, "\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7");
	test_unescape ("%E8%E9%EA%EB%EC%ED%EE%EF", NULL, "\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF");
	test_unescape ("%F0%F1%F2%F3%F4%F5%F6%F7", NULL, "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7");
	test_unescape ("%F8%F9%FA%FB%FC%FD%FE%FF", NULL, "\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF");
	
	/* Report to "make check" on whether it all worked or not. */
	return at_least_one_test_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
