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

   Author: Ettore Perazzoli <ettore@comm2000.it> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
