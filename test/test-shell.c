/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-shell.c - A small program to allow testing of a wide variety of
   gnome-vfs functionality

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

   Author: Michael Meeks <mmeeks@gnu.org> 
   
   NB. This code leaks everywhere, don't loose hair.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>

#include "gnome-vfs.h"

#define TEST_DEBUG 0

static char delim[]=" ";
static char **arg_data = NULL;
static int    arg_cur  = 0;

static char *cur_dir = NULL;

#if 0
static void
show_result (GnomeVFSResult result, const gchar *what, const gchar *text_uri)
{
	fprintf (stderr, "%s `%s': %s\n",
		 what, text_uri, gnome_vfs_result_to_string (result));
	if (result != GNOME_VFS_OK)
		exit (1);
}
#endif

static gboolean
show_if_error (GnomeVFSResult result, const char *what, const char *what2)
{
	if (result != GNOME_VFS_OK) {
		fprintf (stderr, "%s%s: `%s'\n",
			 what, what2, gnome_vfs_result_to_string (result));
		return TRUE;
	} else
		return FALSE;
}


static void
do_ls (void)
{
	GnomeVFSResult         result;
	GnomeVFSDirectoryList *list;
	GnomeVFSFileInfo      *info;

	result = gnome_vfs_directory_list_load (
		&list, cur_dir, GNOME_VFS_FILE_INFO_DEFAULT,
		NULL, NULL);
	if (show_if_error (result, "open directory ", cur_dir))
		return;

	for (info = gnome_vfs_directory_list_first (list);
	     info; info = gnome_vfs_directory_list_next (list)) {
		char prechar = '\0', postchar = '\0';

		/* FIXME: Bug no.1 type field invalid */
		if (1 || info->type & GNOME_VFS_FILE_INFO_FIELDS_TYPE) {
			switch (info->type) {
			case GNOME_VFS_FILE_TYPE_DIRECTORY:
				prechar = '[';
				postchar = ']';
				break;
			case GNOME_VFS_FILE_TYPE_UNKNOWN:
				prechar = '?';
				break;
			case GNOME_VFS_FILE_TYPE_FIFO:
				prechar = '|';
				break;
			case GNOME_VFS_FILE_TYPE_SOCKET:
				prechar = '-';
				break;
			case GNOME_VFS_FILE_TYPE_CHARACTER_DEVICE:
			case GNOME_VFS_FILE_TYPE_BLOCK_DEVICE:
				prechar = '@';
				break;
			case GNOME_VFS_FILE_TYPE_BROKEN_SYMBOLIC_LINK:
				prechar = '#';
				break;
			default:
				prechar = '\0';
				break;
			}
			if (!postchar)
				postchar = prechar;
		}
		printf ("%c%s%c", prechar, info->name, postchar);
		if (strlen (info->name) < 40) {
			int i;
			for (i = 0; i < 40 - strlen (info->name); i++)
				printf (" ");
		}
		if (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) {
			long i = info->size;
			printf (" : %ld bytes", i);
		}
		printf ("\n");
	}
	
	gnome_vfs_directory_list_destroy (list);
}

static void
list_commands ()
{
	printf ("command can be one or all of:\n");
	printf (" * ls:                   list files\n");
	printf (" * cd:                   enter storage\n");
	printf (" * cat     <stream name>:   dump text file to console\n");
	printf (" * dump    <stream name>:   dump binary file to console\n");
	printf (" * quit,exit,bye:        exit\n");
}

#if 0
static void
syntax_error (char *err)
{
	if (err) {
		printf("Error: '%s'\n",err);
		exit(1);
	}
		
	printf ("Sytax:\n");
	printf (" ole <ole-file> [-i] [commands...]\n\n");
	printf (" -i: Interactive, queries for fresh commands\n\n");

	list_commands ();
	exit(1);
}
#endif

