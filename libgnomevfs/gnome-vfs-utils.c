/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-utils.c - Private utility functions for the GNOME Virtual
   File System.

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

   Author: Ettore Perazzoli <ettore@comm2000.it>

   `gnome_vfs_canonicalize_pathname()' derived from code by Brian Fox and Chet
   Ramey in GNU Bash, the Bourne Again SHell.  Copyright (C) 1987, 1988, 1989,
   1990, 1991, 1992 Free Software Foundation, Inc.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <glib.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


GList *
gnome_vfs_string_list_from_string_array (gchar *array[])
{
	GList *list;
	guint i;

	if (array == NULL)
		return NULL;

	for (i = 0; array[i] != NULL; i++)
		;

	list = NULL;
	do {
		i--;
		list = g_list_prepend (list, g_strdup (array[i]));
	} while (i > 0);

	return list;
}

void
gnome_vfs_free_string_list (GList *list)
{
	GList *p;

	if (list == NULL)
		return;

	for (p = list; p != NULL; p = p->next)
		g_free (list->data);
	g_list_free (list);
}


/* Canonicalize path, and return a new path.  Do everything in situ.  The new
   path differs from path in:

     Multiple `/'s are collapsed to a single `/'.
     Leading `./'s and trailing `/.'s are removed.
     Non-leading `../'s and trailing `..'s are handled by removing
     portions of the path.  */
gchar *
gnome_vfs_canonicalize_pathname (gchar *path)
{
	int i, start;
	gchar stub_gchar;

	if (path == NULL)
		return NULL;

	stub_gchar = ((*path == GNOME_VFS_URI_PATH_CHR)
		      ? GNOME_VFS_URI_PATH_CHR : '.');

	/* Walk along path looking for things to compact. */
	i = 0;
	for (;;) {
		if (!path[i])
			break;

		while (path[i] && path[i] != GNOME_VFS_URI_PATH_CHR)
			i++;

		start = i++;

		/* If we didn't find any slashes, then there is nothing left to do. */
		if (!path[start])
			break;

		/* Handle multiple `/'s in a row. */
		while (path[i] == GNOME_VFS_URI_PATH_CHR)
			i++;

		if ((start + 1) != i) {
			strcpy (path + start + 1, path + i);
			i = start + 1;
		}

		/* Handle backquoted `/'. */
		if (start > 0 && path[start - 1] == '\\')
			continue;

#if 0
		/* Check for trailing `/'. */
		if (start && !path[i]) {
		zero_last:
			path[--i] = '\0';
			break;
		}
#endif

		/* Check for `../', `./' or trailing `.' by itself. */
		if (path[i] == '.') {
			/* Handle trailing `.' by itself. */
			if (!path[i + 1]) {
				path[--i] = '\0';
				break;
			}

			/* Handle `./'. */
			if (path[i + 1] == GNOME_VFS_URI_PATH_CHR) {
				strcpy (path + i, path + i + 1);
				i = start;
				continue;
			}

			/* Handle `../' or trailing `..' by itself. 
			   Remove the previous ?/ part with the exception of
			   ../, which we should leave intact. */
			if (path[i + 1] == '.'
			    && (path[i + 2] == GNOME_VFS_URI_PATH_CHR
				|| path[i + 2] == '\0')) {
				while (start > 0) {
					start--;
					if (path[start] == GNOME_VFS_URI_PATH_CHR)
						break;
				}
				if (strncmp (path + start + 1, "../", 3) == 0)
					continue;
				strcpy (path + start + 1, path + i + 2);
				i = start;
				continue;
			}
		}
	}

	if (!*path) {
		*path = stub_gchar;
		path[1] = '\0';
	}

	return path;
}


static glong
get_max_fds (void)
{
#if defined _SC_OPEN_MAX
	return sysconf (_SC_OPEN_MAX);
#elif defined RLIMIT_NOFILE
	{
		struct rlimit rlimit;

		if (getrlimit (RLIMIT_NOFILE, &rlimit) == 0)
			return rlimit.rlim_max;
		else
			return -1;
	}
#elif defined HAVE_GETDTABLESIZE
	return getdtablesize();
#else
#warning Cannot determine the number of available file descriptors
	return 1024;		/* bogus */
#endif
}

