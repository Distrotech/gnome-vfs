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
#include <gnome-vfs-mime-info.h>

GnomeVFSMimeAction *
gnome_vfs_mime_get_default_action (const char *mime_type)
{
	const char *action_type;
	GnomeVFSMimeAction *action;

	action = g_new0 (GnomeVFSMimeAction, 1);

	action_type = gnome_vfs_mime_get_value (mime_type,
						"default_action_type");
	
	if (strcasecmp (action_type, "application") == 0) {
		action->action_type = GNOME_VFS_MIME_ACTION_TYPE_APPLICATION;
		action->action.application = 
			gnome_vfs_mime_get_default_application (mime_type);
		if (action->action.application == NULL) {
			g_free (action);
			action = NULL;
		}
	} else if (strcasecmp (action_type, "component") == 0) {
		action->action_type = GNOME_VFS_MIME_ACTION_TYPE_COMPONENT;
		action->action.component = 
			gnome_vfs_mime_get_default_component (mime_type);
		if (action->action.component == NULL) {
			g_free (action);
			action = NULL;
		}
	} else {
		g_free (action);
		action = NULL;
	}

	return action;
}

GnomeVFSMimeApplication *
gnome_vfs_mime_get_default_application (const char *mime_type)
{
	GnomeVFSMimeApplication *application;
	const char *default_application_command;
	const char *default_application_name;
	const char *default_application_can_open_multiple_files;
	const char *default_application_can_open_uris;


	application = g_new0 (GnomeVFSMimeApplication, 1);
	
	default_application_command = gnome_vfs_mime_get_value (mime_type, 
								"default_application_command");

	if (default_application_command == NULL) {
		g_free (application);
		return NULL;
	} else {
		application->command = g_strdup (default_application_command);
	}

	default_application_name = gnome_vfs_mime_get_value (mime_type, 
							     "default_application_name");
	if (default_application_name == NULL) {
		application->name = gnome_vfs_mime_program_name (application->command);
	} else {
		application->name = g_strdup (default_application_name);
	}

	default_application_can_open_multiple_files = gnome_vfs_mime_get_value (mime_type, 
										"default_application_name");
	
	application->can_open_multiple_files = 
		(strcasecmp (default_application_can_open_multiple_files, "true") == 0) || 
		(strcasecmp (default_application_can_open_multiple_files, "yes") == 0);

	default_application_can_open_uris = gnome_vfs_mime_get_value (mime_type, 
								      "default_application_name");
	
	application->can_open_uris = 
		(strcasecmp (default_application_can_open_uris, "true") == 0) || 
		(strcasecmp (default_application_can_open_uris, "yes") == 0);

	return application;
}


/* It might be worth moving this to nautilus-string.h at some point. */
static char *
extract_prefix_add_suffix (const char *string, const char *separator, const char *suffix)
{
        const char *separator_position;
        int prefix_length;
        char *result;

        separator_position = strstr (string, separator);
        prefix_length = separator_position == NULL
                ? strlen (string)
                : separator_position - string;

        result = g_malloc (prefix_length + strlen(suffix) + 1);
        
        strncpy (result, string, prefix_length);
        result[prefix_length] = '\0';

        strcat (result, suffix);

        return result;
}

static char *
mime_type_get_supertype (const char *mime_type)
{
        return extract_prefix_add_suffix (mime_type, "/", "/*");
}


static OAF_ServerInfo *
OAF_ServerInfo__copy (OAF_ServerInfo *orig)
{
	OAF_ServerInfo *retval;

	retval = OAF_ServerInfo__alloc ();
	
	retval->iid = CORBA_string_dup (orig->iid);
	retval->server_type = CORBA_string_dup (orig->server_type);
	retval->location_info = CORBA_string_dup (orig->location_info);
	retval->username= CORBA_string_dup (orig->username);
	retval->hostname= CORBA_string_dup (orig->hostname);
	retval->domain= CORBA_string_dup (orig->domain);
	retval->attrs = orig->attrs;
	/* FIXME: this looks like a blatant kludge (but I cut & pasted
           it from OAF) */
	CORBA_sequence_set_release (&retval->attrs, CORBA_FALSE);

	return retval;
}