static gboolean
simple_regexp (const char *regexp, const char *fname)
{
	int      i;
	gboolean ret = TRUE;

	g_return_val_if_fail (fname != NULL, FALSE);
	g_return_val_if_fail (regexp != NULL, FALSE);

	for (i = 0; regexp [i] && fname [i]; i++) {
		if (regexp [i] == '.')
			continue;

		if (toupper (regexp [i]) != toupper (fname [i])) {
			ret = FALSE;
			break;
		}
	}

	if (regexp [i] && regexp [i] == '*')
		ret = TRUE;

	else if (!regexp [i] && fname [i])
		ret = FALSE;

/*	if (ret)
	printf ("'%s' matched '%s'\n", regexp, fname);*/

	return ret;
}

static gboolean
validate_path (const char *path)
{
	GnomeVFSResult         result;
	GnomeVFSDirectoryList *list;

	result = gnome_vfs_directory_list_load (
		&list, path, GNOME_VFS_FILE_INFO_DEFAULT,
		NULL, NULL);
	if (show_if_error (result, "open directory ", path))
		return FALSE;

	gnome_vfs_directory_list_destroy (list);

	return TRUE;
}

static char *
get_regexp_name (const char *regexp, const char *path, gboolean dir)
{
	GnomeVFSResult         result;
	GnomeVFSDirectoryList *list;
	GnomeVFSFileInfo      *info;
	char                  *res = NULL;

	result = gnome_vfs_directory_list_load (
		&list, path, GNOME_VFS_FILE_INFO_DEFAULT,
		NULL, NULL);
	if (show_if_error (result, "open directory ", path))
		return NULL;

	for (info = gnome_vfs_directory_list_first (list);
	     info; info = gnome_vfs_directory_list_next (list)) {

		if (simple_regexp (regexp, info->name)) {
			if ((dir  && info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) ||
			    (!dir && info->type != GNOME_VFS_FILE_TYPE_DIRECTORY)) {
				res = g_strdup (info->name);
				break;
			}
		}
	}
	gnome_vfs_directory_list_destroy (list);

	return res;
}

static void
do_cd (void)
{
	char *p;

	p = arg_data [arg_cur++];

	if (!p) {
		printf ("Takes a directory argument\n");
		return;
	}

	if (!g_strcasecmp (p, "..")) {
		guint lp;
		char **tmp;
		GString *newp = g_string_new ("");

		tmp = g_strsplit (cur_dir, "/", -1);
		lp  = 0;
		if (!tmp[lp])
			return;

		while (tmp[lp+1]) {
			g_string_sprintfa (newp, "%s/", tmp[lp]);
			lp++;
		}
		g_free (cur_dir);
		cur_dir = newp->str;
		g_string_free (newp, FALSE);
	} else {
		char *newpath;

		if (strchr (p, ':') ||
		    p [0] == '/')
			newpath = g_strdup (p);
		else {
			char *ptr;
			
			ptr = get_regexp_name (p, cur_dir, TRUE);
			if (!ptr)
				return;

			newpath = g_strconcat (cur_dir, ptr, "/", NULL);
		}

		if (validate_path (newpath)) {
			g_free (cur_dir);
			cur_dir = newpath;
		}
	}
}

static char *
get_fname (void)
{
	char *fname, *f;

	if (!arg_data [arg_cur])
		return NULL;
	
	fname = arg_data [arg_cur++];

	if (cur_dir)
		f = g_strconcat (cur_dir, fname, NULL);
	else
		f = g_strdup (fname);

	return f;
}

static void
do_cat (void)
{
	char *from;
	GnomeVFSHandle *from_handle;
	GnomeVFSResult  result;

	from = get_fname ();

	result = gnome_vfs_open (&from_handle, from, GNOME_VFS_OPEN_READ);
	if (show_if_error (result, "open ", from))
		return;

	while (1) {
		GnomeVFSFileSize bytes_read;
		guint8           data [1025];
		
		result = gnome_vfs_read (from_handle, data, 1024, &bytes_read);
		if (show_if_error (result, "read ", from))
			return;

		if (bytes_read == 0)
			break;
		
		data [1024] = '\0';
		fprintf (stdout, "%s", data);
	}

	result = gnome_vfs_close (from_handle);
	if (show_if_error (result, "close ", from))
		return;
	fprintf (stdout, "\n");
}

