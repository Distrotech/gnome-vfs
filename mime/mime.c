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


/* Open questions/Random thoughts: 
 *
 * I'd like to deprecate the whole "short list" concept (would be replaced by
 * setting a relevance for each mime-type/app mapping), what to do with the
 * set_short_list functions ? (the others are easy enough to simulate, this
 * one would lead to an ugly hack which would be "remove all the apps which
 * would be in the short list for this user, and re-add the new ones".
 *
 * What to do with the get_icon function ? it would be nice if it could return
 * a themed mime-type icon (ie the same as the one Nautilus will use)
 * => after talking with alexl, it should be possible to return the name
 * of the icon used by nautilus
 *
 * What to do when the same app is registered with two different relevances
 * for the same mime type ? => Only keep the most relevant one ?
 * 
 * What to do with the add_extension/remove_extension functions ?
 *
 * Something clever should be done when one of the editing function is used
 * on a mime type we don't know yet
 *
 * Is USER_MODIFIED the same as USER_ADDED in practice ?
 *
 * Descriptions modified with gnome_vfs_mime_set_description should be reloaded
 * "as is", ie without trying to find the more appropriate one wrt to the 
 * current locale. Currently, if a user modifies a mime type (without changing
 * the description) and the switch locale, he will get the description
 * in the locale used when he modified the mime type
 *
 * The mime-type loading/saving could (should ?) be a per-user daemon, 
 * something simple which could send/be sent a fragment of xml describing 
 * the stuff we need to know about a mime-type would probably be enough. That
 * would solve some potential concurrency problems when several apps modifies
 * the same mime-type at the same time.
 *
 * The file-types capplet from control center uses a use_category_default 
 * attribute, what does it correspond to ?
 *
 */

/* Unimplemented stuff:
 *
 * icon_filename and can_be_executable per-mime-type attributes
 *
 * user-defined per-mime-type attributes
 *
 * saving/loading of user changes to the mime types
 *
 * gnome_vfs_mime_can_be_executable, gnome_vfs_set_can_be_executable
 * gnome_vfs_mime_get_icon, gnome_vfs_mime_set_icon
 * gnome_vfs_get_default_component
 * gnome_vfs_get_short_list_component
 * gnome_vfs_get_all_components
 * gnome_vfs_set_default_component
 * gnome_vfs_set_short_list_applications, gnome_vfs_set_short_list_components
 * gnome_vfs_mime_add_application_to_short_list
 * gnome_vfs_mime_remove_application_from_short_list
 * gnome_vfs_mime_add_component_to_short_list
 * gnome_vfs_mime_remove_component_from_short_list
 * gnome_vfs_mime_add_extension, gnome_vfs_mime_remove_extension
 *
 * doesn't handle supertypes
 */

/* TODO:
 *
 * Document how this work a bit more (hopefully it is easy to 
 * understand from the code)
 * 
 * Document the various bits useful to app writers (gnome-specific additions
 * to the xml file describing mime types, various relevance levels, ...)
 *
 * Check with Michael if/how relevance could be added to .server files
 *
 * Rewrite gnome_vfs_application_registry_get_applications (and probably 
 * some other parts of gnome-vfs-application-registry.c) so that it uses
 * the info stored in the mime_types hash table (this should probably be 
 * deprecated in favour of gnome_vfs_mime_get_all_applications)
 * => gnome-vfs-application-registry shouldn't do anything related to 
 * mime-type handling except querying or adding (removing ?) info to the 
 * mime database implemented in this file
 *
 * Go through the whole code to take into account USER_REMOVED
 * => hopefully this is ok now
 *
 * Split this ugly fat file in two parts as it was done before with the
 * gnome-vfs-mime-handlers/gnome-vfs-mime-info split: the mime-info file
 * contained the core mime db code (ie the code directly accessing/modifying/
 * saving it), while mime-handlers only called some helper functions provided
 * by mime-info. That would shrink this file size, as well as help polish
 * the API to access the mime db.
 *
 * Need to have a way to get an app-id from a .desktop file, or from an 
 * executable name/path
 */