OAF_ServerInfo *
gnome_vfs_mime_get_default_component (const char *mime_type)
{
	const char *default_component_iid;
	OAF_ServerInfoList *info_list;
	OAF_ServerInfo *server;
	CORBA_Environment ev;
	char *supertype;
	char *query;
	char *sort[5];

	CORBA_exception_init (&ev);

	default_component_iid = gnome_vfs_mime_get_value (mime_type, 
							  "default_component_iid");
	

	supertype = mime_type_get_supertype (mime_type);

	/* Find a component that supports either the exact mime type,
           the supertype, or all mime types. */

	/* FIXME: should probably check for the right interfaces
           too. Also slightly semantically different from nautilus in
           other tiny ways. */

	query = g_strconcat ("bonobo:supported_mime_types.has_one ([\'", mime_type, 
			     "\', \'", supertype,
			     "\', \'*\'])", NULL);

	/* First try the component specified in the mime database, if available. */
	if (default_component_iid != NULL) {
		sort[0] = g_strconcat ("iid == \'", default_component_iid, "\'", NULL);
	} else {
		sort[0] = g_strdup ("true");
	}

	/* FIXME: We should then fall back to preferring one of the
	   components on the current short list. */
	
	/* Prefer something that matches the exact type to something
           that matches the supertype */
	sort[1] = g_strconcat ("bonobo:supported_mime_types.has (\'",mime_type,"\')", NULL);
	/* Prefer something that matches the supertype to something that matches `*' */
	sort[2] = g_strconcat ("bonobo:supported_mime_types.has (\'",supertype,"\')", NULL);

	/* At lowest priority, alphebetize by name, for the sake of consistency */
	sort[3] = g_strconcat ("name", NULL);

	sort[4] = NULL;

	info_list = oaf_query (query, sort, &ev);
	
	if (ev._major == CORBA_NO_EXCEPTION  && info_list != NULL && info_list->_length > 0) {
		server = OAF_ServerInfo__copy (&info_list->_buffer[0]);
		/* FIXME: free info_list */
	} else {
		server = NULL;
	}

	g_free (supertype);
	g_free (query);
	g_free (sort[0]);
	g_free (sort[1]);
	g_free (sort[2]);
	g_free (sort[3]);

	CORBA_exception_free (&ev);

	return server;
}

GList *
gnome_vfs_mime_get_short_list_applications (const char *mime_type)
{
	/* determine user level */
	/* get short list app info for that user level */

	/* get user short list delta (add list and remove list which store app names only (BOGUS!)) */

	/* compute list modified by delta */

	/* return it */

	return NULL;
}

GList *
gnome_vfs_mime_get_short_list_components (const char *mime_type)
{
	/* determine user level */
	/* get short list IIDs for that user level */
	/* get user short list delta (add list and remove list) */

	/* compute list modified by delta */

	/* Do usual query but requiring that IIDs be one of the ones
           in the short list IID list. */

	return NULL;
}


static GList *
parse_app_lists (const char *name_list, 
		 const char *command_list,
		 const char *can_open_multiple_files_list,
		 const char *can_open_uris_list) 
{
	char **name_strv;
	char **command_strv;
	char **comf_strv;
	char **cou_strv;
	GList *retval;
	int i;
	
	if (name_list == NULL || command_list == NULL || 
	    can_open_multiple_files_list == NULL || 
	    can_open_uris_list == NULL) {
		return NULL;
	}

	name_strv = g_strsplit (name_list, ",", 0);
	command_strv = g_strsplit (command_list, ",", 0);
	comf_strv = g_strsplit (can_open_multiple_files_list, ",", 0);
	cou_strv = g_strsplit (can_open_uris_list, ",", 0);
	
	retval = NULL;
	for (i = 0; name_strv[i] != NULL && command_strv[i] != NULL && 
		     comf_strv[i] != NULL && cou_strv[i] != NULL; i++) {
		GnomeVFSMimeApplication *application;

		application = g_new0 (GnomeVFSMimeApplication, 1);
		application->name = g_strdup (name_strv[i]);
		application->command = g_strdup (command_strv[i]);
		application->can_open_multiple_files = strcasecmp (comf_strv[i], "true") || 
			strcasecmp (comf_strv[i], "yes");
		application->can_open_uris = strcasecmp (cou_strv[i], "true") || 
			strcasecmp (cou_strv[i], "yes");

		retval = g_list_prepend (retval, application);
	}

	retval = g_list_reverse (retval);

	return retval;
}
	


