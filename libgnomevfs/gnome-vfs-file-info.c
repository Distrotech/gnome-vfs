/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-file-info.c - Handling of file information for the GNOME
   Virtual File System.

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
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

/* WARNING: We assume that NULL metadata is different from unspecified
   metadata.  */


static void
free_metadata (GList *p)
{
	GnomeVFSFileMetadata *meta;

	meta = p->data;
	g_free (meta->key);
	g_free (meta->value);
}

static void
remove_metadata (GnomeVFSFileInfo *info,
		 GList *p)
{
	free_metadata (p);
	info->metadata_list = g_list_remove_link (info->metadata_list, p);
	g_list_free (p);
}

static void
free_metadata_list (GnomeVFSFileInfo *info)
{
	GList *p;

	if (info->metadata_list == NULL)
		return;

	for (p = info->metadata_list; p != NULL; p = p->next)
		free_metadata (p);

	g_list_free (info->metadata_list);
	info->metadata_list = NULL;
}

static GList *
lookup_metadata (GnomeVFSFileInfo *info,
		 const gchar *key)
{
	GList *p;

	for (p = info->metadata_list; p != NULL; p = p->next) {
		GnomeVFSFileMetadata *meta;

		meta = p->data;
		if (strcmp (meta->key, key) == 0)
			return p;
	}

	return NULL;
}


GnomeVFSFileInfo *
gnome_vfs_file_info_new (void)
{
	GnomeVFSFileInfo *new;

	new = g_new0 (GnomeVFSFileInfo, 1);

	/* `g_new0()' is enough to initialize everything (we just want
           all the members to be set to zero).  */

	return new;
}

void
gnome_vfs_file_info_init (GnomeVFSFileInfo *info)
{
	g_return_if_fail (info != NULL);

	/* This is enough to initialize everything (we just want all
           the members to be set to zero).  */
	memset (info, 0, sizeof (*info));
}

void
gnome_vfs_file_info_clear (GnomeVFSFileInfo *info)
{
	g_return_if_fail (info != NULL);

	g_free (info->symlink_name);
	g_free (info->mime_type);

	free_metadata_list (info);

	memset (info, 0, sizeof (*info));
}

void
gnome_vfs_file_info_destroy (GnomeVFSFileInfo *info)
{
	g_return_if_fail (info != NULL);

	gnome_vfs_file_info_clear (info);
	g_free (info);
}


gboolean
gnome_vfs_file_info_set_metadata (GnomeVFSFileInfo *info,
				  const gchar *key,
				  gpointer value,
				  guint value_size)
{
	GList *p;
	GnomeVFSFileMetadata *meta;

	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	p = lookup_metadata (info, key);
	if (p != NULL) {
		meta = p->data;
		g_free (meta->value);
	} else {
		meta = g_new (GnomeVFSFileMetadata, 1);
		meta->key = g_strdup (key);
		info->metadata_list = g_list_prepend (info->metadata_list,
						      meta);
	}

	meta->value_size = value_size;
	meta->value = value;

	return TRUE;
}

gboolean
gnome_vfs_file_info_get_metadata (GnomeVFSFileInfo *info,
				  const gchar *key,
				  gconstpointer *value,
				  guint *value_size)
{
	GList *p;
	GnomeVFSFileMetadata *meta;

	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (strcmp (key, "type") == 0) {
		if (value != NULL)
			*value = info->mime_type;
		if (value_size != NULL)
			*value_size = strlen (info->mime_type) + 1;
		return TRUE;
	}

	p = lookup_metadata (info, key);
	if (p == NULL)
		return FALSE;

	meta = p->data;

	if (value != NULL)
		*value = meta->value;
	if (value_size != NULL)
		*value_size = meta->value_size;

	return TRUE;
}

gboolean
gnome_vfs_file_info_unset_metadata (GnomeVFSFileInfo *info,
				    const gchar *key)
{
	GList *p;

	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	p = lookup_metadata (info, key);
	if (p == NULL)
		return FALSE;

	remove_metadata (info, p);

	return TRUE;
}

const gchar *
gnome_vfs_file_info_get_mime_type (GnomeVFSFileInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->mime_type;
}


void
gnome_vfs_file_info_copy (GnomeVFSFileInfo *dest,
			  const GnomeVFSFileInfo *src)
{
	GList *p;
	GList *meta_list_end;

	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);

	/* Copy basic information all at once; we will fix pointers later.  */

	memcpy (dest, src, sizeof (*src));

	/* Duplicate dynamically allocated strings.  */

	dest->symlink_name = g_strdup (src->symlink_name);
	dest->mime_type = g_strdup (src->mime_type);

	/* Duplicate metadata information.  */

	meta_list_end = NULL;
	dest->metadata_list = NULL;
	for (p = src->metadata_list; p != NULL; p = p->next) {
		GnomeVFSFileMetadata *meta;
		GnomeVFSFileMetadata *new_meta;

		meta = p->data;

		new_meta = g_new (GnomeVFSFileMetadata, 1);
		new_meta->key = g_strdup (meta->key);
		new_meta->value_size = meta->value_size;

		if (meta->value == NULL) {
			new_meta->value = NULL;
		} else {
			new_meta->value = g_malloc (meta->value_size);
			memcpy (new_meta->value, meta->value, meta->value_size);
		}

		if (meta_list_end == NULL) {
			dest->metadata_list = g_list_alloc ();
			dest->metadata_list->data = new_meta;
			meta_list_end = dest->metadata_list;
		} else {
			meta_list_end->next = g_list_alloc ();
			meta_list_end->next->prev = meta_list_end;
			meta_list_end = meta_list_end->next;
			meta_list_end->data = new_meta;
		}
	}
}