#include "mime-db.h"

#include <libgnomevfs/gnome-vfs-application-registry.h>
#include <libgnomevfs/gnome-vfs-utils.h>

/**
 * gnome_vfs_mime_get_description:
 * @mime_type: the mime type
 *
 * Query the MIME database for a description of the specified MIME type.
 *
 * Return value: A description of MIME type @mime_type
 */
const char *
gnome_vfs_mime_get_description (const char *mime_type)
{
	struct MimeType *type;
	
	type = find_mime_type (mime_type); 
	if (type != NULL) {
		return type->desc;
	} else {
		return NULL;
	}
}

/**
 * gnome_vfs_mime_set_description:
 * @mime_type: A const char * containing a mime type
 * @description: A description of this MIME type
 * 
 * Set the description of this MIME type in the MIME database. The description
 * should be something like "Gnumeric spreadsheet".
 * 
 * Return value: Always returns GNOME_VFS_OK
 **/
GnomeVFSResult
gnome_vfs_mime_set_description (const char *mime_type, const char *description)
{
	struct MimeType *type;

	type = find_mime_type (mime_type); 
	
	if (type == NULL) {
		return GNOME_VFS_ERROR_INTERNAL;
	}

	if (type->desc != NULL) {
		g_free (type->desc);
	}
	type->desc = g_strdup (description);
	if (type->state == DEFAULT) {
		type->state = USER_MODIFIED;
	}

	return GNOME_VFS_OK;
}


/**
 * gnome_vfs_mime_get_default_action_type:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * 
 * Query the MIME database for the type of action to be performed on a particular MIME type by default.
 * 
 * Return value: The type of action to be performed on a file of 
 * MIME type, @mime_type by default.
 **/
GnomeVFSMimeActionType
gnome_vfs_mime_get_default_action_type (const char *mime_type)
{
	struct MimeType *type;

	type = find_mime_type (mime_type); 

	if (type == NULL) {
		return GNOME_VFS_MIME_ACTION_TYPE_NONE;
	}

	return type->default_action;
}


/**
 * gnome_vfs_mime_get_default_action:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * 
 * Query the MIME database for default action associated with a particular MIME type @mime_type.
 * 
 * Return value: A GnomeVFSMimeAction representing the default action to perform upon
 * file of type @mime_type.
 **/