GList *
gnome_vfs_mime_get_all_applications (const char *mime_type)
{
 	const char *system_application_name_list;
 	const char *system_application_command_list;
 	const char *system_application_can_open_multiple_files_list;
 	const char *system_application_can_open_uris_list;
 	const char *user_application_name_list;
 	const char *user_application_command_list;
 	const char *user_application_can_open_multiple_files_list;
 	const char *user_application_can_open_uris_list;
	GList *system_apps;
	GList *user_apps;
	GList *retval;

	/* FIXME: no way for apps to modify at install time */

	/* get app list */
 	system_application_name_list = gnome_vfs_mime_get_value 
		(mime_type, "system_all_applications_names");
	
 	system_application_command_list = gnome_vfs_mime_get_value 
		(mime_type, "system_all_applications_commands");

 	system_application_can_open_multiple_files_list = gnome_vfs_mime_get_value 
		(mime_type, "system_all_applications_can_open_multiple_files");
	
 	system_application_can_open_uris_list = gnome_vfs_mime_get_value 
		(mime_type, "system_all_applications_can_open_uris");
	

	system_apps = parse_app_lists (system_application_name_list, system_application_command_list,
				       system_application_can_open_multiple_files_list,
				       system_application_can_open_uris_list);
	
	/* get user app list extension */

 	user_application_name_list = gnome_vfs_mime_get_value 
		(mime_type, "all_applications_names");
	
 	user_application_command_list = gnome_vfs_mime_get_value 
		(mime_type, "all_applications_commands");

 	user_application_can_open_multiple_files_list = gnome_vfs_mime_get_value 
		(mime_type, "all_applications_can_open_multiple_files");
	
 	user_application_can_open_uris_list = gnome_vfs_mime_get_value 
		(mime_type, "all_applications_can_open_uris");
	
	user_apps = parse_app_lists (user_application_name_list, user_application_command_list,
				     user_application_can_open_multiple_files_list,
				     user_application_can_open_uris_list);
	

	/* merge the two */

	retval = g_list_concat (system_apps, user_apps);

	return retval;
}

static GList *
OAF_ServerInfoList_to_ServerInfo_g_list (OAF_ServerInfoList *info_list)
{
	GList *retval;
	int i;
	
	retval = NULL;
	if (info_list != NULL && info_list->_length > 0) {
		for (i = 0; i < info_list->_length; i++) {
			retval = g_list_prepend (retval, OAF_ServerInfo__copy (&info_list->_buffer[i]));
		}
		retval = g_list_reverse (retval);
	}

	return retval;
}

GList *
gnome_vfs_mime_get_all_components (const char *mime_type)
{
	OAF_ServerInfoList *info_list;
	GList *retval;
	CORBA_Environment ev;
	char *supertype;
	char *query;
	char *sort[2];

	CORBA_exception_init (&ev);

	supertype = mime_type_get_supertype (mime_type);

	/* Find a component that supports either the exact mime type,
           the supertype, or all mime types. */

	/* FIXME: should probably check for the right interfaces
           too. Also slightly semantically different from nautilus in
           other tiny ways. */

	query = g_strconcat ("bonobo:supported_mime_types.has_one ([\'", mime_type, 
			     "\', \'", supertype,
			     "\', \'*\'])", NULL);
	
	/* Alphebetize by name, for the sake of consistency */
	sort[0] = g_strdup ("name");
	sort[1] = NULL;

	info_list = oaf_query (query, sort, &ev);
	
	if (ev._major == CORBA_NO_EXCEPTION) {
		retval = (OAF_ServerInfoList_to_ServerInfo_g_list (info_list));
		/* FIXME: free info_list */
	} else {
		retval = NULL;
	}

	g_free (supertype);
	g_free (query);
	g_free (sort[0]);

	CORBA_exception_free (&ev);

	return retval;
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
