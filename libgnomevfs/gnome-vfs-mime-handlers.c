/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-mime-handlers.c - Mime type handlers for the GNOME Virtual
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

#include <gnome-vfs-mime-handlers.h>


GnomeVFSMimeAction *
gnome_vfs_mime_get_default_action (const char *mime_type)
{
	return NULL;
}

GnomeVFSMimeApplication *
gnome_vfs_mime_get_default_application (const char *mime_type)
{
	return NULL;
}

OAF_ServerInfo *
gnome_vfs_mime_get_default_component (const char *mime_type)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_short_list_applications (const char *mime_type)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_short_list_components (const char *mime_type)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_all_applications (const char *mime_type)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_all_components (const char *mime_type)
{
	return NULL;
}


GnomeVFSMimeAction *
gnome_vfs_mime_get_default_action_for_uri (const char *uri)
{
	return NULL;
}

GnomeVFSMimeApplication *
gnome_vfs_mime_get_default_application_for_uri (const char *uri)
{
	return NULL;
}

OAF_ServerInfo *
gnome_vfs_mime_get_default_component_for_uri (const char *uri)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_short_list_applications_for_uri (const char *uri)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_short_list_components_for_uri (const char *uri)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_all_applications_for_uri (const char *uri)
{
	return NULL;
}

GList *
gnome_vfs_mime_get_all_components_for_uri (const char *uri)
{
	return NULL;
}

void
gnome_vfs_mime_set_default_action_type (const char              *mime_type,
					GnomeVFSMimeActionType   action_type)
{
	return;
}

void gnome_vfs_mime_set_default_application (const char              *mime_type,
				             GnomeVFSMimeApplication *application)
{
	return;
}

void
gnome_vfs_mime_set_default_component (const char     *mime_type,
				      OAF_ServerInfo *component_iid)
{
	return;
}

void
gnome_vfs_mime_set_short_list_applications (const char *mime_type,
					    GList      *applications)
{
	return;
}


void
gnome_vfs_mime_set_short_list_components (const char *mime_type,
					  GList      *components)
{
	return;
}


void gnome_vfs_mime_extend_all_applications (const char *mime_type,
					     GList      *applications)
{
	return;
}


void
gnome_vfs_mime_remove_from_all_applications (const char *mime_type,
					     GList      *applications)
{
	return;
}


void
gnome_vfs_mime_set_default_action_type_for_uri (const char             *mime_type,
						GnomeVFSMimeActionType  action_type)
{
	return;
}


void
gnome_vfs_mime_set_default_application_for_uri (const char              *mime_type,
						GnomeVFSMimeApplication *application)
{
	return;
}


void
gnome_vfs_mime_set_default_component_for_uri (const char     *mime_type,
					      OAF_ServerInfo *component_iid)
{
	return;
}


void
gnome_vfs_mime_set_short_list_applications_for_uri (const char *mime_type,
						    GList      *applications)
{
	return;
}

void
gnome_vfs_mime_set_short_list_components_for_uri (const char *mime_type,
						  GList      *components)
{
	return;
}

void
gnome_vfs_mime_extend_all_applications_for_uri (const char *mime_type,
						GList      *applications)
{
	return;
}


void
gnome_vfs_mime_remove_from_all_applications_for_uri (const char *mime_type,
						     GList      *applications)
{
	return;
}


void
gnome_vfs_mime_application_free (GnomeVFSMimeApplication *application) 
{
	return;
}

void
gnome_vfs_mime_action_free (GnomeVFSMimeAction *action) 
{
	return;
}