GnomeVFSMimeAction *
gnome_vfs_mime_get_default_action (const char *mime_type)
{
	GnomeVFSMimeAction *action;

	g_assert (mime_type != NULL);

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

/**
 * gnome_vfs_mime_get_default_application:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * 
 * Query the MIME database for the application to be executed on files of MIME type
 * @mime_type by default.
 * 
 * Return value: A GnomeVFSMimeApplication representing the default handler of @mime_type
 **/
GnomeVFSMimeApplication *
gnome_vfs_mime_get_default_application (const char *mime_type)
{
	GnomeVFSMimeApplication *default_application = NULL;
	GList *it;
	gchar *default_id = NULL;

	struct MimeType *type;

	type = find_mime_type (mime_type); 

	if ((type == NULL) || (type->helpers == NULL)) {
		return NULL;
	}
	
	/* FIXME: needs to be able to tell the difference between apps and
	 * components in the helpers list
	 * FIXME: should the helpers list be sorted by relevance ?
	 */
	for (it = type->helpers; it != NULL; it = it->next) {
		static int min = 99;
		struct MimeHelper *helper;

		helper = (struct MimeHelper *)it->data;
		if (helper->relevance < min) {
			min = helper->relevance;
			default_id = helper->app_id;
		}
	}

	if (default_id == NULL) {
		return NULL;
	}

	default_application =	
		gnome_vfs_application_registry_get_mime_application (default_id);
	return default_application;
}


static gboolean
application_known_to_be_nonexistent (const char *application_id)
{
	const char *command;

	g_return_val_if_fail (application_id != NULL, FALSE);

	command = gnome_vfs_application_registry_peek_value
		(application_id,
		 GNOME_VFS_APPLICATION_REGISTRY_COMMAND);

	if (command == NULL) {
		return TRUE;
	}

	return !gnome_vfs_is_executable_command_string (command);
}

static GList *
prune_ids_for_nonexistent_applications (GList *list)
{
	GList *p, *next;

	for (p = list; p != NULL; p = next) {
		next = p->next;

		if (application_known_to_be_nonexistent (p->data)) {
			list = g_list_remove_link (list, p);
			g_free (p->data);
			g_list_free_1 (p);
		}
	}

	return list;
}

/* sort_application_list
 *
 * Sort list alphabetically
 */
static int
sort_application_list (gconstpointer a, gconstpointer b)
{
	GnomeVFSMimeApplication *application1, *application2;

	application1 = (GnomeVFSMimeApplication *) a;
	application2 = (GnomeVFSMimeApplication *) b;

	return g_ascii_strcasecmp (application1->name, application2->name);
}


static GList *
get_app_ids_with_max_relevance (const char *mime_type, gint relevance)
{
	struct MimeType *type;
	GList *app_ids = NULL;
	GList *it;

	type = find_mime_type (mime_type); 

	if ((type == NULL) || (type->helpers == NULL)) {
		return NULL;
	}

	for (it = type->helpers; it != NULL; it = it->next) {
		struct MimeHelper *helper;

		helper = (struct MimeHelper *)it->data;
		if ((helper->relevance <= relevance) 
		    && (helper->state != USER_REMOVED)) {
			app_ids = g_list_prepend (app_ids, 
						  g_strdup (helper->app_id));
		} else {
			/* the helpers list is sorted by relevance */
			break;
		}
	}
	
	app_ids = prune_ids_for_nonexistent_applications (app_ids);

	return app_ids;
}

static GList *
app_ids_to_vfs_application_list (GList *app_ids)
{
	GList *it;
	GnomeVFSMimeApplication *application;
	GList *vfs_apps = NULL;

	vfs_apps = NULL;
	for (it = app_ids; it != NULL; it = it->next) {
		application = gnome_vfs_application_registry_get_mime_application (it->data);
		if (application != NULL) {
			vfs_apps = g_list_insert_sorted
				(vfs_apps, application, 
				 sort_application_list);
		}
	}
	
	return vfs_apps;
}

/* CF: deprecate, or at least rename to gnome_vfs_mime_get_preferred_apps */
/**
 * gnome_vfs_mime_get_short_list_applications:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * 
 * Return an alphabetically sorted list of GnomeVFSMimeApplication
 * data structures for the requested mime type.	The short list contains
 * "select" applications recommended for handling this MIME type, appropriate 
 * for display to the user.
 * 
 * Return value: A GList * where the elements are GnomeVFSMimeApplication *
 * representing various applications to display in the short list for @mime_type.
 **/ 
GList *
gnome_vfs_mime_get_short_list_applications (const char *mime_type)
{
	GList *app_ids;
	GList *preferred_apps;

	app_ids = get_app_ids_with_max_relevance (mime_type, 50);
	
	/* FIXME: the old code fell back to super types (eg text\*) if 
	 * preferred_apps is empty
	 */

	preferred_apps =  app_ids_to_vfs_application_list (app_ids);
	g_list_free_deep (app_ids);
	return preferred_apps;
}

/**
 * gnome_vfs_mime_get_all_applications:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * 
 * Return an alphabetically sorted list of GnomeVFSMimeApplication
 * data structures representing all applications in the MIME database 
 * registered to handle files of MIME type @mime_type (and supertypes).
 * 
 * Return value: A GList * where the elements are GnomeVFSMimeApplication *
 * representing applications that handle MIME type @mime_type.
 **/ 
GList *
gnome_vfs_mime_get_all_applications (const char *mime_type)
{
	GList *app_ids;
	GList *preferred_apps;

	app_ids = get_app_ids_with_max_relevance (mime_type, 99);
	
	/* FIXME: the old code fell back to super types (eg text\*) if 
	 * preferred_apps is empty
	 */

	preferred_apps =  app_ids_to_vfs_application_list (app_ids);
	g_list_free_deep (app_ids);
	return preferred_apps;
}

/**
 * gnome_vfs_mime_set_default_action_type:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * @action_type: A GnomeVFSMimeActionType containing the action to perform 
 * by default
 * 
 * Sets the default action type to be performed on files of 
 * MIME type @mime_type.
 * 
 * Return value: GNOME_VFS_OK or GNOME_VFS_ERROR_INTERNAL
 **/
GnomeVFSResult
gnome_vfs_mime_set_default_action_type (const char *mime_type,
					GnomeVFSMimeActionType action_type)
{
	struct MimeType *type;

	type = find_mime_type (mime_type); 
	if (type == NULL) {
		return GNOME_VFS_ERROR_INTERNAL;
	}
	if (type->default_action != action_type) {
		type->default_action = action_type;
		if (type->state == DEFAULT) {
			type->state = USER_MODIFIED;
		}
	}

	return GNOME_VFS_OK;
}

/**
 * gnome_vfs_mime_set_default_application:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @application_id: A key representing an application in the MIME database 
 * (GnomeVFSMimeApplication->id, for example)
 * 
 * Sets the default application to be run on files of MIME type @mime_type.
 * 
 * Return value: GNOME_VFS_OK or GNOME_VFS_ERROR_INTERNAL
 **/
GnomeVFSResult
gnome_vfs_mime_set_default_application (const char *mime_type,
					const char *application_id)
{
	struct MimeHelper *helper;
	struct MimeType *type;

	g_assert (application_id != NULL);

	type = find_mime_type (mime_type); 
	if (type == NULL) {
		return GNOME_VFS_ERROR_INTERNAL;
	}

	helper = g_new0 (struct MimeHelper, 1);
	if (helper == NULL) {
		return GNOME_VFS_ERROR_INTERNAL;
	}

	helper->app_id = g_strdup (application_id);
	/* FIXME: need to define some relevance policy */
	helper->relevance = 1;
	helper->state = USER_ADDED;

	/* FIXME: need to do something if there is already a default app
	 * (ie need to change it relevance to something else)
	 * This also won't work properly if we promote a known helper
	 * to default app and then want to "unpromote" it
	 */
	add_helper_to_mime_type (type, helper);
	if (type->state == DEFAULT) {
		type->state = USER_MODIFIED;
	}

	if (gnome_vfs_mime_get_default_action_type (mime_type) == GNOME_VFS_MIME_ACTION_TYPE_NONE) {
		gnome_vfs_mime_set_default_action_type (mime_type, GNOME_VFS_MIME_ACTION_TYPE_APPLICATION);
	}

	return GNOME_VFS_OK;
}

/* The following functions are of doubtful interest, nautilus is the only app
 * to use them, and it doesn't use them much, they probably can be deprecated
 */
/* FIXME bugzilla.gnome.org 41148: 
 * The next set of helper functions are all replicated in nautilus-mime-actions.c.
 * Need to refactor so they can share code.
 */

/* CF: used less than 10 times in nautilus */
static gint
gnome_vfs_mime_application_has_id (GnomeVFSMimeApplication *application, const char *id)
{
	return strcmp (application->id, id);
}

/* CF: used 3 times in nautilus */
static gint
gnome_vfs_mime_id_matches_application (const char *id, GnomeVFSMimeApplication *application)
{
	return gnome_vfs_mime_application_has_id (application, id);
}

/* CF: not used in my gnome tree */
static gint
gnome_vfs_mime_id_matches_component (const char *iid, Bonobo_ServerInfo *component)
{
	return strcmp (component->iid, iid);
}

/* CF: not used in my gnome tree */
static gint 
gnome_vfs_mime_application_matches_id (GnomeVFSMimeApplication *application, const char *id)
{
	return gnome_vfs_mime_id_matches_application (id, application);
}

/* CF: not used in my gnome tree */
static gint 
gnome_vfs_mime_component_matches_id (Bonobo_ServerInfo *component, const char *iid)
{
	return gnome_vfs_mime_id_matches_component (iid, component);
}

/* CF: only used once in nautilus => deprecate */
/**
 * gnome_vfs_mime_id_in_application_list:
 * @id: An application id.
 * @applications: A GList * whose nodes are GnomeVFSMimeApplications, such as the
 * result of gnome_vfs_mime_get_short_list_applications().
 * 
 * Check whether an application id is in a list of GnomeVFSMimeApplications.
 * 
 * Return value: TRUE if an application whose id matches @id is in @applications.
 */
gboolean
gnome_vfs_mime_id_in_application_list (const char *id, GList *applications)
{
	return g_list_find_custom
		(applications, (gpointer) id,
		 (GCompareFunc) gnome_vfs_mime_application_matches_id) != NULL;
}

/* CF: only used once in nautilus => deprecate */
/**
 * gnome_vfs_mime_id_in_component_list:
 * @iid: A component iid.
 * @components: A GList * whose nodes are Bonobo_ServerInfos, such as the
 * result of gnome_vfs_mime_get_short_list_components().
 * 
 * Check whether a component iid is in a list of Bonobo_ServerInfos.
 * 
 * Return value: TRUE if a component whose iid matches @iid is in @components.
 */
gboolean
gnome_vfs_mime_id_in_component_list (const char *iid, GList *components)
{
	return g_list_find_custom
		(components, (gpointer) iid,
		 (GCompareFunc) gnome_vfs_mime_component_matches_id) != NULL;
	return FALSE;
}

/* CF: only used twice in nautilus => deprecate */
/**
 * gnome_vfs_mime_id_list_from_application_list:
 * @applications: A GList * whose nodes are GnomeVFSMimeApplications, such as the
 * result of gnome_vfs_mime_get_short_list_applications().
 * 
 * Create a list of application ids from a list of GnomeVFSMimeApplications.
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

/* CF: only used twice in nautilus => deprecate */
/**
 * gnome_vfs_mime_id_list_from_component_list:
 * @components: A GList * whose nodes are Bonobo_ServerInfos, such as the
 * result of gnome_vfs_mime_get_short_list_components().
 * 
 * Create a list of component iids from a list of Bonobo_ServerInfos.
 * 
 * Return value: A new list where each Bonobo_ServerInfo in the original
 * list is replaced by a char * with the component's iid. The original list is
 * not modified.
 */
GList *
gnome_vfs_mime_id_list_from_component_list (GList *components)
{
	GList *list = NULL;
	GList *node;

	for (node = components; node != NULL; node = node->next) {
		list = g_list_prepend 
			(list, g_strdup (((Bonobo_ServerInfo *)node->data)->iid));
	}
	return g_list_reverse (list);
}

static GList *
copy_str_list (GList *string_list)
{
	GList *copy, *node;
       
	copy = NULL;
	for (node = string_list; node != NULL; node = node->next) {
		copy = g_list_prepend (copy, 
				       g_strdup ((char *) node->data));
				       }
	return g_list_reverse (copy);
}

/**
 * gnome_vfs_mime_application_copy:
 * @application: The GnomeVFSMimeApplication to be duplicated.
 * 
 * Creates a newly referenced copy of a GnomeVFSMimeApplication object.
 * 
 * Return value: A copy of @application
 **/
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
	result->expects_uris = application->expects_uris;
	result->supported_uri_schemes = copy_str_list (application->supported_uri_schemes);
	result->requires_terminal = application->requires_terminal;

	return result;
}

