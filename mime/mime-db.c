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

/* FIXME: get_user_attributes and get_mime_types both returns list of char*.
 * Both should return lists of const char* or char* to have a more coherent
 * "malloc/free policy" in gnome-vfs
 */

#include "mime-db.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <libgnomevfs/gnome-vfs-application-registry.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-private-utils.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include <glib.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


//#define DEBUG(x) g_print x
#define DEBUG(x)

#define MIME_SPEC_NAMESPACE (const xmlChar *)"http://www.freedesktop.org/standards/shared-mime-info"
#define GNOME_VFS_NAMESPACE (const xmlChar *)"http://www.gnome.org/gnome-vfs/mime/1.0"


/* The mime_types hash table contains GLists of MimeMapping */
static GHashTable *mime_types = NULL;

static GList *current_lang = NULL;

/* this gives us a number of the language in the current language list,
   the higher the number the "better" the translation */
static int
language_level (const char *lang)
{
        int i;
        GList *li;

        if (lang == NULL)
                return 0;

        for (i = 1, li = current_lang; li != NULL; i++, li = g_list_next (li)){
                if (strcmp ((const char *) li->data, lang) == 0)
                        return i;
	}
        return -1;
}


/* Convenience functions, will be useful if/when we add locking to accesses
 * to the mime database
 */
struct MimeType *
find_mime_type (const char *mime_type)
{
	struct MimeType *type;

	g_return_val_if_fail (mime_type != NULL, NULL);
	g_assert (mime_types != NULL);

	type = g_hash_table_lookup (mime_types, mime_type);
	if ((type == NULL) || (type->state == USER_REMOVED)) {
		return NULL;
	}

	return type;
}

static void
add_mime_type (struct MimeType *mime_type)
{
	/* FIXME: should we check if the mime type is already present in
	 * the mime database ? 
	 */
	g_hash_table_insert (mime_types, mime_type->type, mime_type);
}

static void
remove_mime_type (const gchar *mime_type) 
{
	/* FIXME: need to free mem somehow */
	g_hash_table_remove (mime_types, mime_type);
}

static int
comp_relevance (gconstpointer a, gconstpointer b)
{
	struct MimeHelper *helper1, *helper2;

	helper1 = (struct MimeHelper *)a;
	helper2 = (struct MimeHelper *)b;

	if (a > b) {
		return 1;
	} else if (a < b) {
		return -1;
	} else {
		return 0;
	}
}


void
g_list_free_deep (GList *list)
{
	if (list == NULL) {
		return;
	}
	g_list_foreach (list, (GFunc) g_free, NULL);
	g_list_free (list);
}

void
add_helper_to_mime_type (struct MimeType *mime_type, struct MimeHelper *helper)
{
	g_assert (mime_type != NULL);
	g_assert (mime_type->state != USER_REMOVED);
	g_assert (helper != NULL);
	g_assert (helper->app_id != NULL);

	/* FIXME: should check if the helper is already present */
	mime_type->helpers = g_list_insert_sorted (mime_type->helpers, helper, 
						   comp_relevance);
}

void
remove_helper_from_mime_type (struct MimeType *mime_type, const gchar *app_id)
{
	GList *it;

	g_assert (mime_type != NULL);
	g_assert (mime_type->state != USER_REMOVED);
	g_assert (app_id != NULL);

	if (mime_type->state == DEFAULT) {
		mime_type->state = USER_MODIFIED;
	}

	for (it = mime_type->helpers; it != NULL; it = it->next) {
		struct MimeHelper *helper;

		helper = (struct MimeHelper *)it->data;
		if (strcmp (helper->app_id, app_id) == 0) {
			helper->state = USER_REMOVED;
			/* FIXME: can break out of the loop here if duplicate 
			 * app_ids aren't added to the helper list 
			 */
		}
	}
}


