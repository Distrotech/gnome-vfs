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

#define USER_RELATIVE_PATH ".gnome2/mime-info"

#define MIME_SPEC_NAMESPACE (const xmlChar *)"http://www.freedesktop.org/standards/shared-mime-info"
#define GNOME_VFS_NAMESPACE (const xmlChar *)"http://www.gnome.org/gnome-vfs/mime/1.0"


static struct MimeType *read_mime_file (const gchar *filename);
static GList *get_mime_type_files (const gchar *mime_type);


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

	/* mime_type isn't in our cache (yet?), try to load some info about
	 * it.
	 */
	if (type == NULL) {
		GList *mime_files;
		GList *it;

		mime_files = get_mime_type_files (mime_type);
		if (mime_files == NULL) {
			return NULL;
		}
		for (it = mime_files; it != NULL; it = it->next) {
			type = read_mime_file (it->data);
			/* FIXME: needs to define a sensible policy for the 
			 * reading order of the files, and whether we stop
			 * as soon as we get some info for mime type
			 */
			if (type != NULL) {
				break;
			}
		}
		g_list_foreach (mime_files, (GFunc)g_free, NULL);
		g_list_free (mime_files);
	}

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


static struct MimeType *
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
	/* FIXME: that's probably no longer needed since we only load
	 * mime type info when it's not available
	 */
	/*	mime_type = find_mime_type (type);
		if (mime_type == NULL) {*/
	mime_type = g_new0 (struct MimeType, 1);
	if (mime_type == NULL) {
		fprintf (stderr, "out of memory");
		xmlFreeDoc (doc);
		return;
	}
	mime_type->type = type;
	add_mime_type (mime_type);
	/*	} else {
		g_free (type);
		}*/

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
	return mime_type;
}

/* Parses a MEDIA/SUBTYPE.xml file and stores the corresponding info in 
 * the mime_types hash table
 */
static struct MimeType *
read_mime_file (const gchar *filename)
{
	xmlDocPtr doc;
	xmlNsPtr freedesktop_ns;
	xmlNodePtr cur;
	struct MimeType *result = NULL;

	/*
	 * build an XML tree from a the file;
	 */
	doc = xmlParseFile (filename);
	if (doc == NULL) return NULL;

	/*
	 * Check the document is of the right kind
	 */
	cur = xmlDocGetRootElement (doc);
	if (cur == NULL) {
		fprintf (stderr,"empty document\n");
		xmlFreeDoc (doc);
		return NULL;
	}

	freedesktop_ns = xmlSearchNsByHref (doc, cur, MIME_SPEC_NAMESPACE);
	if (freedesktop_ns == NULL) {
		fprintf (stderr,
			 "document of the wrong type, Shared Mime Info Namespace not found\n");
		xmlFreeDoc (doc);
		return NULL;
	}

	if (xmlStrcmp(cur->name, (const xmlChar *) "mime-type") == 0) {
		result = process_mime_type_node (doc, cur);	 
	} else {
		fprintf(stderr,"document %s of the wrong type, root node != mime-type\n", filename);
		xmlFreeDoc (doc);
		return NULL;
	}

	xmlFreeDoc (doc);
	return result;
}


/* Checks sanity of mime_type, and convert it to the filename to use
 * to access it (ie media/subtype.xml).
 */
static gchar *
file_name_from_mime_type (const gchar *mime_type)
{
	gchar* mime_path = g_strdup (mime_type);
	gchar *result;
	gchar* it = mime_path;
	gint nb_slashes = 0;

	/* Ensure sanity of the mime type. One thing which is not dealt with
	 * is the position of the dot (it can't be at the beginning or at the
	 * end of mime_type), that's probably not really important 
	 */
	while (it && *it != 0) {
		if (*it == '/') {
			nb_slashes++;
		} else if (g_ascii_isalpha (*it)) {
			*it = g_ascii_tolower (*it);
		} else {
			goto error;
		}		
		it++;
	}
	if (nb_slashes != 1) {
		goto error;
	}

	result = g_strconcat (mime_path, ".xml", NULL);
	g_free (mime_path);

	return result;

 error:
	g_warning ("%s: %s is a malformed mime-type\n", 
		   G_GNUC_FUNCTION, mime_type);
	g_free (mime_path);
	return NULL;
}

/* Generates the list of files where to read the info for mime_type from 
 * Please note that generating this list and then using it to read from the
 * files it refers to is inherently racy, some files may appear/disappear 
 * after the g_file_test from this function has been run...
 */
