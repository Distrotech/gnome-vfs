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
#include <gconf/gconf.h>
#include <gtk/gtksignal.h>
#include <stdio.h>
#include <libgnome/gnome-util.h>

static char *get_user_level (void);
static gboolean str_to_bool (const char *str);
static const char *bool_to_str (gboolean bool);
static char *join_str_list (const char *separator, GList *list);
static char *extract_prefix_add_suffix (const char *string, const char *separator, const char *suffix);
static char *mime_type_get_supertype (const char *mime_type);
static char **strsplit_handle_null (const char *str, const char *delim, int max);
static OAF_ServerInfo *OAF_ServerInfo__copy (OAF_ServerInfo *orig);
static GList *OAF_ServerInfoList_to_ServerInfo_g_list (OAF_ServerInfoList *info_list);
static GList *process_app_list (const char *id_list);
static GList *comma_separated_str_to_str_list (const char *str);
static GList *str_list_difference (GList *a, GList *b);
static char *str_list_to_comma_separated_str (GList *list);
static GList *gnome_vfs_strsplit_to_list (const char *str, const char *delim, int max);
static char *gnome_vfs_strjoin_from_list (const char *separator, GList *list);
static void g_list_free_deep (GList *list);


GnomeVFSMimeActionType
gnome_vfs_mime_get_default_action_type (const char *mime_type)
{
	const char *action_type_string;

	action_type_string = gnome_vfs_mime_get_value
		(mime_type, "default_action_type");

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

	default_application_id = gnome_vfs_mime_get_value
		(mime_type, "default_application_id");
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

	if (mime_type == NULL) {
		return NULL;
	}

	CORBA_exception_init (&ev);

	supertype = mime_type_get_supertype (mime_type);

	/* Find a component that supports either the exact mime type,
           the supertype, or all mime types. */

	/* FIXME bugzilla.eazel.com 1142: should probably check for
           the right interfaces too. Also slightly semantically
           different from nautilus in other tiny ways.
	*/
	query = g_strconcat ("bonobo:supported_mime_types.has_one (['", mime_type, 
			     "', '", supertype,
			     "', '*'])", NULL);

	/* First try the component specified in the mime database, if available. */
	default_component_iid = gnome_vfs_mime_get_value
		(mime_type, "default_component_iid");
	if (default_component_iid != NULL) {
		sort[0] = g_strconcat ("iid == '", default_component_iid, "'", NULL);
	} else {
		sort[0] = g_strdup ("true");
	}

	/* FIXME bugzilla.eazel.com 1145: 
	   We should then fall back to preferring one of the
	   components on the current short list. */
	
	/* Prefer something that matches the exact type to something
           that matches the supertype */
	sort[1] = g_strconcat ("bonobo:supported_mime_types.has ('", mime_type, "')", NULL);

	/* Prefer something that matches the supertype to something that matches `*' */
	sort[2] = g_strconcat ("bonobo:supported_mime_types.has ('", supertype, "')", NULL);

	/* At lowest priority, alphebetize by name, for the sake of consistency */
	sort[3] = g_strdup ("name");

	sort[4] = NULL;

	info_list = oaf_query (query, sort, &ev);
	
	server = NULL;
	if (ev._major == CORBA_NO_EXCEPTION) {
		if (info_list != NULL && info_list->_length > 0) {
			server = OAF_ServerInfo__copy (&info_list->_buffer[0]);
		}
		CORBA_free (info_list);
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

static GList *
gnome_vfs_mime_str_list_apply_delta (GList *list_to_process, 
				     GList *additions, 
				     GList *removals)
{
	GList *pruned_addition_list;
	GList *list_to_process_copy;
	GList *extended_original_list;
	GList *processed_list;

	pruned_addition_list = str_list_difference (additions, list_to_process);
	list_to_process_copy = g_list_copy (list_to_process);
	extended_original_list = g_list_concat (list_to_process_copy, pruned_addition_list);

	processed_list = str_list_difference (extended_original_list, removals);
	
	/* No need to free list_to_process_copy or
         * pruned_addition_list since they were concat()ed into
         * extended_original_list 
	 */
	g_list_free (extended_original_list);

	return processed_list;
}

static const char *
gnome_vfs_mime_get_value_for_user_level (const char *mime_type, 
					 const char *key_prefix)
{
	char *user_level;
	char *full_key;
	const char *value;

	/* Base list depends on user level. */
	user_level = get_user_level ();
	full_key = g_strconcat (key_prefix,
				"_for_",
				user_level,
				"_user_level",
				NULL);
	g_free (user_level);
	value = gnome_vfs_mime_get_value (mime_type, full_key);
	g_free (full_key);

	return value;
}

GList *
gnome_vfs_mime_get_short_list_applications (const char *mime_type)
{
	GList *system_short_list;
	GList *short_list_addition_list;
	GList *short_list_removal_list;
	GList *id_list;
	GList *p;
	GnomeVFSMimeApplication *application;
	GList *preferred_applications;

	if (mime_type == NULL) {
		return NULL;
	}


	system_short_list = comma_separated_str_to_str_list (gnome_vfs_mime_get_value_for_user_level 
							     (mime_type, 
							      "short_list_application_ids"));

	/* get user short list delta (add list and remove list) */

	short_list_addition_list = comma_separated_str_to_str_list (gnome_vfs_mime_get_value
								    (mime_type,
								     "short_list_application_user_additions"));
	short_list_removal_list = comma_separated_str_to_str_list (gnome_vfs_mime_get_value
								   (mime_type,
								    "short_list_application_user_removals"));

	/* compute list modified by delta */

	id_list = gnome_vfs_mime_str_list_apply_delta (system_short_list, 
						       short_list_addition_list, 
						       short_list_removal_list);

	preferred_applications = NULL;

	for (p = id_list; p != NULL; p = p->next) {
		application = gnome_vfs_mime_application_new_from_id (p->data);
		if (application != NULL) {
			preferred_applications = g_list_prepend
				(preferred_applications, application);
		}
	}


	preferred_applications = g_list_reverse (preferred_applications);

	g_list_free_deep (system_short_list);
	g_list_free_deep (short_list_addition_list);
	g_list_free_deep (short_list_removal_list);

	g_list_free (id_list);

	return preferred_applications;
}


GList *
gnome_vfs_mime_get_short_list_components (const char *mime_type)
{
	GList *system_short_list;
	GList *short_list_addition_list;
	GList *short_list_removal_list;
	GList *iid_list;
	char *supertype;
	char *query;
	char *sort[2];
	char *iids_delimited;
	CORBA_Environment ev;
	OAF_ServerInfoList *info_list;
	GList *preferred_components;

	if (mime_type == NULL) {
		return NULL;
	}


	/* get short list IIDs for that user level */
	system_short_list = comma_separated_str_to_str_list (gnome_vfs_mime_get_value_for_user_level 
							     (mime_type, 
							      "short_list_component_iids"));

	/* get user short list delta (add list and remove list) */

	short_list_addition_list = comma_separated_str_to_str_list (gnome_vfs_mime_get_value
								    (mime_type,
								     "short_list_component_user_additions"));

	short_list_removal_list = comma_separated_str_to_str_list (gnome_vfs_mime_get_value
								   (mime_type,
								    "short_list_component_user_removals"));

	/* compute list modified by delta */

	iid_list = gnome_vfs_mime_str_list_apply_delta (system_short_list, 
						       short_list_addition_list, 
						       short_list_removal_list);

	/* Do usual query but requiring that IIDs be one of the ones
           in the short list IID list. */
	
	preferred_components = NULL;
	if (iid_list != NULL) {
		CORBA_exception_init (&ev);

		iids_delimited = join_str_list ("','", iid_list);
		
		supertype = mime_type_get_supertype (mime_type);
		
		/* FIXME bugzilla.eazel.com 1142: should probably check for
		   the right interfaces too. Also slightly semantically
		   different from nautilus in other tiny ways.
		*/
		query = g_strconcat ("bonobo:supported_mime_types.has_one (['", mime_type, 
				     "', '", supertype,
				     "', '*'])", NULL);
		
		/* Alphebetize by name, for the sake of consistency */
		sort[0] = g_strdup ("name");
		sort[1] = NULL;
		
		info_list = oaf_query (query, sort, &ev);
		
		if (ev._major == CORBA_NO_EXCEPTION) {
			preferred_components = OAF_ServerInfoList_to_ServerInfo_g_list (info_list);
			CORBA_free (info_list);
		}

		g_free (iids_delimited);
		g_free (supertype);
		g_free (query);
		g_free (sort[0]);

		CORBA_exception_free (&ev);
	}

	g_list_free_deep (system_short_list);
	g_list_free_deep (short_list_addition_list);
	g_list_free_deep (short_list_removal_list);
	g_list_free (iid_list);

	return preferred_components;
}



GList *
gnome_vfs_mime_get_all_applications (const char *mime_type)
{
 	const char *system_all_application_ids;
 	const char *user_all_application_ids;
	GList *system_apps;
	GList *user_apps;

	if (mime_type == NULL) {
		return NULL;
	}

	/* FIXME bugzilla.eazel.com 1075: 
	   no way for apps to modify at install time */

	/* get app list */
 	system_all_application_ids = gnome_vfs_mime_get_value 
		(mime_type, "system_all_application_ids");
	
	system_apps = process_app_list (system_all_application_ids);
	
	/* get user app list extension */

 	user_all_application_ids = gnome_vfs_mime_get_value 
		(mime_type, "all_application_ids");
		
	user_apps = process_app_list (user_all_application_ids);
	
	/* merge the two */

	return g_list_concat (system_apps, user_apps);
}

GList *
gnome_vfs_mime_get_all_components (const char *mime_type)
{
	OAF_ServerInfoList *info_list;
	GList *components_list;
	CORBA_Environment ev;
	char *supertype;
	char *query;
	char *sort[2];

	if (mime_type == NULL) {
		return NULL;
	}

	CORBA_exception_init (&ev);

	/* Find a component that supports either the exact mime type,
           the supertype, or all mime types. */

	/* FIXME bugzilla.eazel.com 1142: should probably check for
           the right interfaces too. Also slightly semantically
           different from nautilus in other tiny ways.
	*/
	supertype = mime_type_get_supertype (mime_type);
	query = g_strconcat ("bonobo:supported_mime_types.has_one (['", mime_type, 
			     "', '", supertype,
			     "', '*'])"
			     " AND bonobo:supported_uri_schemes.defined ()", NULL);
	g_free (supertype);
	
	/* Alphebetize by name, for the sake of consistency */
	sort[0] = g_strdup ("name");
	sort[1] = NULL;

	info_list = oaf_query (query, sort, &ev);
	
	if (ev._major == CORBA_NO_EXCEPTION) {
		components_list = OAF_ServerInfoList_to_ServerInfo_g_list (info_list);
		CORBA_free (info_list);
	} else {
		components_list = NULL;
	}

	g_free (query);
	g_free (sort[0]);

	CORBA_exception_free (&ev);

	return components_list;
}




void
gnome_vfs_mime_set_default_action_type (const char *mime_type,
					GnomeVFSMimeActionType action_type)
{
	char *user_mime_file;
	FILE *f;
	const char *action_string;

	if (mime_type == NULL) {
		return;
	}

	switch (action_type) {
	case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
		action_string = "application";
		break;		
	case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
		action_string = "component";
		break;
	case GNOME_VFS_MIME_ACTION_TYPE_NONE:
	default:
		action_string = "none";
	}

	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tdefault_action_type=%s\n", action_string);
	fclose (f);

	g_free (user_mime_file);
	gnome_vfs_mime_info_reload ();
}

void 
gnome_vfs_mime_set_default_application (const char *mime_type,
				             const char *application_id)
{
	char *user_mime_file;
	FILE *f;

	if (mime_type == NULL) {
		return;
	}

	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tdefault_application_id=%s\n", application_id);
	fclose (f);

	g_free (user_mime_file);
	gnome_vfs_mime_info_reload ();


	/* If there's no default action type, set it to match this. */
	if (application_id != NULL && 
	    gnome_vfs_mime_get_default_action_type (mime_type) == GNOME_VFS_MIME_ACTION_TYPE_NONE) {
		gnome_vfs_mime_set_default_action_type (mime_type, GNOME_VFS_MIME_ACTION_TYPE_APPLICATION);
	}
}

void
gnome_vfs_mime_set_default_component (const char *mime_type,
				      const char *component_iid)
{
	char *user_mime_file;
	FILE *f;

	if (mime_type == NULL) {
		return;
	}

	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tdefault_component_iid=%s\n", component_iid);
	fclose (f);

	g_free (user_mime_file);
	gnome_vfs_mime_info_reload ();

	/* If there's no default action type, set it to match this. */
	if (component_iid != NULL && 
	    gnome_vfs_mime_get_default_action_type (mime_type) == GNOME_VFS_MIME_ACTION_TYPE_NONE) {
		gnome_vfs_mime_set_default_action_type (mime_type, GNOME_VFS_MIME_ACTION_TYPE_COMPONENT);
	}
}

void
gnome_vfs_mime_set_short_list_applications (const char *mime_type,
					    GList *application_ids)
{
	char *user_mime_file;
	char *user_level;
	char *id_list_key;
	char *addition_string;
	char *removal_string;
	const char *short_list_id_str;
	GList *short_list_id_list;
	GList *short_list_addition_list;
	GList *short_list_removal_list;
	FILE *f;

	if (mime_type == NULL) {
		return;
	}

	/* Get base list. */
	/* Base list depends on user level. */
	user_level = get_user_level ();
	id_list_key = g_strconcat ("short_list_application_ids_for_",
				   user_level,
				   "_user_level",
				   NULL);
	g_free (user_level);
	short_list_id_str = gnome_vfs_mime_get_value (mime_type, id_list_key);
	g_free (id_list_key);

	short_list_id_list = comma_separated_str_to_str_list (short_list_id_str);

	/* Compute delta. */
	
	short_list_addition_list = str_list_difference (application_ids, short_list_id_list);
	short_list_removal_list = str_list_difference (short_list_id_list, application_ids);

	addition_string = str_list_to_comma_separated_str (short_list_addition_list);
	removal_string = str_list_to_comma_separated_str (short_list_removal_list);

	g_list_free_deep (short_list_id_list);
	g_list_free (short_list_addition_list);
	g_list_free (short_list_removal_list);

	/* Write it. */

	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tshort_list_application_user_additions=%s\n", addition_string);
	fprintf (f, "\tshort_list_application_user_removals=%s\n", removal_string);
	fclose (f);

	g_free (user_mime_file);

	g_free (addition_string);
	g_free (removal_string);
	gnome_vfs_mime_info_reload ();
}


void
gnome_vfs_mime_set_short_list_components (const char *mime_type,
					  GList *component_iids)
{
	char *user_mime_file;
	char *user_level;
	char *id_list_key;
	char *addition_string;
	char *removal_string;
	const char *short_list_id_str;
	GList *short_list_id_list;
	GList *short_list_addition_list;
	GList *short_list_removal_list;
	FILE *f;

	if (mime_type == NULL) {
		return;
	}

	/* Get base list. */
	/* Base list depends on user level. */
	user_level = get_user_level ();
	id_list_key = g_strconcat ("short_list_component_iids_for_",
				   user_level,
				   "_user_level",
				   NULL);
	g_free (user_level);
	short_list_id_str = gnome_vfs_mime_get_value (mime_type, id_list_key);
	g_free (id_list_key);

	short_list_id_list = comma_separated_str_to_str_list (short_list_id_str);

	/* Compute delta. */
	
	short_list_addition_list = str_list_difference (component_iids, short_list_id_list);
	short_list_removal_list = str_list_difference (short_list_id_list, component_iids);

	addition_string = str_list_to_comma_separated_str (short_list_addition_list);
	removal_string = str_list_to_comma_separated_str (short_list_removal_list);

	g_list_free (short_list_addition_list);
	g_list_free (short_list_removal_list);

	/* Write it. */

	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tshort_list_component_user_additions=%s\n", addition_string);
	fprintf (f, "\tshort_list_component_user_removals=%s\n", removal_string);
	fclose (f);

	g_free (user_mime_file);
	g_free (addition_string);
	g_free (removal_string);
	gnome_vfs_mime_info_reload ();
}

/* FIXME bugzilla.eazel.com 1148: 
 * The next set of helper functions are all replicated in nautilus-mime-actions.c.
 * Need to refactor so they can share code.
 */
static gint
gnome_vfs_mime_application_has_id (GnomeVFSMimeApplication *application, const char *id)
{
	return strcmp (application->id, id);
}

static gint
gnome_vfs_mime_id_matches_application (const char *id, GnomeVFSMimeApplication *application)
{
	return gnome_vfs_mime_application_has_id (application, id);
}

static gint
gnome_vfs_mime_id_matches_component (const char *iid, OAF_ServerInfo *component)
{
	return strcmp (component->iid, iid);
}

static gint 
gnome_vfs_mime_application_matches_id (GnomeVFSMimeApplication *application, const char *id)
{
	return gnome_vfs_mime_id_matches_application (id, application);
}

static gint 
gnome_vfs_mime_component_matches_id (OAF_ServerInfo *component, const char *iid)
{
	return gnome_vfs_mime_id_matches_component (iid, component);
}

/**
 * gnome_vfs_mime_id_in_application_list:
 * 
 * Check whether an application id is in a list of GnomeVFSMimeApplications.
 * 
 * @id: An application id.
 * @applications: A GList * whose nodes are GnomeVFSMimeApplications, such as the
 * result of gnome_vfs_mime_get_short_list_applications.
 * 
 * Return value: TRUE if an application whose id matches @id is in @applications.
 */
gboolean
gnome_vfs_mime_id_in_application_list (const char *id, GList *applications)
{
	return g_list_find_custom (applications, (gpointer) id, (GCompareFunc) gnome_vfs_mime_application_matches_id) != NULL;
}

/**
 * gnome_vfs_mime_id_in_component_list:
 * 
 * Check whether a component iid is in a list of OAF_ServerInfos.
 * 
 * @iid: A component iid.
 * @applications: A GList * whose nodes are OAF_ServerInfos, such as the
 * result of gnome_vfs_mime_get_short_list_components.
 * 
 * Return value: TRUE if a component whose iid matches @iid is in @components.
 */
gboolean
gnome_vfs_mime_id_in_component_list (const char *iid, GList *components)
{
	return g_list_find_custom (components, (gpointer) iid, (GCompareFunc) gnome_vfs_mime_component_matches_id) != NULL;
}

/**
 * gnome_vfs_mime_id_list_from_application_list:
 * 
 * Create a list of application ids from a list of GnomeVFSMimeApplications.
 * 
 * @applications: A GList * whose nodes are GnomeVFSMimeApplications, such as the
 * result of gnome_vfs_mime_get_short_list_applications.
 * 
 * Return value: A new list where each GnomeVFSMimeApplication in the original
 * list is replaced by a char * with the application's id. The original list is
 * not modified.
 */
GList *
gnome_vfs_mime_id_list_from_application_list (GList *applications)
{
	GList *result;
	GList *node;

	result = NULL;
	
	for (node = applications; node != NULL; node = node->next) {
		result = g_list_append 
			(result, g_strdup (((GnomeVFSMimeApplication *)node->data)->id));
	}

	return result;
}

/**
 * gnome_vfs_mime_id_list_from_component_list:
 * 
 * Create a list of component iids from a list of OAF_ServerInfos.
 * 
 * @components: A GList * whose nodes are OAF_ServerInfos, such as the
 * result of gnome_vfs_mime_get_short_list_components.
 * 
 * Return value: A new list where each OAF_ServerInfo in the original
 * list is replaced by a char * with the component's iid. The original list is
 * not modified.
 */
GList *
gnome_vfs_mime_id_list_from_component_list (GList *components)
{
	GList *list;
	GList *node;

	list = NULL;
	
	for (node = components; node != NULL; node = node->next) {
		list = g_list_prepend 
			(list, g_strdup (((OAF_ServerInfo *)node->data)->iid));
	}

	return g_list_reverse (list);
}

static void
g_list_free_deep (GList *list)
{
	g_list_foreach (list, (GFunc) g_free, NULL);
	g_list_free (list);
}

void
gnome_vfs_mime_add_application_to_short_list (const char *mime_type,
					      const char *application_id)
{
	GList *old_list, *new_list;

	old_list = gnome_vfs_mime_get_short_list_applications (mime_type);

	if (!gnome_vfs_mime_id_in_application_list (application_id, old_list)) {
		new_list = g_list_append (gnome_vfs_mime_id_list_from_application_list (old_list), 
					  g_strdup (application_id));
		gnome_vfs_mime_set_short_list_applications (mime_type, new_list);
		g_list_free_deep (new_list);
	}

	gnome_vfs_mime_application_list_free (old_list);
}						   

/**
 * gnome_vfs_mime_remove_application_from_list:
 * 
 * Remove an application specified by id from a list of GnomeVFSMimeApplications.
 * 
 * @applications: A GList * whose nodes are GnomeVFSMimeApplications, such as the
 * result of gnome_vfs_mime_get_short_list_applications.
 * @application_id: The id of an application to remove from @applications.
 * @did_remove: If non-NULL, this is filled in with TRUE if the application
 * was found in the list, FALSE otherwise.
 * 
 * Return value: The modified list. If the application is not found, the list will 
 * be unchanged.
 */
GList *
gnome_vfs_mime_remove_application_from_list (GList *applications, 
					     const char *application_id,
					     gboolean *did_remove)
{
	GList *matching_node;
	
	matching_node = g_list_find_custom 
		(applications, (gpointer)application_id,
		 (GCompareFunc) gnome_vfs_mime_application_matches_id);
	if (matching_node != NULL) {
		applications = g_list_remove_link (applications, matching_node);
		gnome_vfs_mime_application_list_free (matching_node);
	}

	if (did_remove != NULL) {
		*did_remove = matching_node != NULL;
	}

	return applications;
}					     

void
gnome_vfs_mime_remove_application_from_short_list (const char *mime_type,
						   const char *application_id)
{
	GList *old_list, *new_list;
	gboolean was_in_list;

	old_list = gnome_vfs_mime_get_short_list_applications (mime_type);
	old_list = gnome_vfs_mime_remove_application_from_list 
		(old_list, application_id, &was_in_list);

	if (was_in_list) {
		new_list = gnome_vfs_mime_id_list_from_application_list (old_list);
		gnome_vfs_mime_set_short_list_applications (mime_type, new_list);
		g_list_free_deep (new_list);
	}

	gnome_vfs_mime_application_list_free (old_list);
}						   

void
gnome_vfs_mime_add_component_to_short_list (const char *mime_type,
					    const char *iid)
{
	GList *old_list, *new_list;

	old_list = gnome_vfs_mime_get_short_list_components (mime_type);

	if (!gnome_vfs_mime_id_in_component_list (iid, old_list)) {
		new_list = g_list_append (gnome_vfs_mime_id_list_from_component_list (old_list), 
					  g_strdup (iid));
		gnome_vfs_mime_set_short_list_components (mime_type, new_list);
		g_list_free_deep (new_list);
	}

	gnome_vfs_mime_component_list_free (old_list);
}						   

/**
 * gnome_vfs_mime_remove_component_from_list:
 * 
 * Remove a component specified by iid from a list of OAF_ServerInfos.
 * 
 * @components: A GList * whose nodes are OAF_ServerInfos, such as the
 * result of gnome_vfs_mime_get_short_list_components.
 * @iid: The iid of a component to remove from @components.
 * @did_remove: If non-NULL, this is filled in with TRUE if the component
 * was found in the list, FALSE otherwise.
 * 
 * Return value: The modified list. If the component is not found, the list will 
 * be unchanged.
 */
GList *
gnome_vfs_mime_remove_component_from_list (GList *components, 
					   const char *iid,
					   gboolean *did_remove)
{
	GList *matching_node;
	
	matching_node = g_list_find_custom 
		(components, (gpointer)iid,
		 (GCompareFunc) gnome_vfs_mime_component_matches_id);
	if (matching_node != NULL) {
		components = g_list_remove_link (components, matching_node);
		gnome_vfs_mime_component_list_free (matching_node);
	}

	if (did_remove != NULL) {
		*did_remove = matching_node != NULL;
	}

	return components;
}					     

void
gnome_vfs_mime_remove_component_from_short_list (const char *mime_type,
						 const char *iid)
{
	GList *old_list, *new_list;
	gboolean was_in_list;

	old_list = gnome_vfs_mime_get_short_list_components (mime_type);
	old_list = gnome_vfs_mime_remove_component_from_list 
		(old_list, iid, &was_in_list);

	if (was_in_list) {
		new_list = gnome_vfs_mime_id_list_from_component_list (old_list);
		gnome_vfs_mime_set_short_list_components (mime_type, new_list);
		g_list_free_deep (new_list);
	}

	gnome_vfs_mime_component_list_free (old_list);
}						   


void
gnome_vfs_mime_extend_all_applications (const char *mime_type,
					GList *application_ids)
{
	char *user_mime_file;
	const char *user_all_application_ids;
	GList *user_id_list;
	GList *extras;
	GList *update_list;
	char *update_str;
	FILE *f;

 	if (mime_type == NULL) {
		return;
	}

	user_all_application_ids = gnome_vfs_mime_get_value 
		(mime_type, "all_application_ids");
	user_id_list = comma_separated_str_to_str_list (user_all_application_ids);

	extras = str_list_difference (application_ids, user_id_list);

	update_list = g_list_concat (g_list_copy (user_id_list), extras);

	update_str = str_list_to_comma_separated_str (update_list);

	g_list_free_deep (user_id_list);
	
	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tall_application_ids=%s\n", update_str);
	fclose (f);

	g_free (update_str);
	g_free (user_mime_file);
	gnome_vfs_mime_info_reload ();
}


void
gnome_vfs_mime_remove_from_all_applications (const char *mime_type,
					     GList *application_ids)
{
	char *user_mime_file;
	const char *user_all_application_ids;
	GList *user_id_list;
	GList *update_list;
	char *update_str;
	FILE *f;

	if (mime_type == NULL) {
		return;
	}

 	user_all_application_ids = gnome_vfs_mime_get_value 
		(mime_type, "all_application_ids");
	user_id_list = comma_separated_str_to_str_list (user_all_application_ids);

	update_list = str_list_difference (user_id_list, application_ids);

	update_str = str_list_to_comma_separated_str (update_list);

	g_list_free_deep (user_id_list);
	
	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", mime_type);
	fprintf (f, "\tall_application_ids=%s\n", update_str);
	fclose (f);

	g_free (update_str);
	g_free (user_mime_file);
	gnome_vfs_mime_info_reload ();
}

void
gnome_vfs_mime_define_application (GnomeVFSMimeApplication *application)
{
	char *hack_mime_type;
	char *user_mime_file;
	FILE *f;

	g_return_if_fail (application != NULL);

	hack_mime_type = g_strconcat ("x-application-registry-hack/", application->id, NULL);

	user_mime_file = gnome_util_home_file ("mime-info/user.keys");

	/* FIXME bugzilla.eazel.com 1119: Is it OK to always append? */
	/* FIXME bugzilla.eazel.com 1156: Is it OK to ignore errors? */
	f = fopen (user_mime_file, "a");
	fputs ("\n", f);
	fprintf (f, "%s:\n", hack_mime_type);
	fprintf (f, "\tname=%s\n", application->name);
	fprintf (f, "\tcommand=%s\n", application->command);
	fprintf (f, "\tcan_open_multiple_files=%s\n",
		 bool_to_str (application->can_open_multiple_files));
	fprintf (f, "\tcan_open_uris=%s\n",
		 bool_to_str (application->can_open_uris));
	fclose (f);

	g_free (user_mime_file);
	g_free (hack_mime_type);
	gnome_vfs_mime_info_reload ();
}




GnomeVFSMimeApplication *
gnome_vfs_mime_application_copy (GnomeVFSMimeApplication *application)
{
	GnomeVFSMimeApplication *result;

	if (application == NULL) {
		return NULL;
	}
	
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
	if (application != NULL) {
		g_free (application->name);
		g_free (application->command);
		g_free (application->id);
		g_free (application);
	}
}


void
gnome_vfs_mime_action_free (GnomeVFSMimeAction *action) 
{
	switch (action->action_type) {
	case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
		gnome_vfs_mime_application_free (action->action.application);
	case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
		CORBA_free (action->action.component);
	default:
		g_assert_not_reached ();
	}

	g_free (action);
}

void
gnome_vfs_mime_application_list_free (GList *list)
{
	g_list_foreach (list, (GFunc) gnome_vfs_mime_application_free, NULL);
}

void
gnome_vfs_mime_component_list_free (GList *list)
{
	g_list_foreach (list, (GFunc) CORBA_free, NULL);
}



static gboolean
str_to_bool (const char *str)
{
	return str != NULL &&
		(strcasecmp (str, "true") == 0 ||
		 strcasecmp (str, "yes") == 0);
}

static const char *
bool_to_str (gboolean bool)
{
	return bool ? "true" : "false";
}


static char *
join_str_list (const char *separator, GList *list)
{
	char **strv;
	GList *p;
	int i;
	char *retval;

	strv = g_new0 (char *, g_list_length (list) + 1);

	/* Convert to a strv so we can use g_strjoinv.
	 * Saves code but could be made faster if we want.
	 */
	strv = g_new (char *, g_list_length (list) + 1);
	for (p = list, i = 0; p != NULL; p = p->next, i++) {
		strv[i] = (char *) p->data;
	}
	strv[i] = NULL;

	retval = g_strjoinv (separator, strv);

	g_free (strv);

	return retval;
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
	retval->attrs._release = FALSE;

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
 * This does beg the question: Why does gnome-vfs have the Nautilus
 * user level coded into it? Eventually we might want to call this the
 * GNOME user level or something if we can figure out a clear concept
 * that works across GNOME.
 */
static char *
get_user_level (void)
{
	static GConfEngine *engine = NULL;
	char *user_level;

	/* Create the gconf engine once. */
	if (engine == NULL) {
		/* This sequence is needed in case no one has initialized GConf. */
		if (!gconf_is_initialized ()) {
			char *fake_argv[] = { "gnome-vfs", NULL };
			gconf_init (1, fake_argv, NULL);
		}

		engine = gconf_engine_new ();
		/* FIXME bugzilla.eazel.com 1150: This engine never gets freed. */
	}

	user_level = gconf_get_string (engine, "/apps/nautilus/user_level", NULL);

	if (user_level == NULL) {
		user_level = g_strdup ("novice");
	}

	/* If value is invalid, assume "novice". */
	if (strcmp (user_level, "novice") != 0 &&
	    strcmp (user_level, "intermediate") != 0 &&
	    strcmp (user_level, "hacker") != 0) {
		g_free (user_level);
		user_level = g_strdup ("novice");
	}

	return user_level;
}





static GList *
gnome_vfs_strsplit_to_list (const char *str, const char *delim, int max)
{
	char **strv;
	GList *retval;
	int i;

	strv = strsplit_handle_null (str, delim, max);

	retval = NULL;

	for (i = 0; strv[i] != NULL; i++) {
		retval = g_list_prepend (retval, strv[i]);
	}

	retval = g_list_reverse (retval);
	
	/* Don't strfreev, since we didn't copy the individual strings. */
	g_free (strv);

	return retval;
}

static char *
gnome_vfs_strjoin_from_list (const char *separator, GList *list)
{
	char **strv;
	int i;
	GList *p;
	char *retval;

	strv = g_new0 (char *, (g_list_length (list) + 1));

	for (p = list, i = 0; p != NULL; p = p->next, i++) {
		strv[i] = p->data;
	}

	retval = g_strjoinv (separator, strv);

	g_free (strv);

	return retval;
}

static GList *
comma_separated_str_to_str_list (const char *str)
{
	return gnome_vfs_strsplit_to_list (str, ",", 0);
}

static char *
str_list_to_comma_separated_str (GList *list)
{
	return gnome_vfs_strjoin_from_list (",", list);
}


static GList *
str_list_difference (GList *a, GList *b)
{
	GList *p;
	GList *retval;

	retval = NULL;

	for (p = a; p != NULL; p = p->next) {
		if (g_list_find_custom (b, p->data, (GCompareFunc) strcmp) == NULL) {
			retval = g_list_prepend (retval, p->data);
		}
	}

	retval = g_list_reverse (retval);
	return retval;
}
