/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-mime-handlers.h - Mime type handlers for the GNOME Virtual
   File System.

   Copyright (C) 2000 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Maciej Stachowiak <mjs@eazel.com> */

#ifndef GNOME_VFS_MIME_HANDLERS_H
#define GNOME_VFS_MIME_HANDLERS_H

#include <glib.h>
#include <liboaf/liboaf.h>

/* 
 * FIXME: need error handling
 *
 */

enum GnomeVFSMimeActionType {
	GNOME_VFS_MIME_ACTION_TYPE_APPLICATION,
	GNOME_VFS_MIME_ACTION_TYPE_COMPONENT
};

typedef enum GnomeVFSMimeActionType GnomeVFSMimeActionType;


struct GnomeVFSMimeApplication {
	char *name;
	char *command;
	gboolean can_open_multiple_files;
	gboolean can_open_uris;
};

typedef struct GnomeVFSMimeApplication GnomeVFSMimeApplication;


struct GnomeVFSMimeAction {
	GnomeVFSMimeActionType action_type;
	
	union {
		OAF_ServerInfo      *component;
		GnomeVFSMimeApplication *application;
	} action;
};

typedef struct GnomeVFSMimeAction GnomeVFSMimeAction;

GnomeVFSMimeApplication *gnome_vfs_mime_application_copy		  (GnomeVFSMimeApplication *application);
void		    gnome_vfs_mime_application_list_free		  (GList 		  *list);
void		    gnome_vfs_mime_component_list_free		  	  (GList 		  *list);

GnomeVFSMimeAction  *gnome_vfs_mime_get_default_action                    (const char             *mime_type);
GnomeVFSMimeApplication *gnome_vfs_mime_get_default_application           (const char             *mime_type);
OAF_ServerInfo      *gnome_vfs_mime_get_default_component                 (const char             *mime_type);
GList               *gnome_vfs_mime_get_short_list_applications           (const char             *mime_type);
GList               *gnome_vfs_mime_get_short_list_components             (const char             *mime_type);
GList               *gnome_vfs_mime_get_all_applications                  (const char             *mime_type);
GList               *gnome_vfs_mime_get_all_components                    (const char             *mime_type);


void                gnome_vfs_mime_set_default_action_type                (const char             *mime_type,
								           GnomeVFSMimeActionType  action_type);
void                gnome_vfs_mime_set_default_application                (const char             *mime_type,
								           GnomeVFSMimeApplication    *application);
void                gnome_vfs_mime_set_default_component                  (const char             *mime_type,
								           OAF_ServerInfo         *component_iid);

/* Stored as delta to current user level - API function computes delta and stores in prefs */
void                gnome_vfs_mime_set_short_list_applications            (const char             *mime_type,
								           GList                  *applications);
void                gnome_vfs_mime_set_short_list_components              (const char             *mime_type,
								           GList                  *components);
/* No way to override system list; can only add. */
void                gnome_vfs_mime_extend_all_applications                (const char             *mime_type,
								           GList                  *applications);
/* Only "user" entries may be removed. */
void                gnome_vfs_mime_remove_from_all_applications           (const char             *mime_type,
								           GList                  *applications);


void                gnome_vfs_mime_application_free                       (GnomeVFSMimeApplication *application);
void                gnome_vfs_mime_action_free                            (GnomeVFSMimeAction      *action);


#endif