/* Parses a <default-application> tag in gnome-vfs's namespace */
static struct MimeHelper *
parse_default_app_node (xmlDocPtr doc, xmlNodePtr cur)
{
	struct MimeHelper *helper;

	helper = g_new0 (struct MimeHelper, 1);
	if (helper == NULL) {
		return NULL;
	}

	while (cur != NULL) {
		if (!xmlStrcmp (cur->name, (const xmlChar *)"relevance")) {
			gchar *relevance;
			
			relevance = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
			helper->relevance = atoi (relevance);
			g_free (relevance);
		} else if (!xmlStrcmp (cur->name, (const xmlChar *)"app-id")) {
			if (helper->app_id) {
				g_free (helper->app_id);
			}
			helper->app_id = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
		}
		cur = cur->next;
	}

	return helper;
}

/* Parses nodes in the gnome-vfs's namespace */
static void
process_gnomevfs_node (xmlDocPtr doc, xmlNodePtr cur, 
			struct MimeType *mime_type)
{
	gint cur_i18n_level = -1;

	if (!xmlStrcmp (cur->name, (const xmlChar *)"category")) {
		gint i18n_level;
		gchar *cat_lang;

		cat_lang = xmlNodeGetLang (cur);
		i18n_level = language_level (cat_lang);
		if (i18n_level > cur_i18n_level) {
			xmlFree (mime_type->category);
			mime_type->category = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
			cur_i18n_level = i18n_level;
		}
		xmlFree (cat_lang);
		
	} else if (!xmlStrcmp (cur->name, (const xmlChar *)"default-application")) {
		struct MimeHelper *helper;
		helper = parse_default_app_node (doc, cur->xmlChildrenNode);
		if (helper != NULL) {
			add_helper_to_mime_type (mime_type, helper);
		}
	} else if (!xmlStrcmp (cur->name, (const xmlChar *)"user-attributes")){
		xmlNodePtr n = cur->xmlChildrenNode;
		if (mime_type->user_attributes == NULL) {
			mime_type->user_attributes =
				g_hash_table_new_full (g_str_hash, g_str_equal,
						       (GDestroyNotify)g_free, 
						       (GDestroyNotify)g_free);
		}
		while (n != NULL) {
			g_hash_table_insert (mime_type->user_attributes, 
					     g_strdup(n->name), xmlNodeListGetString (doc, n->xmlChildrenNode, 1));
			n = n->next;
		}
	}
}


static void 
process_mime_type_node (xmlDocPtr doc, xmlNodePtr cur)
{
	gchar *type;
	struct MimeType *mime_type;
	xmlNsPtr freedesktop_ns;
	gchar *state;

	if (xmlStrcmp(cur->name, (const xmlChar *) "mime-type")) {
		fprintf(stderr,"document of the wrong type, root node != mime-type\n");
		xmlFreeDoc (doc);
		return;
	}
	
	type = xmlGetProp (cur, "type");
	
	if (type == NULL) {
		fprintf (stderr, "couldn't find type attribute");
		xmlFreeDoc (doc);
		return;
	}

	state = xmlGetNsProp (cur, "state",  GNOME_VFS_NAMESPACE);
	
	if ((state != NULL) && (strcmp (state, "removed") == 0)) {
		remove_mime_type (type);
		return;
	}
	
	mime_type = find_mime_type (type);
	if (mime_type == NULL) {
		mime_type = g_new0 (struct MimeType, 1);
		if (mime_type == NULL) {
			fprintf (stderr, "out of memory");
			xmlFreeDoc (doc);
			return;
		}
		mime_type->type = type;
		add_mime_type (mime_type);
	} else {
		g_free (type);
	}

	cur = cur->xmlChildrenNode;
	
	freedesktop_ns = xmlSearchNsByHref (doc, cur, MIME_SPEC_NAMESPACE);
	while (cur != NULL) {
		gint cur_i18n_level = -1;
		
		if ((cur->ns) 
		    && (strcmp (cur->ns->href, GNOME_VFS_NAMESPACE) == 0)) {
			process_gnomevfs_node (doc, cur, mime_type);
		} else if ((!xmlStrcmp (cur->name, (const xmlChar *)"comment"))
		    && (cur->ns == freedesktop_ns)) {
			gchar *comment_lang;
			gint i18n_level;

			comment_lang = xmlNodeGetLang (cur);
			i18n_level = language_level (comment_lang);
			if (i18n_level > cur_i18n_level) {
				xmlFree (mime_type->desc);
				mime_type->desc = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
				cur_i18n_level = i18n_level;
			}
			xmlFree (comment_lang);
		}
		cur = cur->next;
	}
}
/* Parses a MEDIA/SUBTYPE.xml file and stores the corresponding info in 
 * the mime_types hash table
 */