static GList *
get_mime_type_files (const gchar *mime_type)
{
	/* FIXME: this directory list must use the XDG directory spec */
	gchar *system_dirs[] = { "/usr/share/mime/", 
				 "/usr/local/share/mime/",
				 NULL };

	gchar *rel_path = file_name_from_mime_type (mime_type);
	gchar *full_path;
	gchar **it = system_dirs;
	GList *result = NULL;

	if (rel_path == NULL) {
		g_free (rel_path);
		return NULL;
	}
	
	while ((it != NULL) && (*it != NULL)) {
		full_path = g_strconcat (*it, rel_path, NULL);
		if (g_file_test (full_path, G_FILE_TEST_EXISTS)) {
			result = g_list_append (result, full_path);
		} else {
			g_free (full_path);
		}
		it++;
	}
	
	full_path = g_strjoin ("/", g_get_home_dir (), ".mime",
			       rel_path, NULL);
	if (g_file_test (full_path, G_FILE_TEST_EXISTS)) {
		result = g_list_prepend (result, full_path);
	} else {
		g_free (full_path);
	}

	full_path = g_strjoin ("/", g_get_home_dir (), USER_RELATIVE_PATH, 
			       rel_path, NULL);
	if (g_file_test (full_path, G_FILE_TEST_EXISTS)) {
		result = g_list_prepend (result, full_path);
	} else {
		g_free (full_path);
	}
	
	return result;
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


static gchar *get_user_filename (const gchar *mime_type)
{
	gchar *filename;
	gchar *fullpath;
	gchar *dirname;
	gchar *slash_pos;

	filename = file_name_from_mime_type (mime_type);
	
	if (filename == NULL) {
		return NULL;
	}
	
	fullpath = g_strjoin ("/", g_get_home_dir (),
			      USER_RELATIVE_PATH, 
			      filename, NULL);
	g_free (filename);
	dirname = g_strdup (fullpath);
	slash_pos = strrchr (dirname, '/');
	if (slash_pos == NULL) {
		/* This should never happens ince file_name_from_mime_type
		 * guarantee that filename contains a / 
		 */
		g_error ("Couldn't find '/' in %s\n", fullpath);
		return NULL;
	}
	*slash_pos = '\0';

	if (g_file_test (dirname, G_FILE_TEST_EXISTS)) {
		if (g_file_test (dirname, G_FILE_TEST_IS_DIR)) {
			g_free (dirname);		
			return fullpath;
		} else {
			/* FIXME: should we try to remove the existing 
			 * thing before failing ?
			 */
			g_free (dirname);
			g_free (fullpath);
			return NULL;
		}
	}

	/* ~/USER_RELATIVE_PATH/media doesn't exist, create it */
	if (mkdir (dirname, S_IRWXU) != 0) {
		g_warning ("Couldn't create %s dir.", dirname);
		g_free (dirname);
		g_free (fullpath);
		return NULL;
	} else {
		g_free (dirname);
		return fullpath;
	}

	/* Should never be reached */
	g_free (dirname);
	g_free (fullpath);
	
	return NULL;
}

static void
mime_type_save (gchar *key, struct MimeType *mime_type)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNsPtr mime_ns;
	xmlNsPtr gnome_ns;
	GList *it;
	gchar *filename;

	g_assert (mime_type != NULL);
	g_assert (key == mime_type->type);


	if (mime_type->state == DEFAULT) {
		return;
	}

	filename = get_user_filename (mime_type->type);
	if (filename == NULL) {
		return;
	}
		
	xmlKeepBlanksDefault (0);
	
	doc = xmlNewDoc ("1.0");
	root = xmlNewDocNode (doc, NULL, "mime-type", "");
	mime_ns = xmlNewNs (root, MIME_SPEC_NAMESPACE, NULL);
	gnome_ns = xmlNewNs (root, GNOME_VFS_NAMESPACE, "gnome");
	xmlDocSetRootElement (doc, root);

	xmlNewProp (root, "type", mime_type->type);
	xmlNewNsProp (root, gnome_ns, "state", 
		      modification_string[mime_type->state]);

	if (mime_type->state == USER_REMOVED) {
		xmlSaveFormatFile (filename, 
				   doc, 1);
		g_free (filename);
		xmlFreeDoc (doc);
		return;
	}

	/* FIXME:
	 * Currently, if a user modifies a mime type (without changing
	 * the description) and the switch locale, he will get the description
	 * in the locale used when he modified the mime type
	 */
	if (mime_type->desc != NULL) {
		xmlNewTextChild (root, NULL, "comment", mime_type->desc);
	}
	/* It's pointless to save that since there is no API to change it...
	 * Moreover, it adds problems when the user changes locale after
	 * persisting some mime changes to disk...
	 */
	if (mime_type->category) {
		xmlNewTextChild (root, gnome_ns, "category", 
				 mime_type->category);
	}
	if (mime_type->default_action == GNOME_VFS_MIME_ACTION_TYPE_COMPONENT){
		xmlNewTextChild (root, gnome_ns, "default_action_type",
				 "component");
	} else {
		xmlNewTextChild (root, gnome_ns, "default_action_type", 
				 "application");
	}
	
	for (it = mime_type->helpers; it != NULL; it = it->next) {
		struct MimeHelper *helper;
		xmlNodePtr h_node;

		helper = (struct MimeHelper *)it->data;
		if (helper->state != DEFAULT) {
			gchar *relevance;
			h_node = xmlNewChild (root, gnome_ns, 
					      "default-application", 
					      NULL);
			xmlNewNsProp (h_node, gnome_ns, "state", 
				      modification_string[mime_type->state]);

			if (mime_type->state == USER_REMOVED) {
				continue;
			}
			xmlNewTextChild (h_node, gnome_ns, "app-id", 
					 helper->app_id);
			relevance = g_strdup_printf ("%u", helper->relevance);
			xmlNewTextChild (h_node, gnome_ns, "relevance", 
					 relevance);
			g_free (relevance);
		}
	}

	/* Save user attributes */
	/* FIXME: may need to check that users aren't trying to use a tag used
	 * by gnome-vfs => maybe use a different namespace ? 
	 */
	if (mime_type->user_attributes != NULL) {
		xmlNodePtr user_node;
		user_node = xmlNewChild (root, gnome_ns, "user-attributes", 
					 NULL);
		g_hash_table_foreach (mime_type->user_attributes, 
				      (GHFunc)save_user_attributes, 
				      user_node);
	}

	xmlSaveFormatFile (filename, doc, 1);
	xmlFreeDoc (doc);
	g_free (filename);
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
	g_assert (mime_types != NULL);

	g_hash_table_foreach (mime_types, (GHFunc)mime_type_save, NULL);
}
 