/**
 * gnome_vfs_mime_application_free:
 * @application: The GnomeVFSMimeApplication to be freed
 * 
 * Frees a GnomeVFSMimeApplication *.
 * 
 **/
void
gnome_vfs_mime_application_free (GnomeVFSMimeApplication *application) 
{
	if (application != NULL) {
		g_free (application->name);
		g_free (application->command);
		g_list_foreach (application->supported_uri_schemes,
				(GFunc) g_free,
				NULL);
		g_list_free (application->supported_uri_schemes);
		g_free (application->id);
		g_free (application);
	}
}

/**
 * gnome_vfs_mime_action_free:
 * @action: The GnomeVFSMimeAction to be freed
 * 
 * Frees a GnomeVFSMimeAction *.
 * 
 **/
void
gnome_vfs_mime_action_free (GnomeVFSMimeAction *action) 
{
	switch (action->action_type) {
	case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
		gnome_vfs_mime_application_free (action->action.application);
		break;
	case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
		CORBA_free (action->action.component);
		break;
	default:
		g_assert_not_reached ();
	}

	g_free (action);
}

/**
 * gnome_vfs_mime_application_list_free:
 * @list: a GList of GnomeVFSApplication * to be freed
 * 
 * Frees lists of GnomeVFSApplications, as returned from functions such
 * as gnome_vfs_get_all_applications().
 * 
 **/
