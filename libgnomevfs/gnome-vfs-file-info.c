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

#include <string.h>
#include <glib.h>
#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

/* Special refcount used on stack-allocated file_info's */
#define FILE_INFO_REFCOUNT_STACK ((guint)(-1))

/* Mutex for making GnomeVFSFileInfo ref's/unref's atomic */
/* Note that an atomic increment function (such as is present in NSPR) is preferable */
static GStaticMutex file_info_ref_lock = G_STATIC_MUTEX_INIT;


/**
 * gnome_vfs_file_info_new:
 * 
 * Allocate and initialize a new file information struct.
 * 
 * Return value: A pointer to the new file information struct.
 **/
GnomeVFSFileInfo *
gnome_vfs_file_info_new (void)
{
	GnomeVFSFileInfo *new;

	new = g_new0 (GnomeVFSFileInfo, 1);

	/* `g_new0()' is enough to initialize everything (we just want
           all the members to be set to zero).  */

	new->refcount = 1;
	
	return new;
}

/**
 * gnome_vfs_file_info_init:
 * @info: 
 * 
 * Initialize @info.  This is different from %gnome_vfs_file_info_clear,
 * because it will not de-allocate any memory.  This is supposed to be used
 * when a new %GnomeVFSFileInfo struct is allocated on the stack, and you want
 * to initialize it.
 **/
void
gnome_vfs_file_info_init (GnomeVFSFileInfo *info)
{
	g_return_if_fail (info != NULL);

	/* This is enough to initialize everything (we just want all
           the members to be set to zero).  */
	memset (info, 0, sizeof (*info));

	info->refcount = FILE_INFO_REFCOUNT_STACK;
}

/**
 * gnome_vfs_file_info_clear:
 * @info: Pointer to a file information struct
 * 
 * Clear @info so that it's ready to accept new data.  This is different from
 * %gnome_vfs_file_info_init as it will free associated memory too.  This is
 * supposed to be used when @info already contains meaningful information which
 * we want to get rid of.
 **/
void
gnome_vfs_file_info_clear (GnomeVFSFileInfo *info)
{
	guint old_refcount;
	
	g_return_if_fail (info != NULL);

	g_free (info->name);
	g_free (info->symlink_name);
	g_free (info->mime_type);

	/* Ensure the ref count is maintained correctly */
	g_static_mutex_lock (&file_info_ref_lock);

	old_refcount = info->refcount;
	memset (info, 0, sizeof (*info));
	info->refcount = old_refcount;

	g_static_mutex_unlock (&file_info_ref_lock);

}


/**
 * gnome_vfs_file_info_ref:
 * @info: Pointer to a file information struct
 * 
 * Increment reference count
 **/
void
gnome_vfs_file_info_ref (GnomeVFSFileInfo *info)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->refcount != FILE_INFO_REFCOUNT_STACK);
	g_return_if_fail (info->refcount > 0);

	g_static_mutex_lock (&file_info_ref_lock);
	info->refcount += 1;
	g_static_mutex_unlock (&file_info_ref_lock);
	
}

/**
 * gnome_vfs_file_info_unref:
 * @info: Pointer to a file information struct
 * 
 * Destroy @info
 **/
void
gnome_vfs_file_info_unref (GnomeVFSFileInfo *info)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->refcount != FILE_INFO_REFCOUNT_STACK);
	g_return_if_fail (info->refcount > 0);

	g_static_mutex_lock (&file_info_ref_lock);
	info->refcount -= 1;
	g_static_mutex_unlock (&file_info_ref_lock);

	if (info->refcount == 0) {
		gnome_vfs_file_info_clear (info);
		g_free (info);
	}
}



/**
 * gnome_vfs_file_info_get_mime_type:
 * @info: A pointer to a file information struct
 * 
 * Retrieve MIME type from @info.
 * 
 * Return value: A pointer to a string representing the MIME type.
 **/
const gchar *
gnome_vfs_file_info_get_mime_type (GnomeVFSFileInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->mime_type;
}


/**
 * gnome_vfs_file_info_copy:
 * @dest: Pointer to a struct to copy @src's information into
 * @src: Pointer to the information to be copied into @dest
 * 
 * Copy information from @src into @dest.
 **/
void
gnome_vfs_file_info_copy (GnomeVFSFileInfo *dest,
			  const GnomeVFSFileInfo *src)
{
	guint old_refcount;

	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);

	/* The primary purpose of this lock is to guarentee that the
	 * refcount is correctly maintained, not to make the copy
	 * atomic.  If you want to make the copy atomic, you probably
	 * want serialize access differently (or perhaps you shouldn't
	 * use copy)
	 */
	g_static_mutex_lock (&file_info_ref_lock);

	old_refcount = dest->refcount;

	/* Copy basic information all at once; we will fix pointers later.  */

	memcpy (dest, src, sizeof (*src));

	/* Duplicate dynamically allocated strings.  */

	dest->name = g_strdup (src->name);
	dest->symlink_name = g_strdup (src->symlink_name);
	dest->mime_type = g_strdup (src->mime_type);

	dest->refcount = old_refcount;

	g_static_mutex_unlock (&file_info_ref_lock);

}

/**
 * gnome_vfs_file_info_dup:
 * @orig: Pointer to a file information structure to duplicate
 * 
 * Returns a new file information struct that duplicates the information in @orig.
 **/

GnomeVFSFileInfo *
gnome_vfs_file_info_dup 	(const GnomeVFSFileInfo *orig)
{
	GnomeVFSFileInfo * ret;

	g_return_val_if_fail (orig != NULL, NULL);

	ret = gnome_vfs_file_info_new();

	gnome_vfs_file_info_copy (ret, orig);

	return ret;
}


/**
 * gnome_vfs_file_info_matches:
 *
 * Compare the two file info structs, return TRUE if they match.
 **/
gboolean
gnome_vfs_file_info_matches (const GnomeVFSFileInfo *a,
			     const GnomeVFSFileInfo *b)
{
	if (a->type != b->type
	    || a->size != b->size
	    || a->block_count != b->block_count
	    || a->atime != b->atime
	    || a->mtime != b->mtime
	    || a->ctime != b->ctime
	    || strcmp (a->name, b->name) != 0) {
		return FALSE;
	}

	if (a->mime_type == NULL || b->mime_type == NULL) {
		return a->mime_type == b->mime_type;
	}

	g_assert (a->mime_type != NULL && b->mime_type != NULL);
	return g_strcasecmp (a->mime_type, b->mime_type) == 0;
}

GList *
gnome_vfs_file_info_list_ref (GList *list)
{
	g_list_foreach (list, (GFunc) gnome_vfs_file_info_ref, NULL);
	return list;
}

GList *
gnome_vfs_file_info_list_unref (GList *list)
{
	g_list_foreach (list, (GFunc) gnome_vfs_file_info_unref, NULL);
	return list;
}

GList *
gnome_vfs_file_info_list_copy (GList *list)
{
	return g_list_copy (gnome_vfs_file_info_list_ref (list));
}

void
gnome_vfs_file_info_list_free (GList *list)
{
	g_list_free (gnome_vfs_file_info_list_unref (list));
}