static void
parse_mime_desc (const gchar *filename)
{
	xmlDocPtr doc;
	xmlNsPtr freedesktop_ns;
	xmlNodePtr cur;

	/*
	 * build an XML tree from a the file;
	 */
	doc = xmlParseFile (filename);
	if (doc == NULL) return;

	/*
	 * Check the document is of the right kind
	 */
	cur = xmlDocGetRootElement (doc);
	if (cur == NULL) {
		fprintf (stderr,"empty document\n");
		xmlFreeDoc (doc);
		return;
	}

	freedesktop_ns = xmlSearchNsByHref (doc, cur, MIME_SPEC_NAMESPACE);
	if (freedesktop_ns == NULL) {
		fprintf (stderr,
			 "document of the wrong type, Shared Mime Info Namespace not found\n");
		xmlFreeDoc (doc);
		return;
	}

	if (xmlStrcmp(cur->name, (const xmlChar *) "mime-type") == 0) {
		process_mime_type_node (doc, cur);
	} else if (xmlStrcmp(cur->name, (const xmlChar *) "mime-types") == 0) {
		g_print ("%s: mime-types\n", filename);

		cur = cur->xmlChildrenNode;
		while (cur != NULL) {
			process_mime_type_node (doc, cur);
			cur = cur->next;
		}
	} else {
		fprintf(stderr,"document %s of the wrong type, root node != mime-type\n", filename);
		xmlFreeDoc (doc);
		return;
	}

	xmlFreeDoc (doc);
}


/* Returns true if dirname is a directory */
static gboolean 
is_dir (const char *dirname)
{
	struct stat s;

	stat (dirname, &s);
	return S_ISDIR (s.st_mode);
}

static gboolean
file_exists (const char *filename)
{
	struct stat s;

	stat (filename, &s);
	return S_ISREG (s.st_mode);
}

/* Find all .xml files in a dir, parses them and add the mime types they 
 * describe to the database
 */
static void 
read_mime_info_from_dir (const char *dirname)
{
	DIR *dir;
	struct dirent *dent;
	const int extlen = sizeof (".xml") - 1;

	dir = opendir (dirname);
	while ((dent = readdir (dir)) != NULL){
		gchar *filename;
		int len = strlen (dent->d_name);

		if (len <= extlen)
			continue;
		if (strcmp (dent->d_name + len - extlen, ".xml")) {
			continue;
		}
		filename = g_strconcat (dirname, "/", dent->d_name, 
						NULL);
		parse_mime_desc (filename);
		g_free (filename);
	}
	closedir (dir);
}

