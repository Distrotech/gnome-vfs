/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* mime.c: implementation of a mime database using the Shared Mime Spec from
           freedesktop.org (some code was copied straight from 
           gnome-vfs-mime-handlers.c hence the (C) Eazel)

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2003, Christophe Fergeau.
   All rights reserved.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors:
   Christophe Fergeau <teuf@users.sourceforge.net>
*/

#ifndef MIME_DB_H
#define MIME_DB_H

#include <glib.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>

enum ModificationState {
	DEFAULT = 0,
	USER_ADDED,
	USER_REMOVED,
	USER_MODIFIED
};

/* Describes a mime type */
struct MimeType {
	gchar *type;           /* Mime type (eg application/zip) */
	gchar *desc; 	       /* Translated description (eg Zip archive) */
	gchar *category;       /* Translated category (eg Compressed archive)*/
	GHashTable *user_attributes;
	GList *helpers;         /* Known helpers for this mime type */
	GnomeVFSMimeActionType default_action;
	enum ModificationState state;
};

/* Describes a mime-type/application association 
 */
struct MimeHelper {
	gchar *app_id;         /* id of this helper in the app registry */
	gint relevance;        /* Whether this helper is appropriate for the
				* associated mime type */
	enum ModificationState state;
};

/* Private API for now - most of it should stay private to gnome-vfs anyway */
struct MimeType *find_mime_type (const char *mime_type);

void add_helper_to_mime_type (struct MimeType *mime_type, 
			      struct MimeHelper *helper);

void remove_helper_from_mime_type (struct MimeType *mime_type, 
				   const gchar *app_id);

void g_list_free_deep (GList *list);

void init_mime_db (void);

/* Old public API with a few tweaks */
#ifdef GNOME_VFS_DISABLE_DEPRECATED
#define gnome_vfs_get_registered_mime_types gnome_vfs_get_mime_types
#define gnome_vfs_get_value gnome_vfs_mime_get_user_attribute
#define gnome_vfs_set_value gnome_vfs_set_user_attribute
#define gnome_vfs_mime_get_key_list gnome_vfs_get_user_attributes
void gnome_vfs_mime_registered_mime_type_list_free (GList *list);
void gnome_vfs_mime_keys_list_free (GList *mime_type_list);
GnomeVFSResult gnome_vfs_mime_set_registered_type_key (const char *mime_type, 
						       const char *key, 
						       const char *value);
#endif /* GNOME_VFS_DISABLE_DEPRECATED */

GList *gnome_vfs_get_mime_types (void);

gboolean gnome_vfs_mime_type_is_known (const char *mime_type);

void gnome_vfs_mime_info_reload (void);

GnomeVFSResult gnome_vfs_mime_set_user_attribute    (const char *mime_type, 
						     const char *key, 
						     const char *value);
const char *   gnome_vfs_mime_get_user_attribute    (const char *mime_type, 
						     const char *key);
void           gnome_vfs_mime_remove_user_attribute (const char *mime_type, 
						     const char *key);
GList *        gnome_vfs_mime_get_user_attributes   (const char *mime_type);

#endif /* MIME_DB_H */