static void
do_cp (void)
{
	char *from;
	char *to;
	GnomeVFSHandle *from_handle;
	GnomeVFSHandle *to_handle;
	GnomeVFSResult  result;

	from = get_fname ();

	if (from)
		to = get_fname ();
	else {
		fprintf (stderr, "cp <from> <to>\n");
		return;
	}
       
	result = gnome_vfs_open (&from_handle, from, GNOME_VFS_OPEN_READ);
	if (show_if_error (result, "open ", from))
		return;

	result = gnome_vfs_open (&to_handle, to, GNOME_VFS_OPEN_WRITE);
	if (result == GNOME_VFS_ERROR_NOT_FOUND)
		result = gnome_vfs_create (&to_handle, to, GNOME_VFS_OPEN_WRITE, FALSE,
					   GNOME_VFS_PERM_USER_ALL);
	if (show_if_error (result, "open ", to))
		return;

	while (1) {
		GnomeVFSFileSize bytes_read;
		GnomeVFSFileSize bytes_written;
		guint8           data [1024];
		
		result = gnome_vfs_read (from_handle, data, 1024, &bytes_read);
		if (show_if_error (result, "read ", from))
			return;

		if (bytes_read == 0)
			break;
		
		result = gnome_vfs_write (to_handle, data, bytes_read, &bytes_written);
		if (show_if_error (result, "write ", to))
			return;

		if (bytes_read != bytes_written)
			fprintf (stderr, "Didn't write it all");
	}

	result = gnome_vfs_close (to_handle);
	if (show_if_error (result, "close ", to))
		return;

	result = gnome_vfs_close (from_handle);
	if (show_if_error (result, "close ", from))
		return;
}

static void
do_dump (void)
{
	g_warning ("Implement dump");
}

int
main (int argc, char **argv)
{
	int exit = 0, interact = 0;
	char *buffer = g_new (char, 1024) ;

	cur_dir = g_strdup (getcwd (NULL, 0));

	if (cur_dir && cur_dir [strlen (cur_dir)] != '/')
		cur_dir = g_strconcat (cur_dir, "/", NULL);

	if (!gnome_vfs_init ()) {
		fprintf (stderr, "Cannot initialize gnome-vfs.\n");
		return 1;
	}

#if 0
	if (argc > 1 && argv [argc - 1] [0] == '-'
	    && argv [argc - 1] [1] == 'i') 
#endif
		interact = 1;
#if 0
	else {
		char *str=g_strdup(argv[1]) ;
		for (lp=2;lp<argc;lp++)
			str = g_strconcat(str," ",argv[lp],NULL); /* FIXME Mega leak :-) */
		buffer = str; /* and again */
	}
#endif

	do
	{
		char *ptr;

		if (interact) {
			fprintf (stdout,"> ");
			fflush (stdout);
			fgets (buffer, 1023, stdin);
		}

		arg_data = g_strsplit (g_strchomp (buffer), delim, -1);
		arg_cur  = 0;
		if (!arg_data && interact) continue;
		if (!interact)
			printf ("Command : '%s'\n", arg_data[0]);
		ptr = arg_data[arg_cur++];
		if (!ptr)
			continue;

		if (g_strcasecmp (ptr, "ls") == 0)
			do_ls ();
		else if (g_strcasecmp (ptr, "cd") == 0)
			do_cd ();
		else if (g_strcasecmp (ptr, "dump") == 0)
			do_dump ();
		else if (g_strcasecmp (ptr, "type") == 0 ||
			 g_strcasecmp (ptr, "cat") == 0)
			do_cat ();
		else if (g_strcasecmp (ptr, "cp") == 0)
			do_cp ();
		else if (g_strcasecmp (ptr,"help") == 0 ||
			 g_strcasecmp (ptr,"?")    == 0 ||
			 g_strcasecmp (ptr,"info") == 0 ||
			 g_strcasecmp (ptr,"man")  == 0)
			list_commands ();
		else if (g_strcasecmp (ptr,"exit") == 0 ||
			 g_strcasecmp (ptr,"quit") == 0 ||
			 g_strcasecmp (ptr,"q")    == 0 ||
			 g_strcasecmp (ptr,"bye") == 0)
			exit = 1;
	}
	while (!exit && interact);

	return 0;
}