/* Load the whole mime database */
static void 
load_mime_database (void)
{
	GList *mime_paths = NULL;
	GList *li;
	gchar *user_dir = g_strconcat (g_get_home_dir (), "/.mime", NULL);
	gchar *user_custom = g_strconcat (g_get_home_dir (), 
					  "/.gnome2/mime-info/user.xml", NULL);
	DIR *dir;
	struct dirent *dent;

	if (is_dir ("/usr/share/mime")) {
		mime_paths = 
			g_list_append (mime_paths, g_strdup("/usr/share/mime"));
	}

	if (is_dir ("/usr/local/share/mime")) {
		mime_paths = 
			g_list_append (mime_paths, g_strdup("/usr/local/share/mime"));
	}

	if (is_dir (user_dir)) {		
		mime_paths = 
			g_list_append (mime_paths, user_dir);
	}

	/* Maybe we should also check something like $GNOME2_PATH/share/mime 
	   and we probably shoud also consider the prefix where gnome-vfs is
	   installed
	*/

	for (li = mime_paths; li != NULL; li = li->next) {
		gchar *dirname = (gchar*)li->data;
		
		dir = opendir (dirname);
		if (dir == NULL) {
			continue;
		}
		while ((dent = readdir (dir)) != NULL){
			gchar *subdir;

			if (!strcmp (dent->d_name, "packages")) {
				continue;
			}
			if (!strcmp (dent->d_name, ".")) {
				continue;
			}
			if (!strcmp (dent->d_name, "..")) {
				continue;
			}

			subdir = g_strconcat (dirname, "/", dent->d_name, 
					      NULL);
			if (is_dir (subdir)) {
				read_mime_info_from_dir (subdir);
			}
			g_free (subdir);
		}
		closedir (dir);
	}

	if (file_exists (user_custom)) {
		g_print ("parsing user custom");
		parse_mime_desc (user_custom);
	}

	g_free (user_custom);
	g_list_foreach (mime_paths, (GFunc)g_free, NULL);
}


static void 
mime_type_print (struct MimeType *mime_type)
{
	//	g_print ("%s: %s\n", mime_type->type, mime_type->desc);
	g_print ("%s\n", mime_type->type);
}


static void 
print_hash_entry (gpointer key, gpointer value, gpointer user_data)
{
	/*	GList *it;*/
	struct MimeType *mime_type = (struct MimeType *)value;

	if (value == NULL) {
		return;
	}

	mime_type_print (mime_type);
	/*	for (it = mime_type->helpers; it != NULL; it=it->next) {
		struct MimeHelper *helper = (struct MimeHelper *)(it->data);
		g_print ("\t%s:%u\n", helper->app_id, helper->relevance);
		}*/
	
	//	g_print ("\n");
}

void
init_mime_db (void)
{
	current_lang = gnome_vfs_i18n_get_language_list ("LC_ALL");
	mime_types = g_hash_table_new (g_str_hash, g_str_equal);
	load_mime_database ();
	//	g_print ("nb of elems: %u\n", g_hash_table_size (mime_types));
}


static void
add_type_to_list (gchar *key, struct MimeType *mime_type, GList **list)
{
	GList *dup = NULL;

	if ((mime_type == NULL) || (mime_type->state == USER_REMOVED)) {
		return;
	}

	g_assert (key == mime_type->type);

	dup = g_list_find_custom ((*list), key, (GCompareFunc)strcmp);
	if (dup == NULL) {
		(*list) = g_list_insert_sorted ((*list), 
						g_strdup(key), 
						(GCompareFunc)strcmp);
	}
}

/*
 * gnome_vfs_get_registered_mime_types
 *
 *  Return the list containing the name of all
 *  registered mime types.
 *  This function is costly in terms of speed.
 * 
 *  The returned list and the strings it contains
 *  should be freed.
 */
GList *
gnome_vfs_get_mime_types (void)
{
	GList *result = NULL;
	g_hash_table_foreach (mime_types, (GHFunc)add_type_to_list, &result);
	return result;
}



/* FIXME: in the following 3 functions, there should be a way for each app
 * to have its own "namespace"
 */

/**
 * gnome_vfs_mime_set_user_attribute:
 * @mime_type: a mime type.
 * @key: a key to store the value in.
 * @value: the value to store in the key.
 *
 * This function is going to set the value
 * associated to the key and it will save it
 * to the user's file if necessary.
 *
 * This can be used by any app which wishes to associate
 * arbitraty information with mime-types.
 *
 * Returns: GNOME_VFS_OK if the operation succeeded, otherwise an error code
 *
 */
