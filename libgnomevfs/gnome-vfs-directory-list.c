/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-directory-list.c - Support for directory lists in the
   GNOME Virtual File System.

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

/* Get the definition of `FNM_CASEFOLD'.  FIXME */
#define _POSIX_SOURCE

#include <unistd.h>
#include <fnmatch.h>
#include <regex.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


struct _GnomeVFSDirectoryList {
	GList *entries;		/* GnomeVFSFileInfo */
	GList *current_entry;
	GList *last_entry;
};


static void
remove_entry (GnomeVFSDirectoryList *list,
	      GList *p)
{
	GnomeVFSFileInfo *info;

	info = p->data;
	gnome_vfs_file_info_destroy (info);

	if (list->current_entry == p)
		list->current_entry = NULL;
	if (list->last_entry == p)
		list->last_entry = p->prev;
	list->entries = g_list_remove_link (list->entries, p);

	g_list_free (p);
}


static gint
compare_for_sort (const GnomeVFSFileInfo *a,
		  const GnomeVFSFileInfo *b,
		  gconstpointer data)
{
	const GnomeVFSDirectoryFilterType *sort_rules;
	guint i;
	gint retval;

	sort_rules = data;

	for (i = 0; sort_rules[i] != GNOME_VFS_DIRECTORY_SORT_NONE; i++) {
		switch (sort_rules[i]) {
		case GNOME_VFS_DIRECTORY_SORT_DIRECTORYFIRST:
			if (a->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				if (b->type != GNOME_VFS_FILE_TYPE_DIRECTORY)
					return -1;
			} else if (b->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				return +1;
			}
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYNAME:
			retval = strcmp (a->name, b->name);
			if (retval != 0)
				return retval;
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYNAME_IGNORECASE:
			retval = g_strcasecmp (a->name, b->name);
			if (retval != 0)
				return retval;
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYSIZE:
			if (a->size != b->size)
				return (a->size < b->size) ? -1 : +1;
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYBLOCKCOUNT:
			if (a->block_count != b->block_count)
				return ((a->block_count < b->block_count)
					? -1 : +1);
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYATIME:
			if (a->atime != b->atime)
				return (a->atime < b->atime) ? -1 : +1;
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYMTIME:
			if (a->mtime != b->mtime)
				return (a->mtime < b->mtime) ? -1 : +1;
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYCTIME:
			if (a->ctime != b->ctime)
				return (a->ctime < b->ctime) ? -1 : +1;
			break;
		case GNOME_VFS_DIRECTORY_SORT_BYMIMETYPE:
			retval = g_strcasecmp (a->mime_type, b->mime_type);
			if (retval != 0)
				return retval;
			break;
		default:
			g_warning (_("Unknown sort rule %d"), sort_rules[i]);
		}
	}

	return 0;
}

static gint
compare_for_sort_reversed (const GnomeVFSFileInfo *a,
			   const GnomeVFSFileInfo *b,
			   const GnomeVFSDirectoryFilterType *sort_rules)
{
	return 0 - compare_for_sort (a, b, sort_rules);
}


GnomeVFSDirectoryList *
gnome_vfs_directory_list_new (void)
{
	GnomeVFSDirectoryList *new;

	new = g_new (GnomeVFSDirectoryList, 1);

	new->entries = NULL;
	new->current_entry = NULL;
	new->last_entry = NULL;

	return new;
}

void
gnome_vfs_directory_list_destroy (GnomeVFSDirectoryList *list)
{
	GList *p;

	g_return_if_fail (list != NULL);

	if (list->entries != NULL) {
		for (p = list->entries; p != NULL; p = p->next) {
			GnomeVFSFileInfo *info;

			info = p->data;
			gnome_vfs_file_info_destroy (info);
		}
		g_list_free (list->entries);
	}

	g_free (list);
}

void
gnome_vfs_directory_list_prepend (GnomeVFSDirectoryList *list,
				  GnomeVFSFileInfo *info)
{
	g_return_if_fail (list != NULL);
	g_return_if_fail (info != NULL);

	list->entries = g_list_prepend (list->entries, info);
	if (list->last_entry == NULL)
		list->last_entry = list->entries;
}

void
gnome_vfs_directory_list_append (GnomeVFSDirectoryList *list,
				 GnomeVFSFileInfo *info)
{
	g_return_if_fail (list != NULL);
	g_return_if_fail (info != NULL);

	if (list->entries == NULL) {
		list->entries = g_list_alloc ();
		list->entries->data = info;
		list->last_entry = list->entries;
	} else {
		g_list_append (list->last_entry, info);
		list->last_entry = list->last_entry->next;
	}
}


