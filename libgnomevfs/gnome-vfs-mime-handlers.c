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

#include <config.h>
#include "gnome-vfs-mime-handlers.h"

#include "gnome-vfs-mime-info.h"
#include <gconf/gconf-client.h>
#include <gtk/gtksignal.h>

static char *get_user_level (void);
static gboolean str_to_bool (const char *str);
static char *join_str_list (const char *separator, GList *list);
static gboolean strv_contains_str (char **strv, const char *str);
static char *extract_prefix_add_suffix (const char *string, const char *separator, const char *suffix);
static char *mime_type_get_supertype (const char *mime_type);
static char **strsplit_handle_null (const char *str, const char *delim, int max);
static OAF_ServerInfo *OAF_ServerInfo__copy (OAF_ServerInfo *orig);
static GList *OAF_ServerInfoList_to_ServerInfo_g_list (OAF_ServerInfoList *info_list);
static GList *process_app_list (const char *id_list);


GnomeVFSMimeActionType
gnome_vfs_mime_get_default_action_type (const char *mime_type)
{
	const char *action_type_string;

	action_type_string = gnome_vfs_mime_get_value (mime_type,
						       "default_action_type");

	if (action_type_string == NULL) {
		return GNOME_VFS_MIME_ACTION_TYPE_NONE;
	} else if (strcasecmp (action_type_string, "application") == 0) {
		return GNOME_VFS_MIME_ACTION_TYPE_APPLICATION;
	} else if (strcasecmp (action_type_string, "component") == 0) {
		return GNOME_VFS_MIME_ACTION_TYPE_COMPONENT;
	} else {
		return GNOME_VFS_MIME_ACTION_TYPE_NONE;
	}
}

GnomeVFSMimeAction *
gnome_vfs_mime_get_default_action (const char *mime_type)
{
	GnomeVFSMimeAction *action;

	action = g_new0 (GnomeVFSMimeAction, 1);

	action->action_type = gnome_vfs_mime_get_default_action_type (mime_type);

	switch (action->action_type) {
	case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
		action->action.application = 
			gnome_vfs_mime_get_default_application (mime_type);
		if (action->action.application == NULL) {
			g_free (action);
			action = NULL;
		}
		break;
	case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
		action->action.component = 
			gnome_vfs_mime_get_default_component (mime_type);
		if (action->action.component == NULL) {
			g_free (action);
			action = NULL;
		}
		break;
	case GNOME_VFS_MIME_ACTION_TYPE_NONE:
		g_free (action);
		action = NULL;
		break;
	default:
		g_assert_not_reached ();
	}

	return action;
}