void
gnome_vfs_mime_application_list_free (GList *list)
{
	g_list_foreach (list, (GFunc) gnome_vfs_mime_application_free, NULL);
	g_list_free (list);
}

/**
 * gnome_vfs_mime_component_list_free:
 * @list: a GList of Bonobo_ServerInfo * to be freed
 * 
 * Frees lists of Bonobo_ServerInfo * (as returned from functions such
 * as @gnome_vfs_get_all_components)
 * 
 **/
void
gnome_vfs_mime_component_list_free (GList *list)
{
	g_list_foreach (list, (GFunc) CORBA_free, NULL);
	g_list_free (list);
}

/**
 * gnome_vfs_mime_application_new_from_id:
 * @id: A const char * containing an application id
 * 
 * Fetches the GnomeVFSMimeApplication associated with the specified
 * application ID from the MIME database.
 *
 * Return value: GnomeVFSMimeApplication * corresponding to @id
 **/
GnomeVFSMimeApplication *
gnome_vfs_mime_application_new_from_id (const char *id)
{
	/* FIXME: deprecate this */
	return gnome_vfs_application_registry_get_mime_application (id);
}

/* CF: something similar but taking a single app_id instead of a GList 
 * might be more useful
 */
/**
 * gnome_vfs_mime_extend_all_applications:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @application_ids: a GList of const char * containing application ids
 * 
 * Register @mime_type as being handled by all applications list in 
 * @application_ids.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or 
 * reporting any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_extend_all_applications (const char *mime_type,
					GList *application_ids)
{
	GList *it;
	struct MimeType *type;

	g_return_val_if_fail (mime_type != NULL, GNOME_VFS_ERROR_INTERNAL);

	type = find_mime_type (mime_type); 
	if (type == NULL) {
		return GNOME_VFS_ERROR_INTERNAL;
	}

	for (it = application_ids; it != NULL; it = it->next) {
		struct MimeHelper *helper;	       
		const char *application_id = it->data;

		helper = g_new (struct MimeHelper, 1);
		if (helper == NULL) {
			return GNOME_VFS_ERROR_INTERNAL;
		}
		helper->app_id = g_strdup (application_id);
		helper->relevance = 50;
		helper->state = USER_ADDED;
		add_helper_to_mime_type (type, helper);
		if (type->state == DEFAULT) {
			type->state = USER_MODIFIED;
		}
	}

	return GNOME_VFS_OK;
}

/**
 * gnome_vfs_mime_remove_from_all_applications:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @application_ids: a GList of const char * containing application ids
 * 
 * Remove @mime_type as a handled type from every application in 
 * @application_ids
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation 
 * or reporting any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_remove_from_all_applications (const char *mime_type,
					     GList *application_ids)
{
	GList *li;
	struct MimeType *type;

	g_return_val_if_fail (mime_type != NULL, GNOME_VFS_ERROR_INTERNAL);

	type = find_mime_type (mime_type); 
	if (type == NULL) {
		return GNOME_VFS_ERROR_INTERNAL;
	}

	for (li = application_ids; li != NULL; li = li->next) {
		const char *application_id = li->data;
		remove_helper_from_mime_type (type, application_id);
	}

	return GNOME_VFS_OK;
}

/**
 * gnome_vfs_mime_can_be_executable:
 * @mime_type: A const char * containing a mime type
 * 
 * Check whether files of this MIME type might conceivably be executable.
 * Default for known types if FALSE. Default for unknown types is TRUE.
 * 
 * Return value: gboolean containing TRUE if some files of this MIME type
 * are registered as being executable, and false otherwise.
 **/