GnomeVFSFileInfo *
gnome_vfs_directory_list_first (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	list->current_entry = list->entries;
	if (list->current_entry == NULL)
		return NULL;

	return list->current_entry->data;
}

GnomeVFSFileInfo *
gnome_vfs_directory_list_last (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	list->current_entry = list->last_entry;
	if (list->current_entry == NULL)
		return NULL;

	return list->current_entry->data;
}

GnomeVFSFileInfo *
gnome_vfs_directory_list_next (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	if (list->current_entry == NULL)
		return NULL;

	list->current_entry = list->current_entry->next;
	if (list->current_entry == NULL)
		return NULL;

	return list->current_entry->data;
}

GnomeVFSFileInfo *
gnome_vfs_directory_list_prev (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	if (list->current_entry == NULL)
		return NULL;

	list->current_entry = list->current_entry->prev;
	if (list->current_entry == NULL)
		return NULL;

	return list->current_entry->data;
}

GnomeVFSFileInfo *
gnome_vfs_directory_list_current (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	if (list->current_entry == NULL)
		return NULL;

	return list->current_entry->data;
}

GnomeVFSFileInfo *
gnome_vfs_directory_list_nth (GnomeVFSDirectoryList *list, guint n)
{
	g_return_val_if_fail (list != NULL, NULL);

	list->current_entry = g_list_nth (list->entries, n);
	if (list->current_entry == NULL)
		return NULL;

	return list->current_entry->data;
}


void
gnome_vfs_directory_list_filter	(GnomeVFSDirectoryList *list,
				 GnomeVFSDirectoryFilter *filter)
{
	GList *p;

	g_return_if_fail (list != NULL);

	if (filter == NULL)
		return;

	p = list->entries;
	while (p != NULL) {
		GnomeVFSFileInfo *info;
		GList *pnext;

		info = p->data;
		pnext = p->next;

		if (! gnome_vfs_directory_filter_apply (filter, info))
			remove_entry (list, p);

		p = pnext;
	}
}


void
gnome_vfs_directory_list_sort (GnomeVFSDirectoryList *list,
			       gboolean reversed,
			       const GnomeVFSDirectorySortRule *rules)
{
	GnomeVFSListCompareFunc func;

	g_return_if_fail (list != NULL);
	g_return_if_fail (rules[0] != GNOME_VFS_DIRECTORY_SORT_NONE);

	if (reversed)
		func = (GnomeVFSListCompareFunc) compare_for_sort_reversed;
	else
		func = (GnomeVFSListCompareFunc) compare_for_sort;

	gnome_vfs_list_sort (list->entries, func, (gpointer) rules);
}

void
gnome_vfs_directory_list_sort_custom (GnomeVFSDirectoryList *list,
				      GnomeVFSDirectorySortFunc func,
				      gpointer data)
{
	g_return_if_fail (list != NULL);
	g_return_if_fail (func != NULL);

	gnome_vfs_list_sort (list->entries, (GnomeVFSListCompareFunc) func,
			     data);
}


GnomeVFSDirectoryListPosition
gnome_vfs_directory_list_get_position (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	return list->current_entry;
}

void
gnome_vfs_directory_list_set_position (GnomeVFSDirectoryList *list,
				       GnomeVFSDirectoryListPosition position)
{
	g_return_if_fail (list != NULL);
	g_return_if_fail (position != NULL);

	list->current_entry = position;
}

GnomeVFSDirectoryListPosition
gnome_vfs_directory_list_get_last_position (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	return list->last_entry;
}

GnomeVFSDirectoryListPosition
gnome_vfs_directory_list_get_first_position (GnomeVFSDirectoryList *list)
{
	g_return_val_if_fail (list != NULL, NULL);

	return list->entries;
}

GnomeVFSDirectoryListPosition
gnome_vfs_directory_list_position_next (GnomeVFSDirectoryListPosition position)
{
	GList *list;

	g_return_val_if_fail (position != NULL, NULL);

	list = position;
	return list->next;
}

GnomeVFSDirectoryListPosition
gnome_vfs_directory_list_position_prev (GnomeVFSDirectoryListPosition position)
{
	GList *list;

	g_return_val_if_fail (position != NULL, NULL);

	list = position;
	return list->prev;
}