GnomeVFSMimeApplication *
gnome_vfs_mime_get_default_application (const char *mime_type)
{
	const char *default_application_id;

	default_application_id = gnome_vfs_mime_get_value (mime_type, 
							   "default_application_id");

	return gnome_vfs_mime_application_new_from_id (default_application_id);
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
	const char *short_list_id_list;
	const char *short_list_id_user_additions;
	const char *short_list_id_user_removals;
	char **short_list_strv;
	char **short_list_addition_strv;
	char **short_list_removal_strv;
	GList *id_list;
	GList *retval;
	GList *p;
	int i;
	char *user_level, *id_list_key;
	
	if (mime_type == NULL) {
		return NULL;
	}

	/* Base list depends on user level. */
	user_level = get_user_level ();
	id_list_key = g_strconcat ("short_list_application_ids_for_",
				   user_level,
				   "_user_level",
				   NULL);
	g_free (user_level);
	short_list_id_list = gnome_vfs_mime_get_value (mime_type, id_list_key);
	g_free (id_list_key);

	/* get user short list delta (add list and remove list) */
	short_list_id_user_additions = gnome_vfs_mime_get_value
		(mime_type,
		 "short_list_application_user_additions");
	short_list_id_user_removals = gnome_vfs_mime_get_value
		(mime_type,
		 "short_list_application_user_removals");
	
	/* compute list modified by delta */
	short_list_strv = strsplit_handle_null (short_list_id_list, ",", 0);
	short_list_addition_strv = strsplit_handle_null (short_list_id_user_additions, ",", 0);
	short_list_removal_strv = strsplit_handle_null (short_list_id_user_removals, ",", 0);

	id_list = NULL;
	for (i = 0; short_list_strv[i] != NULL; i++) {
		if (! strv_contains_str (short_list_removal_strv, short_list_strv[i])) {
			id_list = g_list_prepend (id_list, g_strdup (short_list_strv[i]));
		}
	}

	for (i = 0; short_list_addition_strv[i] != NULL; i++) {
		if (! strv_contains_str (short_list_removal_strv, short_list_strv[i]) &&
		    ! strv_contains_str (short_list_strv, short_list_strv[i])) {
			id_list = g_list_prepend (id_list, g_strdup (short_list_strv[i]));
		}
	}

	retval = NULL;
	for (p = id_list; p != NULL; p = p->next) {
		GnomeVFSMimeApplication *application;

		application = gnome_vfs_mime_application_new_from_id (p->data);
		
		if (application != NULL) {
			retval = g_list_prepend (retval, application);
		}
	}

	retval = g_list_reverse (retval);

	return retval;
}


GList *
gnome_vfs_mime_get_short_list_components (const char *mime_type)
{
	const char *short_list_iid_list;
	const char *short_list_iid_user_additions;
	const char *short_list_iid_user_removals;
	char **short_list_strv;
	char **short_list_addition_strv;
	char **short_list_removal_strv;
	int i;
	GList *iid_list;
	OAF_ServerInfoList *info_list;
	GList *retval;
	CORBA_Environment ev;
	char *supertype;
	char *query;
	char *sort[2];
	char *iids_delimited;
	char *user_level, *id_list_key;
	
	if (mime_type == NULL) {
		return NULL;
	}

	/* get short list IIDs for that user level */
	user_level = get_user_level ();
	id_list_key = g_strconcat ("short_list_component_iids_for_",
				   user_level,
				   "_user_level",
				   NULL);
	g_free (user_level);
	short_list_iid_list = gnome_vfs_mime_get_value (mime_type, id_list_key);
	g_free (id_list_key);

	/* get user short list delta (add list and remove list) */
	short_list_iid_user_additions = gnome_vfs_mime_get_value
		(mime_type, "short_list_component_user_additions");
	short_list_iid_user_removals = gnome_vfs_mime_get_value
		(mime_type, "short_list_component_user_removals");
	
	/* compute list modified by delta */
	
	short_list_strv = strsplit_handle_null (short_list_iid_list, ",", 0);
	short_list_addition_strv = strsplit_handle_null (short_list_iid_user_additions, ",", 0);
	short_list_removal_strv = strsplit_handle_null (short_list_iid_user_removals, ",", 0);

	iid_list = NULL;
	for (i = 0; short_list_strv[i] != NULL; i++) {
		if (! strv_contains_str (short_list_removal_strv, short_list_strv[i])) {
			iid_list = g_list_prepend (iid_list, g_strdup (short_list_strv[i]));
		}
	}

	for (i = 0; short_list_addition_strv[i] != NULL; i++) {
		if (! strv_contains_str (short_list_removal_strv, short_list_strv[i]) &&
		    ! strv_contains_str (short_list_strv, short_list_strv[i])) {
			iid_list = g_list_prepend (iid_list, g_strdup (short_list_strv[i]));
		}
	}

	/* Do usual query but requiring that IIDs be one of the ones
           in the short list IID list. */
	
	if (iid_list != NULL) {

		CORBA_exception_init (&ev);

		iids_delimited = join_str_list ("\',\'", iid_list);
		
		supertype = mime_type_get_supertype (mime_type);
		
		query = g_strconcat ("bonobo:supported_mime_types.has_one ([\'", mime_type, 
				     "\', \'", supertype,
				     "\', \'*\']) && has ([\'", iids_delimited, "\'], iid)", NULL);
		
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
	} else {
		retval = NULL;
	}

	return retval;
}