gboolean
gnome_vfs_mime_can_be_executable (const char *mime_type)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return FALSE;
}

/**
 * gnome_vfs_mime_set_can_be_executable:
 * @mime_type: A const char * containing a mime type
 * @new_value: A boolean value indicating whether @mime_type could be executable.
 * 
 * Set whether files of this MIME type might conceivably be executable.
 * 
 * Return value: GnomeVFSResult indicating the success of the operation or any
 * errors that may have occurred.
 **/
GnomeVFSResult
gnome_vfs_mime_set_can_be_executable (const char *mime_type, gboolean new_value)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_get_icon:
 * @mime_type: A const char * containing a  MIME type
 *
 * Query the MIME database for an icon representing the specified MIME type.
 *
 * Return value: The filename of the icon as listed in the MIME database. This is
 * usually a filename without path information, e.g. "i-chardev.png", and sometimes
 * does not have an extension, e.g. "i-regular" if the icon is supposed to be image
 * type agnostic between icon themes. Icons are generic, and not theme specific. These
 * will not necessarily match with the icons a user sees in Nautilus, you have been warned.
 */
const char *
gnome_vfs_mime_get_icon (const char *mime_type)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return NULL;
}

/**
 * gnome_vfs_mime_set_icon:
 * @mime_type: A const char * containing a  MIME type
 * @filename: a const char * containing an image filename
 *
 * Set the icon entry for a particular MIME type in the MIME database. Note that
 * icon entries need not necessarily contain the full path, and do not necessarily need to
 * specify an extension. So "i-regular", "my-special-icon.png", and "some-icon"
 * are all valid icon filenames.
 *
 * Return value: A GnomeVFSResult indicating the success of the operation
 * or any errors that may have occurred.
 */