/* Close all the currrently opened file descriptors.  */
static void
shut_down_file_descriptors (void)
{
	glong i, max_fds;

	max_fds = get_max_fds ();

	for (i = 3; i < max_fds; i++)
		close (i);
}

/* FIXME I am not sure the following stuff should be here.  This is a bit
   messy.  */

pid_t
gnome_vfs_forkexec (const gchar *file_name,
		    gchar *const argv[],
		    GnomeVFSProcessOptions options,
		    GnomeVFSProcessInitFunc init_func,
		    gpointer init_data)
{
	pid_t child_pid;

	child_pid = fork ();
	if (child_pid == 0) {
		if (init_func != NULL)
			(* init_func) (init_data);
		if (options & GNOME_VFS_PROCESS_SETSID)
			setsid ();
		if (options & GNOME_VFS_PROCESS_CLOSEFDS)
			shut_down_file_descriptors ();
		if (options & GNOME_VFS_PROCESS_USEPATH)
			execvp (file_name, argv);
		else
			execv (file_name, argv);
		_exit (1);
	}

	return child_pid;
}

/**
 * gnome_vfs_process_run_cancellable:
 * @file_name: Name of the executable to run
 * @argv: NULL-terminated argument list
 * @options: Options
 * @cancellation: Cancellation object
 * @return_value: Pointer to an integer that will contain the exit value
 * on return.
 * 
 * Run @file_name with argument list @argv, according to the specified
 * @options.
 * 
 * Return value: 
 **/
GnomeVFSProcessResult
gnome_vfs_process_run_cancellable (const gchar *file_name,
				   gchar *const argv[],
				   GnomeVFSProcessOptions options,
				   GnomeVFSCancellation *cancellation,
				   guint *exit_value)
{
	pid_t child_pid;

	child_pid = gnome_vfs_forkexec (file_name, argv, options, NULL, NULL);
	if (child_pid == -1)
		return GNOME_VFS_PROCESS_RUN_ERROR;

	while (1) {
		pid_t pid;
		int status;

		pid = waitpid (child_pid, &status, WUNTRACED);
		if (pid == -1) {
			if (errno != EINTR)
				return GNOME_VFS_PROCESS_RUN_ERROR;
			if (gnome_vfs_cancellation_check (cancellation)) {
				*exit_value = 0;
				return GNOME_VFS_PROCESS_RUN_CANCELLED;
			}
		} else if (pid == child_pid) {
			if (WIFEXITED (status)) {
				*exit_value = WEXITSTATUS (status);
				return GNOME_VFS_PROCESS_RUN_OK;
			}
			if (WIFSIGNALED (status)) {
				*exit_value = WTERMSIG (status);
				return GNOME_VFS_PROCESS_RUN_SIGNALED;
			}
			if (WIFSTOPPED (status)) {
				*exit_value = WSTOPSIG (status);
				return GNOME_VFS_PROCESS_RUN_SIGNALED;
			}
		}
	}

}


/**
 * gnome_vfs_create_temp:
 * @prefix: Prefix for the name of the temporary file
 * @name_return: Pointer to a pointer that, on return, will point to
 * the dynamically allocated name for the new temporary file created.
 * @fd_return: Pointer to a variable that will hold a file descriptor for
 * the new temporary file on return.
 * 
 * Create a temporary file whose name is prefixed with @prefix, and return an
 * open file descriptor for it in @*fd_return.
 * 
 * Return value: An integer value representing the result of the operation
 **/
GnomeVFSResult
gnome_vfs_create_temp (const gchar *prefix,
		       gchar **name_return,
		       gint *fd_return)
{
	gchar *name;
	gint fd;

	while (1) {
		name = tempnam (NULL, prefix);
		if (name == NULL)
			return GNOME_VFS_ERROR_INTERNAL;

		fd = open (name, O_WRONLY | O_CREAT | O_EXCL, 0700);
		if (fd != -1) {
			*name_return = name;
			*fd_return = fd;
			return GNOME_VFS_OK;
		}
	}
}
