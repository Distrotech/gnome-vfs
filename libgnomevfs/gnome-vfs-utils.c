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

   `gnome_vfs_canonicalize_pathname()' derived from Midnight Commander code by
   Norbert Warmuth, Miguel de Icaza, Janne Kukonlehto, Dugan Porter, Jakub
   Jelinek.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

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