GnomeVFSResult
gnome_vfs_mime_set_icon (const char *mime_type, const char *filename)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_get_default_component:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * 
 * Query the MIME database for the default Bonobo component to be activated to 
 * view files of MIME type @mime_type.
 * 
 * Return value: An Bonobo_ServerInfo * representing the OAF server to be activated
 * to get a reference to the proper component.
 **/
Bonobo_ServerInfo *
gnome_vfs_mime_get_default_component (const char *mime_type)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return NULL;
}

/**
 * gnome_vfs_mime_get_short_list_components:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * 
 * Return an unsorted sorted list of Bonobo_ServerInfo *
 * data structures for the requested mime type.	The short list contains
 * "select" components recommended for handling this MIME type, appropriate for
 * display to the user.
 * 
 * Return value: A GList * where the elements are Bonobo_ServerInfo *
 * representing various components to display in the short list for @mime_type.
 **/ 
GList *
gnome_vfs_mime_get_short_list_components (const char *mime_type)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return NULL;
}

/**
 * gnome_vfs_mime_get_all_components:
 * @mime_type: A const char * containing a mime type, e.g. "image/png"
 * 
 * Return an alphabetically sorted list of Bonobo_ServerInfo
 * data structures representing all Bonobo components registered
 * to handle files of MIME type @mime_type (and supertypes).
 * 
 * Return value: A GList * where the elements are Bonobo_ServerInfo *
 * representing components that can handle MIME type @mime_type.
 **/ 
GList *
gnome_vfs_mime_get_all_components (const char *mime_type)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return NULL;
}