GList *
gnome_vfs_mime_get_all_applications (const char *mime_type)
{
 	const char *system_all_application_ids;
 	const char *user_all_application_ids;
	GList *system_apps;
	GList *user_apps;
	GList *retval;

	if (mime_type == NULL) {
		return NULL;
	}

	/* FIXME: no way for apps to modify at install time */

	/* get app list */
 	system_all_application_ids = gnome_vfs_mime_get_value 
		(mime_type, "system_all_application_ids");
	
	system_apps = process_app_list (system_all_application_ids);
	
	/* get user app list extension */

 	user_all_application_ids = gnome_vfs_mime_get_value 
		(mime_type, "all_application_ids");
		
	user_apps = process_app_list (user_all_application_ids);
	
	/* merge the two */

	retval = g_list_concat (system_apps, user_apps);

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

	if (mime_type == NULL) {
		return NULL;
	}

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



GnomeVFSMimeApplication *
gnome_vfs_mime_application_copy (GnomeVFSMimeApplication *application)
{
	GnomeVFSMimeApplication *result;
	
	result = g_new0 (GnomeVFSMimeApplication, 1);
	result->id = g_strdup (application->id);
	result->name = g_strdup (application->name);
	result->command = g_strdup (application->command);
	result->can_open_multiple_files = application->can_open_multiple_files;
	result->can_open_uris = application->can_open_uris;

	return result;
}

void
gnome_vfs_mime_application_free (GnomeVFSMimeApplication *application) 
{
	g_free (application->name);
	g_free (application->command);
	g_free (application);
}

void
gnome_vfs_mime_action_free (GnomeVFSMimeAction *action) 
{
	return;
}

void
gnome_vfs_mime_application_list_free (GList *list)
{
	g_list_foreach (list, (GFunc)gnome_vfs_mime_application_free, NULL);
}

void
gnome_vfs_mime_component_list_free (GList *list)
{
	g_list_foreach (list, (GFunc)CORBA_free, NULL);
}



static gboolean
str_to_bool (const char *str)
{
	return ((str != NULL) &&
		((strcasecmp (str, "true") == 0) || 
		 (strcasecmp (str, "yes") == 0)));
}

static char *
join_str_list (const char *separator, GList *list)
{
	char **strv;
	GList *p;
	int i;
	char *retval;

	strv = g_new0 (char *, g_list_length (list) + 1);

	for (p = list, i = 0; p != NULL; p = p->next, i++) {
		strv[i] = (char *) p->data;
	}

	strv[i] = NULL;

	retval = g_strjoinv (separator, strv);

	g_free (strv);

	return retval;
}

static gboolean
strv_contains_str (char **strv, const char *str)
{
	int i;

	for (i = 0; strv[i] != NULL; i++) {
		if (strcmp (strv[i], str) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

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


static char **
strsplit_handle_null (const char *str, const char *delim, int max)
{
	return g_strsplit ((str == NULL ? "" : str), delim, max);
}

GnomeVFSMimeApplication *
gnome_vfs_mime_application_new_from_id (const char *id) {
	GnomeVFSMimeApplication *application;
	const char *command;
	const char *name;
	char *id_hack_mime_type;

	if (id == NULL) {
		return NULL;
	}


	application = g_new0 (GnomeVFSMimeApplication, 1);

	id_hack_mime_type = g_strconcat ("x-application-registry-hack/", id, NULL);

	command = gnome_vfs_mime_get_value (id_hack_mime_type, 
					    "command");

	if (command == NULL) {
		g_free (id_hack_mime_type);
		g_free (application);
		return NULL;
	} else {
		application->command = g_strdup (command);
	}

	name = gnome_vfs_mime_get_value (id_hack_mime_type, 
					 "name");

	if (name == NULL) {
		application->name = gnome_vfs_mime_program_name (application->command);
	} else {
		application->name = g_strdup (name);
	}

	application->can_open_multiple_files = str_to_bool (gnome_vfs_mime_get_value (id_hack_mime_type, 
										      "can_open_multiple_files"));
	
	application->can_open_uris = str_to_bool (gnome_vfs_mime_get_value (id_hack_mime_type, 
									    "can_open_uris"));

	application->id = g_strdup (id);

	return application;
}


static OAF_ServerInfo *
OAF_ServerInfo__copy (OAF_ServerInfo *orig)
{
	OAF_ServerInfo *retval;
	int i;

	retval = OAF_ServerInfo__alloc ();
	
	retval->iid = CORBA_string_dup (orig->iid);
	retval->server_type = CORBA_string_dup (orig->server_type);
	retval->location_info = CORBA_string_dup (orig->location_info);
	retval->username= CORBA_string_dup (orig->username);
	retval->hostname= CORBA_string_dup (orig->hostname);
	retval->domain= CORBA_string_dup (orig->domain);
	retval->attrs = orig->attrs;

	retval->attrs._maximum = orig->attrs._maximum;
	retval->attrs._length = orig->attrs._length;
	
	retval->attrs._buffer = CORBA_sequence_OAF_Attribute_allocbuf (retval->attrs._length);
	memcpy (retval->attrs._buffer, orig->attrs._buffer, (sizeof (OAF_Attribute)) * retval->attrs._length);
	
	for (i = 0; i < retval->attrs._length; i++) {
		retval->attrs._buffer[i].name = CORBA_string_dup (retval->attrs._buffer[i].name);
		if (retval->attrs._buffer[i].v._d == OAF_A_STRING) {
			retval->attrs._buffer[i].v._u.value_string = CORBA_string_dup (retval->attrs._buffer[i].v._u.value_string);
		
		}
	}

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



static GList *
process_app_list (const char *id_list) 
{
	char **id_strv;
	GList *retval;
	int i;
	
	if (id_list == NULL) {
		return NULL;
	}

	id_strv = strsplit_handle_null (id_list, ",", 0);
	
	retval = NULL;
	for (i = 0; id_strv[i] != NULL; i++) {
		GnomeVFSMimeApplication *application;

		application = gnome_vfs_mime_application_new_from_id (id_strv[i]);
		
		if (application != NULL) {
			retval = g_list_prepend (retval, application);
		}
	}

	retval = g_list_reverse (retval);

	return retval;
}
	

/* Returns the Nautilus user level, a string.
 * This does beg the question: Why does gnome-vfs have the
 * Nautilus user level coded into it. Eventually we might
 * want to call this the GNOME user level or something.
 */
static char *
get_user_level (void)
{
	static GConfClient *client;
	char *user_level;

	client = NULL;

	/* This sequence is needed in case no one has initialize GConf.
	 * GConf won't take care of initializing Gtk.
	 */
	if (!gconf_is_initialized ()) {
		char *fake_argv[] = { "gnome-vfs", NULL };
		gconf_init (1, fake_argv, NULL);
	}
	gtk_type_init ();
	gtk_signal_init ();

	/* Create the client. */
	if (client == NULL) {
		client = gconf_client_new ();
		/* FIXME: This client never gets freed. */
	}

	user_level = gconf_client_get_string (client, "/nautilus/user_level", NULL);

	/* FIXME: Nautilus just asserts this.
	 * But it doesn't seem reasonable to assert something that's the result
	 * of reading from a file.
	 */
	if (user_level == NULL) {
		user_level = g_strdup ("novice");
	}

	/* FIXME: Is it OK to just return a string without checking if
	 * it's one of the 3 expected values?
	 */
	return user_level;
}