GnomeVFSResult
gnome_vfs_mime_set_user_attribute (const char *mime_type, const char *key, 
				   const char *value)
{
	struct MimeType *type;

	g_assert (mime_type != NULL);
	g_assert (key != NULL);	

	//	DEBUG (("%s: %s %s %s\n", G_GNUC_FUNCTION, mime_type, key, value));

	type = find_mime_type (mime_type); 

	if (type == NULL) {
		g_print ("couldn't find mime type\n");
		return GNOME_VFS_ERROR_INTERNAL;
	}

	if (type->user_attributes == NULL) {
		type->user_attributes = 
			g_hash_table_new_full (g_str_hash, g_str_equal,
					       (GDestroyNotify)g_free, 
					       (GDestroyNotify)g_free);
	}

	g_hash_table_insert (type->user_attributes, 
			     g_strdup (key), g_strdup (value));
	
	if (type->state == DEFAULT) {
		type->state = USER_MODIFIED;
	}

	return GNOME_VFS_OK;
}

/**
 * gnome_vfs_mime_set_registered_type_key:
 * @mime_type: 	Mime type to set key for
 * @key: 	The key to set
 * @data: 	The data to set for the key
 *
 * This function sets the key data for the registered mime
 * type's hash table.
 *
 * Returns: GNOME_VFS_OK if the operation succeeded, otherwise an error code
 */
GnomeVFSResult
gnome_vfs_mime_set_registered_type_key (const char *mime_type, 
					const char *key, 
					const char *value)
{
	return gnome_vfs_mime_set_user_attribute (mime_type, key, value);
}


/**
 * gnome_vfs_mime_get_user_attribute:
 * @mime_type: a mime type.
 * @key: A key to lookup for the given mime-type
 *
 * This function retrieves the value associated with @key for
 * the given mime-type.  The string is private, you
 * should not free the result.
 *
 * This can be used by any app which wishes to associate
 * arbitraty information with mime-types.
 *
 * Returns: GNOME_VFS_OK if the operation succeeded, otherwise an error code
 */
const char *
gnome_vfs_mime_get_user_attribute (const char *mime_type, const char *key)
{
	struct MimeType *type;

	g_assert (mime_type != NULL);
	g_assert (key != NULL);

	DEBUG (("%s: %s %s\n", G_GNUC_FUNCTION, mime_type, key));
	type = find_mime_type (mime_type); 
	
	if ((type == NULL) || (type->user_attributes == NULL)) {
		return NULL;
	}

	return g_hash_table_lookup (type->user_attributes, key);
}

/**
 * gnome_vfs_mime_remove_user_attribute:
 * @mime_type: a mime_type.
 * @key: A key to remove for the given mime-type
 *
 * This can be used by any app which wishes to associate
 * arbitraty information with mime-types.
 *
 * This function removes the value associated with @key for the mime_type 
 * parameter. 
 */
void
gnome_vfs_mime_remove_user_attribute (const char *mime_type, const char *key)
{
	struct MimeType *type;

	g_assert (mime_type != NULL);
	g_assert (key != NULL);

	DEBUG (("%s: %s %s\n", G_GNUC_FUNCTION, mime_type, key));
	type = find_mime_type (mime_type); 
	
	if ((type == NULL) || (type->user_attributes == NULL)) {
		return;
	}

	g_hash_table_remove (type->user_attributes, key);
}

static void
add_attribute_to_list (gchar *key, gchar *value, GList **list)
{
	GList *dup = NULL;

	dup = g_list_find_custom ((*list), key, (GCompareFunc)strcmp);
	if (dup == NULL) {
		(*list) = g_list_insert_sorted ((*list), key, 
						(GCompareFunc)strcmp);
	}
}


/**
 * gnome_vfs_mime_get_user_attributes:
 * @mime_type: the MIME type to lookup
 *
 * Gets a list of all attribute names associated with @mime_type.
 * An attribute is a name/value pair added to a mime-type by a 
 * third party app.
 *
 * Return value: a GList of const char * representing keys associated
 * with @mime_type
 **/