/**
 * gnome_vfs_mime_set_default_component:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @component_iid: The OAFIID of a component
 * 
 * Sets the default component to be activated for files of MIME type @mime_type.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_set_default_component (const char *mime_type,
				      const char *component_iid)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_set_short_list_applications:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @application_ids: GList of const char * application ids
 * 
 * Set the short list of applications for the specified MIME type. The short list
 * contains applications recommended for possible selection by the user.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_set_short_list_applications (const char *mime_type,
					    GList *application_ids)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_set_short_list_components:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @component_iids: GList of const char * OAF IIDs
 * 
 * Set the short list of components for the specified MIME type. The short list
 * contains companents recommended for possible selection by the user. * 
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_set_short_list_components (const char *mime_type,
					  GList *component_iids)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_add_application_to_short_list:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @application_id: const char * containing the application's id in the MIME database
 * 
 * Add an application to the short list for MIME type @mime_type. The short list contains
 * applications recommended for display as choices to the user for a particular MIME type.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_add_application_to_short_list (const char *mime_type,
					      const char *application_id)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_remove_application_from_short_list:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @application_id: const char * containing the application's id in the MIME database
 * 
 * Remove an application from the short list for MIME type @mime_type. The short list contains
 * applications recommended for display as choices to the user for a particular MIME type.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_remove_application_from_short_list (const char *mime_type,
						   const char *application_id)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_add_component_to_short_list:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @iid: const char * containing the component's OAF IID
 * 
 * Add a component to the short list for MIME type @mime_type. The short list contains
 * components recommended for display as choices to the user for a particular MIME type.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_add_component_to_short_list (const char *mime_type,
					    const char *iid)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_remove_component_from_list:
 * @components: A GList * whose nodes are Bonobo_ServerInfos, such as the
 * result of gnome_vfs_mime_get_short_list_components().
 * @iid: The iid of a component to remove from @components.
 * @did_remove: If non-NULL, this is filled in with TRUE if the component
 * was found in the list, FALSE otherwise.
 * 
 * Remove a component specified by iid from a list of Bonobo_ServerInfos.
 * 
 * Return value: The modified list. If the component is not found, the list will 
 * be unchanged.
 */
GList *
gnome_vfs_mime_remove_component_from_list (GList *components, 
					   const char *iid,
					   gboolean *did_remove)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return NULL;
}

/**
 * gnome_vfs_mime_remove_component_from_short_list:
 * @mime_type: A const char * containing a mime type, e.g. "application/x-php"
 * @iid: const char * containing the component's OAF IID
 * 
 * Remove a component from the short list for MIME type @mime_type. The short list contains
 * components recommended for display as choices to the user for a particular MIME type.
 * 
 * Return value: A GnomeVFSResult indicating the success of the operation or reporting 
 * any errors encountered.
 **/
GnomeVFSResult
gnome_vfs_mime_remove_component_from_short_list (const char *mime_type,
						 const char *iid)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_add_extension:
 * @extension: The extension to add (e.g. "txt")
 * @mime_type: The mime type to add the mapping to.
 * 
 * Add a file extension to the specificed MIME type in the MIME database.
 * 
 * Return value: GnomeVFSResult indicating the success of the operation or any
 * errors that may have occurred.
 **/
GnomeVFSResult
gnome_vfs_mime_add_extension (const char *mime_type, const char *extension)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}

/**
 * gnome_vfs_mime_remove_extension:
 * @extension: The extension to remove
 * @mime_type: The mime type to remove the extension from
 * 
 * Removes a file extension from the specificied MIME type in the MIME database.
 * 
 * Return value: GnomeVFSResult indicating the success of the operation or any
 * errors that may have occurred.
 **/
GnomeVFSResult
gnome_vfs_mime_remove_extension (const char *mime_type, const char *extension)
{
	g_warning ("%s not implemented\n", G_GNUC_FUNCTION);
	return GNOME_VFS_ERROR_INTERNAL;
}