GList *
gnome_vfs_get_user_attributes (const char *mime_type)
{
	struct MimeType *type;
	GList *list = NULL;

	g_assert (mime_type != NULL);

	DEBUG (("%s: %s\n", G_GNUC_FUNCTION, mime_type));
	type = find_mime_type (mime_type); 
	
	if ((type == NULL) || (type->user_attributes == NULL)) {
		return NULL;
	}
	g_hash_table_foreach (type->user_attributes, 
			      (GHFunc)add_attribute_to_list,
			      &list);
}


/**
 * gnome_vfs_mime_keys_list_free:
 * @mime_type_list: A mime type list to free.
 *
 * Frees the mime type list.
 */
void
gnome_vfs_mime_keys_list_free (GList *mime_type_list)
{
	/* we do not need to free the data in the list since
	   the data was stolen from the internal hash table
	   This function is there so that people do not need
	   to know this particular implementation detail.
	   => many functions in gnome-vfs return a list whose members
	   must or must not be freed manually by the lib user, this 
	   function is now deprecated because of that
	*/
	g_list_free (mime_type_list);
}

/**
 * gnome_vfs_mime_registered_mime_type_list_free:
 * @list: the extensions list
 *
 * Call this function on the list returned by 
 * gnome_vfs_get_registered_mime_types (aka gnome_vfs_get_mime_types)
 * to free the list and all of its elements.
 */
void
gnome_vfs_mime_registered_mime_type_list_free (GList *list)
{
	g_list_free_deep (list);
}


/**
 * gnome_vfs_mime_type_is_known:
 * @mime_type: a mime type.
 *
 * This function returns TRUE if @mime_type is in the MIME database at all.
 *
 * Returns: TRUE if anything is known about @mime_type, otherwise FALSE
 */
gboolean
gnome_vfs_mime_type_is_known (const char *mime_type)
{
	g_assert (mime_type != NULL);

	DEBUG (("%s: %s\n", G_GNUC_FUNCTION, mime_type));

	return (find_mime_type (mime_type) != NULL);
}


static void
mime_type_free (gchar *key, struct MimeType *type, gpointer data)
{
	g_assert (type != NULL);
	g_assert (key != NULL);

	g_free (type->type);
	g_free (type->desc);
	g_free (type->category);
	if (type->user_attributes != NULL) {
		g_hash_table_destroy (type->user_attributes);
		type->user_attributes = NULL;
	}
	if (type->helpers != NULL) {
		GList *it;
		for (it = type->helpers; it != NULL; it = it->next) {
			struct MimeHelper *helper;

			helper = (struct MimeHelper *)it->data;
			g_assert (helper != NULL);

			g_free (helper->app_id);
			g_free (helper);
		}
		g_list_free (type->helpers);
	}
	g_free (type);
}

/**
 * gnome_vfs_mime_info_reload:
 *
 * Reload the MIME database from disk and notify any listeners
 * holding active #GnomeVFSMIMEMonitor objects.
 **/
/* FIXME: this has the side effect of forgetting all unsaved user changes */
/* Is that acceptable ? */
/* FIXME: the monitoring part isn't done at all yet, the whole "when should
 * the database be saved ?" question hasn't been dealt with at all yet 
 */
void
gnome_vfs_mime_info_reload (void)
{
	/* 1. Clean */
	if (mime_types != NULL) {
		g_hash_table_foreach (mime_types, (GHFunc)mime_type_free, 
				      NULL);
	}

	/* 2. Reload */
	load_mime_database ();
}


/* The following 3 functions are used to save modified mime types in the
 * user home dir
 */

/* Used to convert a ModificationState to a string when saving to an XML file 
 */
static char *modification_string[] = 
	{ "unchanged", "added", "removed", "modified" };

static void
save_user_attributes (gchar *key, gchar *value, xmlNodePtr node)
{
	xmlNsPtr ns;
	
	ns = xmlSearchNs (node->doc, node, "gnome");
	if (ns == NULL) {
		g_print ("Uh Oh, namespace not found\n");
		return;
	}
	xmlNewTextChild (node, ns, key, value);
}


static void
mime_type_save (gchar *key, struct MimeType *mime_type, xmlNodePtr root)
{
	xmlNodePtr node;
	xmlNodePtr user_node;
	xmlNsPtr ns;
	GList *it;

	if (mime_type->state == DEFAULT) {
		return;
	}

	ns = xmlSearchNs (root->doc, root, "gnome");
	if (ns == NULL) {
		return;
	}

	node = xmlNewChild (root, NULL, "mime-type", NULL);
	xmlNewProp (node, "type", mime_type->type);
	xmlNewNsProp (node, ns, "state", 
		      modification_string[mime_type->state]);

	if (mime_type->state == USER_REMOVED) {
		return;
	}

	/* FIXME:
	 * Currently, if a user modifies a mime type (without changing
	 * the description) and the switch locale, he will get the description
	 * in the locale used when he modified the mime type
	 */
	if (mime_type->desc != NULL) {
		xmlNewTextChild (node, NULL, "comment", mime_type->desc);
	}
	/* It's pointless to save that since there is no API to change it...
	 * Moreover, it adds problems when the user changes locale after
	 * persisting some mime changes to disk...
	 */
	if (mime_type->category) {
		xmlNewTextChild (node, ns, "category", mime_type->category);
	}
	if (mime_type->default_action == GNOME_VFS_MIME_ACTION_TYPE_COMPONENT){
		xmlNewTextChild (node, ns, "default_action_type", "component");
	} else {
		xmlNewTextChild (node, ns, "default_action_type", 
				 "application");
	}
	
	for (it = mime_type->helpers; it != NULL; it = it->next) {
		struct MimeHelper *helper;
		xmlNodePtr h_node;

		helper = (struct MimeHelper *)it->data;
		if (helper->state != DEFAULT) {
			gchar *relevance;
			h_node = xmlNewChild (node, ns, "default-application", 
					      NULL);
			xmlNewNsProp (node, ns, "state", 
				      modification_string[mime_type->state]);

			if (mime_type->state == USER_REMOVED) {
				continue;
			}
			xmlNewTextChild (h_node, ns, "app-id", helper->app_id);
			relevance = g_strdup_printf ("%u", helper->relevance);
			xmlNewTextChild (h_node, ns, "relevance", relevance);
			g_free (relevance);
		}
	}

	/* Save user attributes */
	/* FIXME: may need to check that users aren't trying to use a tag used
	 * by gnome-vfs => maybe use a different namespace ? 
	 */
	if (mime_type->user_attributes != NULL) {
		user_node = xmlNewChild (node, ns, "user-attributes", NULL);
		g_hash_table_foreach (mime_type->user_attributes, 
				      (GHFunc)save_user_attributes, 
				      user_node);
	}
}


/* This will save the user changes to the mime database in an XML file.
 * This file format is the same as the one described in the shared mime
 * spec with its gnome-specific extensions, and an additional "state" 
 * attribute to each mime-type tag to indicate if it was "added", 
 * "removed" or "modified". Similarly, each default-application tag uses
 * a similar tag.
 * All the user changes are persisted in a single file instead of a
 * directory hierarchy as in the spec for performance reason.
 */
void
persist_user_changes (void)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNsPtr mime_ns;
	xmlNsPtr gnome_ns;

	xmlKeepBlanksDefault (0);
	
	doc = xmlNewDoc ("1.0");
	root = xmlNewDocNode (doc, NULL, "mime-types", "");
	mime_ns = xmlNewNs (root, MIME_SPEC_NAMESPACE, NULL);
	gnome_ns = xmlNewNs (root, GNOME_VFS_NAMESPACE, "gnome");
	xmlDocSetRootElement (doc, root);

	g_hash_table_foreach (mime_types, (GHFunc)mime_type_save, 
			      doc->children);

	xmlSaveFormatFile ("/home/teuf/.gnome2/mime-info/user.xml", doc, 1);
}
