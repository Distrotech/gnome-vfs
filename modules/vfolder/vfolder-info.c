/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* 
 * vfolder-info.c - Loading of .vfolder-info files.  External interface 
 *                  defined in vfolder-common.h
 *
 * Copyright (C) 2002 Ximian, Inc.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alex Graveley <alex@ximian.com>
 *         Based on original code by George Lebl <jirka@5z.com>.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-monitor-private.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <sys/time.h>

#include "vfolder-common.h"
#include "vfolder-util.h"

#define DOT_GNOME ".gnome2"

/* .vfolder-info format example:
 * <VFolderInfo>
 *   <!-- Merge dirs optional -->
 *   <MergeDir>/etc/X11/applnk</MergeDir>
 *   <!-- Only specify if it should override standard location -->
 *   <ItemDir>/usr/share/applications</ItemDir>
 *   <!-- This is where the .directories are -->
 *   <DesktopDir>/etc/X11/gnome/vfolders</DesktopDir>
 *   <!-- Root folder -->
 *   <Folder>
 *     <Name>Root</Name>
 *
 *     <Include>important.desktop</Include>
 *
 *     <!-- Other folders -->
 *     <Folder>
 *       <Name>SomeFolder</Name>
 *       <ParentLink>http:///mywebdav.com/homedir</ParentLink>
 *     </Folder>
 *     <Folder>
 *       <Name>Test_Folder</Name>
 *       <Parent>file:///a_readonly_path</Parent>
 *       <!-- could also be absolute -->
 *       <Desktop>Test_Folder.directory</Desktop>
 *       <Query>
 *         <Or>
 *           <And>
 *             <Keyword>Application</Keyword>
 *             <Keyword>Game</Keyword>
 *           </And>
 *           <Keyword>Clock</Keyword>
 *         </Or>
 *       </Query>
 *       <Include>somefile.desktop</Include>
 *       <Include>someotherfile.desktop</Include>
 *       <Exclude>yetanother.desktop</Exclude>
 *     </Folder>
 *   </Folder>
 * </VFolderInfo>
 */


/* 
 * XML VFolder description reading
 */
static Query *
single_query_read (xmlNode *qnode)
{
	Query *query;
	xmlNode *node;

	if (qnode->type != XML_ELEMENT_NODE || qnode->name == NULL)
		return NULL;

	query = NULL;

	if (g_ascii_strcasecmp (qnode->name, "Not") == 0 &&
	    qnode->xmlChildrenNode != NULL) {
		xmlNode *iter;

		for (iter = qnode->xmlChildrenNode;
		     iter != NULL && query == NULL;
		     iter = iter->next)
			query = single_query_read (iter);
		if (query != NULL) {
			query->not = ! query->not;
		}
		return query;
	} 
	else if (g_ascii_strcasecmp (qnode->name, "Keyword") == 0) {
		xmlChar *word = xmlNodeGetContent (qnode);

		if (word != NULL) {
			query = query_new (QUERY_KEYWORD);
			query->val.keyword = g_quark_from_string (word);
			xmlFree (word);
		}
		return query;
	} 
	else if (g_ascii_strcasecmp (qnode->name, "Filename") == 0) {
		xmlChar *file = xmlNodeGetContent (qnode);

		if (file != NULL) {
			query = query_new (QUERY_FILENAME);
			query->val.filename = g_strdup (file);
			xmlFree (file);
		}
		return query;
	} 
	else if (g_ascii_strcasecmp (qnode->name, "ParentQuery") == 0) {
		query = query_new (QUERY_PARENT);
	}
	else if (g_ascii_strcasecmp (qnode->name, "And") == 0) {
		query = query_new (QUERY_AND);
	} 
	else if (g_ascii_strcasecmp (qnode->name, "Or") == 0) {
		query = query_new (QUERY_OR);
	} 
	else {
		/* We don't understand */
		return NULL;
	}

	/* This must be OR or AND */
	g_assert (query != NULL);

	for (node = qnode->xmlChildrenNode; node; node = node->next) {
		Query *new_query = single_query_read (node);

		if (new_query != NULL)
			query->val.queries = 
				g_slist_prepend (query->val.queries, new_query);
	}

	query->val.queries = g_slist_reverse (query->val.queries);

	return query;
}

static void
add_or_set_query (Query **query, Query *new_query)
{
	if (*query == NULL) {
		*query = new_query;
	} else {
		Query *old_query = *query;
		*query = query_new (QUERY_OR);
		(*query)->val.queries = 
			g_slist_append ((*query)->val.queries, old_query);
		(*query)->val.queries = 
			g_slist_append ((*query)->val.queries, new_query);
	}
}

static Query *
query_read (xmlNode *qnode)
{
	Query *query;
	xmlNode *node;

	query = NULL;

	for (node = qnode->xmlChildrenNode; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE ||
		    node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "Not") == 0 &&
		    node->xmlChildrenNode != NULL) {
			xmlNode *iter;
			Query *new_query = NULL;

			for (iter = node->xmlChildrenNode;
			     iter != NULL && new_query == NULL;
			     iter = iter->next)
				new_query = single_query_read (iter);
			if (new_query != NULL) {
				new_query->not = ! new_query->not;
				add_or_set_query (&query, new_query);
			}
		} else {
			Query *new_query = single_query_read (node);
			if (new_query != NULL)
				add_or_set_query (&query, new_query);
		}
	}

	return query;
}

static Folder *
folder_read (VFolderInfo *info, gboolean user_private, xmlNode *fnode)
{
	Folder *folder;
	xmlNode *node;

	folder = folder_new (info, NULL, user_private);

	for (node = fnode->xmlChildrenNode; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE ||
		    node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "Name") == 0) {
			xmlChar *name = xmlNodeGetContent (node);

			if (name) {
				g_free (folder->name);
				folder_set_name (folder, name);
				xmlFree (name);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Parent") == 0) {
			xmlChar *parent = xmlNodeGetContent (node);

			if (parent) {
				gchar *esc_parent;

				esc_parent = vfolder_escape_home (parent);
				folder_set_extend_uri (folder, esc_parent);
				folder->is_link = FALSE;

				xmlFree (parent);
				g_free (esc_parent);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "ParentLink") == 0) {
			xmlChar *parent = xmlNodeGetContent (node);

			if (parent) {
				gchar *esc_parent;

				esc_parent = vfolder_escape_home (parent);
				folder_set_extend_uri (folder, esc_parent);
				folder->is_link = TRUE;
				
				xmlFree (parent);
				g_free (esc_parent);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Desktop") == 0) {
			xmlChar *desktop = xmlNodeGetContent (node);

			if (desktop) {
				folder_set_desktop_file (folder, desktop);
				xmlFree (desktop);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Include") == 0) {
			xmlChar *file = xmlNodeGetContent (node);

			if (file) {
				gchar *esc_file;

				esc_file = vfolder_escape_home (file);
				folder_add_include (folder, esc_file);

				xmlFree (file);
				g_free (esc_file);
			}
		}
		else if (g_ascii_strcasecmp (node->name, "Exclude") == 0) {
			xmlChar *file = xmlNodeGetContent (node);

			if (file) {
				gchar *esc_file;

				esc_file = vfolder_escape_home (file);
				folder_add_exclude (folder, esc_file);

				xmlFree (file);
				g_free (esc_file);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Query") == 0) {
			Query *query;

			query = query_read (node);
			if (query)
				folder_set_query (folder, query);
		} 
		else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *new_folder = folder_read (info, 
							  user_private,
							  node);

			if (new_folder != NULL) {
				folder_add_subfolder (folder, new_folder);
				folder_unref (new_folder);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, 
					     "OnlyUnallocated") == 0) {
			folder->only_unallocated = TRUE;
			info->has_unallocated_folder = TRUE;
		} 
		else if (g_ascii_strcasecmp (node->name, "ReadOnly") == 0) {
			folder->read_only = TRUE;
		} 
		else if (g_ascii_strcasecmp (node->name,
					     "DontShowIfEmpty") == 0) {
			folder->dont_show_if_empty = TRUE;
		}
	}

	/* Name is required */
	if (!folder_get_name (folder)) {
		folder_unref (folder);
		return NULL;
	}

	return folder;
}

static void itemdir_monitor_cb (GnomeVFSMonitorHandle    *handle,
				const gchar              *monitor_uri,
				const gchar              *info_uri,
				GnomeVFSMonitorEventType  event_type,
				gpointer                  user_data);

typedef struct {
	VFolderInfo    *info;
	gint            weight;
	gchar          *uri;
	GSList         *monitors;
	gboolean        is_mergedir;
	gboolean        untimestamp_files;
} ItemDir;

static ItemDir *
itemdir_new (VFolderInfo *info, 
	     const gchar *uri, 
	     gboolean     is_mergedir,
	     gboolean     untimestamp_files,
	     gint         weight)
{
	ItemDir *ret;

	ret = g_new0 (ItemDir, 1);
	ret->info              = info;
	ret->weight            = weight;
	ret->uri               = vfolder_escape_home (uri);
	ret->is_mergedir       = is_mergedir;
	ret->untimestamp_files = untimestamp_files;

	if (untimestamp_files)
		/* WriteDirs go to the head of the class */
		info->item_dirs = g_slist_prepend (info->item_dirs, ret);
	else 
		/* Otherwise, order does matter */
		info->item_dirs = g_slist_append (info->item_dirs, ret);

	return ret;
}

static void
itemdir_free (ItemDir *itemdir)
{
	GSList *iter;

	for (iter = itemdir->monitors; iter; iter = iter->next) {
		VFolderMonitor *monitor = iter->data;
		vfolder_monitor_cancel (monitor);
	}

	g_slist_free (itemdir->monitors);
	g_free (itemdir->uri);
	g_free (itemdir);
}

static gboolean
read_vfolder_from_file (VFolderInfo     *info,
			const gchar     *filename,
			gboolean         user_private,
			GnomeVFSResult  *result,
			GnomeVFSContext *context)
{
	xmlDoc *doc;
	xmlNode *node;
	GnomeVFSResult my_result;
	gint weight = 700;

	if (result == NULL)
		result = &my_result;

	/* Fail silently if filename does not exist */
	if (access (filename, F_OK) != 0)
		return TRUE;

	doc = xmlParseFile (filename); 
	if (doc == NULL
	    || doc->xmlRootNode == NULL
	    || doc->xmlRootNode->name == NULL
	    || g_ascii_strcasecmp (doc->xmlRootNode->name, 
				   "VFolderInfo") != 0) {
		*result = GNOME_VFS_ERROR_WRONG_FORMAT;
		xmlFreeDoc(doc);
		return FALSE;
	}

	if (context != NULL && 
	    gnome_vfs_context_check_cancellation (context)) {
		xmlFreeDoc(doc);
		*result = GNOME_VFS_ERROR_CANCELLED;
		return FALSE;
	}

	for (node = doc->xmlRootNode->xmlChildrenNode; 
	     node != NULL; 
	     node = node->next) {
		if (node->type != XML_ELEMENT_NODE ||
		    node->name == NULL)
			continue;

		if (context != NULL && 
		    gnome_vfs_context_check_cancellation (context)) {
			xmlFreeDoc(doc);
			*result = GNOME_VFS_ERROR_CANCELLED;
			return FALSE;
		}

		if (g_ascii_strcasecmp (node->name, "MergeDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				itemdir_new (info, dir, TRUE, FALSE, weight--);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "ItemDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				itemdir_new (info, dir, FALSE, FALSE, weight--);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "WriteDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				g_free (info->write_dir);

				info->write_dir = vfolder_escape_home (dir);
				itemdir_new (info, dir, FALSE, TRUE, 1000);

				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "DesktopDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				g_free (info->desktop_dir);
				info->desktop_dir = vfolder_escape_home (dir);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *folder = folder_read (info, 
						      user_private,
						      node);

			if (folder != NULL) {
				if (info->root != NULL)
					folder_unref (info->root);

				info->root = folder;
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "ReadOnly") == 0) {
			info->read_only = TRUE;
		}
	}

	xmlFreeDoc(doc);

	return TRUE;
}

struct {
	const gchar *dirname;
	const gchar *keyword;
} mergedir_keywords[] = {
	 /*Parent Dir*/  /*Keyword to add*/

	/* Gnome Menus */
	{ "Development",  "Development" },
	{ "Editors",      "TextEditor" },
	{ "Games",        "Game" },
	{ "Graphics",     "Graphics" },
	{ "Internet",     "Network" },
	{ "Multimedia",   "AudioVideo" },
	{ "Office",       "Office" },
	{ "Settings",     "Settings" },
	{ "System",       "System" },
	{ "Utilities",    "Utility" },

	/* Ximian Menus */
	{ "Addressbook",  "Office" },
	{ "Audio",        "AudioVideo" },
	{ "Calendar",     "Office" },
	{ "Finance",      "Office" },

	/* KDE Menus */
	{ "WordProcessing", "Office" },
	{ "Toys",           "Utility" },
};

static GQuark
get_mergedir_keyword (const gchar *dirname)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (mergedir_keywords); i++) {
		if (g_ascii_strcasecmp (mergedir_keywords [i].dirname, 
					dirname) == 0) {
			return g_quark_from_static_string (
					mergedir_keywords [i].keyword);
		}
	}

	return 0;
}

static Entry *
create_itemdir_entry (ItemDir          *id, 
		      const gchar      *rel_path,
		      GnomeVFSFileInfo *file_info)
{
	Entry *new_entry = NULL;
	gchar *file_uri;
	
	if (file_info->type != GNOME_VFS_FILE_TYPE_DIRECTORY) {
		if (!vfolder_check_extension (file_info->name, ".desktop")) 
			return NULL;

		if (vfolder_info_lookup_entry (id->info, file_info->name)) {
			D (g_print ("EXCLUDING DUPLICATE ENTRY: %s\n", 
				    file_info->name));
			return NULL;
		}
	}

	file_uri = vfolder_build_uri (id->uri, rel_path, NULL);

	if (file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
		VFolderMonitor *dir_monitor;

		/* Add monitor for subdirectory of this MergeDir/ItemDir */
		dir_monitor = vfolder_monitor_dir_new (file_uri, 
						       itemdir_monitor_cb, 
						       id);
		if (dir_monitor)
			id->monitors = 
				g_slist_prepend (id->monitors, dir_monitor);
	} else {
		gchar *displayname;

		if (id->untimestamp_files)
			displayname = 
				vfolder_untimestamp_file_name (file_info->name);
		else
			displayname = file_info->name;	

		/* Ref belongs to the VFolderInfo */
		new_entry = entry_new (id->info, 
				       file_uri, 
				       displayname, 
				       TRUE       /*user_private*/,
				       id->weight /*weight*/);

		if (id->untimestamp_files)
			g_free (displayname);
	}

	g_free (file_uri);

	return new_entry;
}

static void
add_keywords_from_relative_path (Entry *new_entry, const gchar *rel_path)
{
	gchar **pelems;
	GQuark keyword;
	gint i;

	pelems = g_strsplit (rel_path, "/", -1);
	if (!pelems)
		return;

	for (i = 0; pelems [i]; i++) {
		keyword = get_mergedir_keyword (pelems [i]);
		if (keyword)
			entry_add_implicit_keyword (new_entry, keyword);
	}

	g_strfreev (pelems);
}

static Entry *
create_mergedir_entry (ItemDir          *id,
		       const gchar      *rel_path,
		       GnomeVFSFileInfo *file_info)
{
	static GQuark merged, application, core_quark;
	Entry *new_entry;

	if (!merged) {
		merged = g_quark_from_static_string ("Merged");
		application = g_quark_from_static_string("Application");
		core_quark = g_quark_from_static_string ("Core");
	}

	new_entry = create_itemdir_entry (id, rel_path, file_info);
	if (new_entry) {
		/* 
		 * Mergedirs have the 'Merged' and 'Appliction' keywords added.
		 */
		entry_add_implicit_keyword (new_entry, merged);
		entry_add_implicit_keyword (new_entry, application);

		if (!strcmp (rel_path, file_info->name))
			entry_add_implicit_keyword (new_entry, core_quark);
		else
			add_keywords_from_relative_path (new_entry, rel_path);
	}

	return new_entry;
}

static gboolean
create_entry_directory_visit_cb (const gchar      *rel_path,
				 GnomeVFSFileInfo *file_info,
				 gboolean          recursing_will_loop,
				 gpointer          user_data,
				 gboolean         *recurse)
{
	ItemDir *id = user_data;

	if (id->is_mergedir)
		create_mergedir_entry (id, rel_path, file_info);
	else
		create_itemdir_entry (id, rel_path, file_info);

	*recurse = !recursing_will_loop;
	return TRUE;
}

static gboolean
vfolder_info_read_info (VFolderInfo     *info,
			GnomeVFSResult  *result,
			GnomeVFSContext *context)
{
	gboolean ret = FALSE;
	GSList *iter;

	if (!info->filename)
		return FALSE;

	/* Don't let set_dirty write out the file */
	info->loading = TRUE;

	ret = read_vfolder_from_file (info, 
				      info->filename, 
				      TRUE,
				      result, 
				      context);
	if (ret) {
		/* 
		 * Load WriteDir entries first, followed by ItemDir/MergeDirs 
		 * in order of appearance.
		 */
		for (iter = info->item_dirs; iter; iter = iter->next) {
			ItemDir *id = iter->data;

			gnome_vfs_directory_visit (
				id->uri,
				GNOME_VFS_FILE_INFO_DEFAULT,
				GNOME_VFS_DIRECTORY_VISIT_DEFAULT,
				create_entry_directory_visit_cb,
				id);
		}
	}

	/* Allow set_dirty to write config file again */
	info->loading = FALSE;

	return ret;
}		     


/* 
 * XML VFolder description reading
 */
static void
add_xml_tree_from_query (xmlNode *parent, Query *query)
{
	xmlNode *real_parent;

	if (query->not)
		real_parent = xmlNewChild (parent /* parent */,
					   NULL /* ns */,
					   "Not" /* name */,
					   NULL /* content */);
	else
		real_parent = parent;

	if (query->type == QUERY_KEYWORD) {
		const char *string = g_quark_to_string (query->val.keyword);

		xmlNewChild (real_parent /* parent */,
			     NULL /* ns */,
			     "Keyword" /* name */,
			     string /* content */);
	} else if (query->type == QUERY_FILENAME) {
		xmlNewChild (real_parent /* parent */,
			     NULL /* ns */,
			     "Filename" /* name */,
			     query->val.filename /* content */);
	} else if (query->type == QUERY_PARENT) {
		xmlNewChild (real_parent   /* parent */,
			     NULL          /* ns */,
			     "ParentQuery" /* name */,
			     NULL          /* content */);
	} else if (query->type == QUERY_OR ||
		   query->type == QUERY_AND) {
		xmlNode *node;
		const char *name;
		GSList *li;

		if (query->type == QUERY_OR)
			name = "Or";
		else /* QUERY_AND */
			name = "And";

		node = xmlNewChild (real_parent /* parent */,
				    NULL /* ns */,
				    name /* name */,
				    NULL /* content */);

		for (li = query->val.queries; li != NULL; li = li->next) {
			Query *subquery = li->data;
			add_xml_tree_from_query (node, subquery);
		}
	} else {
		g_assert_not_reached ();
	}
}

static void
add_excludes_to_xml (gpointer key, gpointer value, gpointer user_data)
{
	const char *filename = key;
	xmlNode *folder_node = user_data;

	xmlNewChild (folder_node /* parent */,
		     NULL /* ns */,
		     "Exclude" /* name */,
		     filename /* content */);
}

static void
add_xml_tree_from_folder (xmlNode *parent, Folder *folder)
{
	const GSList *li;
	xmlNode *folder_node;
	const gchar *extend_uri;

	/* 
	 * return if this folder hasn't been modified by the user, 
	 * and contains no modified subfolders.
	 */
	if (!folder->user_private && !folder->has_user_private_subfolders)
		return;

	folder_node = xmlNewChild (parent /* parent */,
				   NULL /* ns */,
				   "Folder" /* name */,
				   NULL /* content */);

	xmlNewChild (folder_node /* parent */,
		     NULL /* ns */,
		     "Name" /* name */,
		     folder_get_name (folder) /* content */);

	extend_uri = folder_get_extend_uri (folder);
	if (extend_uri) {
		xmlNewChild (folder_node /* parent */,
			     NULL /* ns */,
			     folder->is_link ? "ParentLink" : "Parent",
			     extend_uri /* content */);
	}

	if (folder->user_private) {
		const gchar *desktop_file;

		if (folder->read_only)
			xmlNewChild (folder_node /* parent */,
				     NULL /* ns */,
				     "ReadOnly" /* name */,
				     NULL /* content */);
		if (folder->dont_show_if_empty)
			xmlNewChild (folder_node /* parent */,
				     NULL /* ns */,
				     "DontShowIfEmpty" /* name */,
				     NULL /* content */);
		if (folder->only_unallocated)
			xmlNewChild (folder_node /* parent */,
				     NULL /* ns */,
				     "OnlyUnallocated" /* name */,
				     NULL /* content */);

		if (folder->desktop_file != NULL) {
			desktop_file = folder_get_desktop_file (folder);
			if (desktop_file)
				xmlNewChild (folder_node /* parent */,
					     NULL /* ns */,
					     "Desktop" /* name */,
					     desktop_file);
		}

		for (li = folder->includes; li != NULL; li = li->next) {
			const char *include = li->data;
			xmlNewChild (folder_node /* parent */,
				     NULL /* ns */,
				     "Include" /* name */,
				     include /* content */);
		}

		if (folder->excludes) {
			g_hash_table_foreach (folder->excludes,
					      add_excludes_to_xml,
					      folder_node);
		}

		if (folder->query) {
			xmlNode *query_node;
			query_node = xmlNewChild (folder_node /* parent */,
						  NULL /* ns */,
						  "Query" /* name */,
						  NULL /* content */);
			add_xml_tree_from_query (query_node, 
						 folder_get_query (folder));
		}
	}

	for (li = folder_list_subfolders (folder); li != NULL; li = li->next) {
		Folder *subfolder = li->data;
		add_xml_tree_from_folder (folder_node, subfolder);
	}
}

static xmlDoc *
xml_tree_from_vfolder (VFolderInfo *info)
{
	xmlDoc *doc;
	xmlNode *topnode;
	GSList *li;

	doc = xmlNewDoc ("1.0");

	topnode = xmlNewDocNode (doc /* doc */,
				 NULL /* ns */,
				 "VFolderInfo" /* name */,
				 NULL /* content */);
	doc->xmlRootNode = topnode;
	
	for (li = info->item_dirs; li != NULL; li = li->next) {
		ItemDir *item_dir = li->data;

		if (item_dir->is_mergedir)
			xmlNewChild (topnode /* parent */,
				     NULL /* ns */,
				     "MergeDir" /* name */,
				     item_dir->uri /* content */);
		else if (!item_dir->untimestamp_files)
			xmlNewChild (topnode /* parent */,
				     NULL /* ns */,
				     "ItemDir" /* name */,
				     item_dir->uri /* content */);
	}

	if (info->write_dir != NULL) {
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "WriteDir" /* name */,
			     info->write_dir /* content */);
	}

	/* Deprecated */
	if (info->desktop_dir != NULL) {
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "DesktopDir" /* name */,
			     info->desktop_dir /* content */);
	}

	if (info->root != NULL)
		add_xml_tree_from_folder (topnode, info->root);

	return doc;
}

/* FIXME: what to do about errors */
void
vfolder_info_write_user (VFolderInfo *info)
{
	xmlDoc *doc;
	GnomeVFSResult result;
	gchar *tmpfile;
	struct timeval tv;

	if (info->loading || !info->dirty)
		return;

	if (!info->filename)
		return;

	info->loading = TRUE;

	/* FIXME: errors, anyone? */
	result = vfolder_make_directory_and_parents (info->filename, 
						     TRUE, 
						     0700);
	if (result != GNOME_VFS_OK) {
		g_warning ("Unable to create parent directory for "
			   "vfolder-info file: %s",
			   info->filename);
		return;
	}

	doc = xml_tree_from_vfolder (info);
	if (!doc)
		return;

	gettimeofday (&tv, NULL);
	tmpfile = g_strdup_printf ("%s.tmp-%d", 
				   info->filename,
				   (int) (tv.tv_sec ^ tv.tv_usec));

	/* Write to temporary file */
	xmlSaveFormatFile (tmpfile, doc, TRUE /* format */);

	/* Avoid being notified of move, since we're performing it */
	if (info->filename_monitor)
		vfolder_monitor_freeze (info->filename_monitor);

	/* Move temp file over to real filename */
	result = gnome_vfs_move (tmpfile, 
				 info->filename, 
				 TRUE /*force_replace*/);
	if (result != GNOME_VFS_OK) {
		g_warning ("Error writing vfolder configuration "
			   "file \"%s\": %s.",
			   info->filename,
			   gnome_vfs_result_to_string (result));
	}

	/* Start listening to changes again */
	if (info->filename_monitor)
		vfolder_monitor_thaw (info->filename_monitor);

	xmlFreeDoc(doc);
	g_free (tmpfile);

	info->modification_time = time (NULL);
	info->dirty = FALSE;
	info->loading = FALSE;
}

static void
vfolder_info_reset (VFolderInfo *info)
{
	GSList *iter;

	info->loading = TRUE;
	
	if (info->filename_monitor) {
		vfolder_monitor_cancel (info->filename_monitor);
		info->filename_monitor = NULL;
	}

	for (iter = info->item_dirs; iter; iter = iter->next) {
		ItemDir *dir = iter->data;
		itemdir_free (dir);
	}
	g_slist_free (info->item_dirs);
	info->item_dirs = NULL;

	g_free (info->filename);
	g_free (info->write_dir);
	g_free (info->desktop_dir);

	info->filename = NULL;
	info->desktop_dir = NULL;
	info->write_dir = NULL;

	folder_unref (info->root);
	info->root = NULL;

	g_slist_foreach (info->entries, (GFunc) entry_unref, NULL);
	g_slist_free (info->entries);
	info->entries = NULL;

	if (info->entries_ht) {
		g_hash_table_destroy (info->entries_ht);
		info->entries_ht = NULL;
	}

	/* Clear flags */
	info->read_only =
		info->dirty = 
		info->loading =
		info->has_unallocated_folder = FALSE;
}

static gchar *
find_replacement_for_delete (ItemDir *id, const gchar *filename)
{
	GSList *iter, *miter;
	gint idx;
	
	idx = g_slist_index (id->info->item_dirs, id);
	if (idx < 0)
		return NULL;

	iter = g_slist_nth (id->info->item_dirs, idx + 1);

	for (; iter; iter = iter->next) {
		ItemDir *id_next = iter->data;

		for (miter = id_next->monitors; miter; miter = miter->next) {
			VFolderMonitor *monitor = miter->data;
			GnomeVFSURI *check_uri;
			gchar *uristr;

			uristr = vfolder_build_uri (monitor->uri,
						    filename,
						    NULL);
			check_uri = gnome_vfs_uri_new (uristr);

			if (gnome_vfs_uri_exists (check_uri)) {
				gnome_vfs_uri_unref (check_uri);
				return uristr;
			}

			gnome_vfs_uri_unref (check_uri);
			g_free (uristr);
		}
	}

	return NULL;
}

static void
integrate_entry (Folder *folder, Entry *entry, gboolean do_add)
{
	const GSList *subs;
	Entry *existing;
	Query *query;
	gboolean matches = FALSE;

	for (subs = folder_list_subfolders (folder); subs; subs = subs->next) {
		Folder *asub = subs->data;
		integrate_entry (asub, entry, do_add);
	}

	if (folder->only_unallocated)
		return;

	query = folder_get_query (folder);
	if (do_add && query)
		matches = query_try_match (query, folder, entry);

	existing = folder_get_entry (folder, entry_get_displayname (entry));
	if (existing) {
		/* 
		 * Do nothing if the existing entry has a higher weight than the
		 * one we wish to add.
		 */
		if (entry_get_weight (existing) > entry_get_weight (entry))
			return;
		
		folder_remove_entry (folder, existing);

		if (matches) {
			folder_add_entry (folder, entry);

			folder_emit_changed (folder, 
					     entry_get_displayname (entry),
					     GNOME_VFS_MONITOR_EVENT_CHANGED);
		} else 
			folder_emit_changed (folder, 
					     entry_get_displayname (entry),
					     GNOME_VFS_MONITOR_EVENT_DELETED);
	} 
	else if (matches) {
		folder_add_entry (folder, entry);

		folder_emit_changed (folder, 
				     entry_get_displayname (entry),
				     GNOME_VFS_MONITOR_EVENT_CREATED);
	}
}

static void
itemdir_monitor_handle (ItemDir                  *id,
			GnomeVFSURI              *full_uri,
			const gchar              *full_uristr,
			const gchar              *displayname,
			GnomeVFSMonitorEventType  event_type)
{
	Entry *entry;
	GnomeVFSURI *real_uri;
	gchar *replace_uri;

	switch (event_type) {
	case GNOME_VFS_MONITOR_EVENT_CREATED:
	case GNOME_VFS_MONITOR_EVENT_CHANGED:
		/* Look for an existing entry with the same displayname */
		entry = vfolder_info_lookup_entry (id->info, displayname);
		if (entry) {
			real_uri = entry_get_real_uri (entry);

			if (gnome_vfs_uri_equal (full_uri, real_uri)) {
				/* Refresh */
				entry_set_dirty (entry);
			} 
			else if (entry_get_weight (entry) < id->weight) {
				/* 
				 * Existing entry is less important than the new
				 * one, so replace.
				 */
				entry_set_filename (entry, full_uristr);
			}

			gnome_vfs_uri_unref (real_uri);
		} 
		else if (event_type == GNOME_VFS_MONITOR_EVENT_CREATED) {
			const gchar *rel_path;
			GnomeVFSFileInfo *file_info;
			GnomeVFSResult result;

			file_info = gnome_vfs_file_info_new ();

			result = 
				gnome_vfs_get_file_info_uri (
					full_uri,
					file_info,
					GNOME_VFS_FILE_INFO_DEFAULT);
			if (result != GNOME_VFS_OK) {
				gnome_vfs_file_info_unref (file_info);
				break;
			}

			rel_path  = strstr (full_uristr, id->uri);
			rel_path += strlen (id->uri);

			if (id->is_mergedir)
				entry = create_mergedir_entry (id,
							       rel_path,
							       file_info);
			else
				entry = create_itemdir_entry (id,
							      rel_path,
							      file_info);

			gnome_vfs_file_info_unref (file_info);
		}

		if (entry) {
			entry_ref (entry);
			integrate_entry (id->info->root, 
					 entry, 
					 TRUE /* do_add */);
			entry_unref (entry);

			id->info->modification_time = time (NULL);
		}

		break;
	case GNOME_VFS_MONITOR_EVENT_DELETED:
		entry = vfolder_info_lookup_entry (id->info, displayname);
		if (!entry)
			break;

		/* Only care if its the currently visible entry being deleted */
		real_uri = entry_get_real_uri (entry);
		if (!gnome_vfs_uri_equal (full_uri, real_uri)) {
			gnome_vfs_uri_unref (real_uri);
			break;
		}
		gnome_vfs_uri_unref (real_uri);

		replace_uri = find_replacement_for_delete (id, displayname);
		if (replace_uri) {
			entry_set_filename (entry, replace_uri);
			g_free (replace_uri);
		}

		entry_ref (entry);
		integrate_entry (id->info->root, 
				 entry, 
				 replace_uri != NULL /* do_add */);
		entry_unref (entry);

		id->info->modification_time = time (NULL);

		break;
	default:
		break;
	}	
}

static void
itemdir_monitor_cb (GnomeVFSMonitorHandle    *handle,
		    const gchar              *monitor_uri,
		    const gchar              *info_uri,
		    GnomeVFSMonitorEventType  event_type,
		    gpointer                  user_data)
{
	ItemDir *id = user_data;
	gchar *filename, *filename_ts;
	GnomeVFSURI *uri;

	g_print ("*** Itemdir '%s' monitor called! ***\n", monitor_uri);

	VFOLDER_INFO_WRITE_LOCK (id->info);
	id->info->loading = TRUE;

	/* Get untimestamped filename */
	uri = gnome_vfs_uri_new (info_uri);
	filename = gnome_vfs_uri_extract_short_name (uri);

	if (id->untimestamp_files) {
		filename_ts = filename;
		filename = vfolder_untimestamp_file_name (filename);
		g_free (filename_ts);
	}

	itemdir_monitor_handle (id,
				uri,
				info_uri,
				filename,
				event_type);

	id->info->loading = FALSE;
	VFOLDER_INFO_WRITE_UNLOCK (id->info);

	gnome_vfs_uri_unref (uri);
	g_free (filename);
}

static void
check_monitors_foreach (gpointer key, gpointer val, gpointer user_data)
{
	MonitorHandle *handle = key;
	GnomeVFSURI *uri, *curi;
	const gchar *path;

	uri = handle->uri;
	path = gnome_vfs_uri_get_path (handle->uri);

	if (handle->type == GNOME_VFS_MONITOR_DIRECTORY) {
		Folder *folder;
		GSList *children = val, *iter, *found;
		GSList *new_children;

		folder = vfolder_info_get_folder (handle->info, path);
		if (!folder) {
			gnome_vfs_monitor_callback (
				(GnomeVFSMethodHandle *) handle,
				handle->uri,
				GNOME_VFS_MONITOR_EVENT_DELETED);
			return;
		}
			
		new_children = folder_list_children (folder);

		for (iter = children; iter; iter = iter->next) {
			gchar *child_name = iter->data;
				
			found = g_slist_find_custom (new_children,
						     child_name,
						     (GCompareFunc) strcmp);
			if (found) {
				g_free (found->data);
				new_children = 
					g_slist_delete_link (new_children, 
							     found);
			} else {
				curi = 
					gnome_vfs_uri_append_file_name (
						handle->uri, 
						child_name);

				gnome_vfs_monitor_callback (
					(GnomeVFSMethodHandle *) handle,
					curi,
				        GNOME_VFS_MONITOR_EVENT_DELETED);

				gnome_vfs_uri_unref (curi);
			}

			g_free (child_name);
		}

		for (iter = new_children; iter; iter = iter->next) {
			gchar *child_name = iter->data;

			curi = gnome_vfs_uri_append_file_name (handle->uri, 
							       child_name);

			gnome_vfs_monitor_callback (
				(GnomeVFSMethodHandle *) handle,
				curi,
				GNOME_VFS_MONITOR_EVENT_CREATED);

			gnome_vfs_uri_unref (curi);
			g_free (child_name);
		}

		g_slist_free (new_children);
		g_slist_free (children);
	} 
	else {
		gboolean found;

		found = vfolder_info_get_entry (handle->info, path) ||
			vfolder_info_get_folder (handle->info, path);

		gnome_vfs_monitor_callback (
			(GnomeVFSMethodHandle *) handle,
			handle->uri,
			found ?
			        GNOME_VFS_MONITOR_EVENT_CHANGED :
			        GNOME_VFS_MONITOR_EVENT_DELETED);
	}
}

static void vfolder_info_init (VFolderInfo *info);

static gboolean
filename_monitor_handle (gpointer user_data)
{
	VFolderInfo *info = user_data;
	GHashTable *monitors;
	GSList *iter;

	g_print ("*** PROCESSING .vfolder-info!!! ***\n");
	
	VFOLDER_INFO_WRITE_LOCK (info);
	info->loading = TRUE;

	monitors = g_hash_table_new (g_direct_hash, g_direct_equal);

	/* Compose a hash of all existing monitors and their children */
	for (iter = info->requested_monitors; iter; iter = iter->next) {
		MonitorHandle *mhandle = iter->data;
		GSList *monitored_paths = NULL;

		if (mhandle->type == GNOME_VFS_MONITOR_DIRECTORY) {
			Folder *folder;

			folder = 
				vfolder_info_get_folder (
					info, 
					gnome_vfs_uri_get_path (mhandle->uri));
			if (folder)
				monitored_paths = folder_list_children (folder);
		}

		g_hash_table_insert (monitors, mhandle, monitored_paths);
	}

	vfolder_info_reset (info);
	vfolder_info_init (info);

	vfolder_info_read_info (info, NULL, NULL);

	/* Traverse monitor hash and diff with newly read folder structure */
	g_hash_table_foreach (monitors, check_monitors_foreach, info);
	g_hash_table_destroy (monitors);

	info->loading = FALSE;
	VFOLDER_INFO_WRITE_UNLOCK (info);

	return FALSE;
}

static void
filename_monitor_cb (GnomeVFSMonitorHandle *handle,
		     const gchar *monitor_uri,
		     const gchar *info_uri,
		     GnomeVFSMonitorEventType event_type,
		     gpointer user_data)
{
	VFolderInfo *info = user_data;

	g_print ("*** Filename monitor called! ***\n");

	if (info->filename_reload_tag) {
		g_source_remove (info->filename_reload_tag);
		info->filename_reload_tag = 0;
	}

	/* 
	 * Don't process the .vfolder-info for 2 seconds after a delete event or
	 * .5 seconds after a create event.  This allows files to be rewritten
	 * before we start reading it and possibly copying the system default
	 * file over top of it.  
	 */
	switch (event_type) {
	case GNOME_VFS_MONITOR_EVENT_DELETED:
		info->filename_reload_tag = 
			g_timeout_add (2000, filename_monitor_handle, info);
		break;
	case GNOME_VFS_MONITOR_EVENT_CREATED:
		info->filename_reload_tag = 
			g_timeout_add (500, filename_monitor_handle, info);
		break;
	case GNOME_VFS_MONITOR_EVENT_CHANGED:
	default:
		filename_monitor_handle (info);
		break;
	}
}

static gboolean
g_str_case_equal (gconstpointer v1,
		  gconstpointer v2)
{
	const gchar *string1 = v1;
	const gchar *string2 = v2;
  
	return g_ascii_strcasecmp (string1, string2) == 0;
}

/* 31 bit hash function */
static guint
g_str_case_hash (gconstpointer key)
{
	const char *p = key;
	guint h = g_ascii_toupper (*p);
	
	if (h)
		for (p += 1; *p != '\0'; p++)
			h = (h << 5) - h + g_ascii_toupper (*p);

	return h;
}


/* 
 * VFolderInfo Implementation
 */
static VFolderInfo *
vfolder_info_new (const char *scheme)
{
	VFolderInfo *info;

	info = g_new0 (VFolderInfo, 1);
	info->scheme = g_strdup (scheme);

	g_static_rw_lock_init (&info->rw_lock);

	return info;
}

static gboolean
copy_user_default_file (VFolderInfo *info, GnomeVFSURI *user_file_uri)
{
	gchar *default_file_name; 
	GnomeVFSResult result;
	GnomeVFSURI *default_file_uri;

	default_file_name = g_strconcat (SYSCONFDIR,
					 "/gnome-vfs-2.0/vfolders/",
					 info->scheme, ".vfolder-info-default",
					 NULL);
	default_file_uri = gnome_vfs_uri_new (default_file_name);
	g_free (default_file_name);

	if (!gnome_vfs_uri_exists (default_file_uri)) {
		gnome_vfs_uri_unref (default_file_uri);
		return FALSE;
	}

	result = vfolder_make_directory_and_parents (info->filename, 
						     TRUE, 
						     0700);
	if (result != GNOME_VFS_OK) {
		g_warning ("Unable to create parent directory for "
			   "vfolder-info file: %s",
			   info->filename);
		gnome_vfs_uri_unref (default_file_uri);
		return FALSE;
	}

	/* Copy the default file */
	result = gnome_vfs_xfer_uri (default_file_uri /* source_uri */,
				     user_file_uri    /* target_uri */,
				     GNOME_VFS_XFER_USE_UNIQUE_NAMES, 
				     GNOME_VFS_XFER_ERROR_MODE_ABORT, 
				     GNOME_VFS_XFER_OVERWRITE_MODE_ABORT,
				     NULL, 
				     NULL);

	gnome_vfs_uri_unref (default_file_uri);

	return result == GNOME_VFS_OK;
}

static void
vfolder_info_find_filenames (VFolderInfo *info)
{
	gchar *scheme = info->scheme;

	/* 
	 * FIXME: load from gconf 
	 */

	if (strstr (scheme, "-all-users")) {
		GSList *list = NULL;
		int i;
		const char *path;
		char *dir, **ppath;
		ItemDir *id;
		int weight = 800;

		info->filename = g_strconcat (SYSCONFDIR,
					      "/gnome-vfs-2.0/vfolders/",
					      scheme, ".vfolder-info",
					      NULL);

		path = g_getenv ("GNOME2_PATH");
		if (!path)
			return;

		ppath = g_strsplit (path, ":", -1);
		for (i = 0; ppath[i] != NULL; i++) {
			dir = g_build_filename (ppath[i], 
						"/share/applications/",
						NULL);
			id = itemdir_new (info, dir, FALSE, FALSE, weight--);
			g_free (dir);
			
			list = g_slist_prepend (list, id);
		}
		g_strfreev (ppath);

		info->item_dirs = g_slist_reverse (list);
	} 
	else {
		GnomeVFSURI *file_uri;

		/* output .vfolder-info filename */
		info->filename = g_strconcat (g_get_home_dir (),
					      "/" DOT_GNOME "/vfolders/",
					      scheme, ".vfolder-info",
					      NULL);

		/* default write directory */
		info->write_dir = g_build_filename (g_get_home_dir (),
						    "/" DOT_GNOME "/vfolders/",
						    scheme,
						    NULL);

		file_uri = gnome_vfs_uri_new (info->filename);

		/* 
		 * Check user's .vfolder-info exists, and try to copy the
		 * default if not. 
		 */
		if (!gnome_vfs_uri_exists (file_uri))
			copy_user_default_file (info, file_uri);

		gnome_vfs_uri_unref (file_uri);
	}
}

static void
vfolder_info_init (VFolderInfo *info)
{
	gchar *all_user_scheme;

	info->loading = TRUE;
	info->entries_ht = g_hash_table_new (g_str_case_hash, g_str_case_equal);
	info->root = folder_new (info, "Root", TRUE);

	/* 
	 * Set the extend uri for the root folder to the -all-users version of
	 * the scheme, in case the user doesn't have a private .vfolder-info
	 * file, and there's no default.  
	 */
	all_user_scheme = g_strconcat (info->scheme, "-all-users:///", NULL);
	folder_set_extend_uri (info->root, all_user_scheme);
	g_free (all_user_scheme);

	vfolder_info_find_filenames (info);

	if (g_getenv ("GNOME_VFS_VFOLDER_INFODIR")) {
		gchar *filename = g_strconcat (info->scheme, 
					       ".vfolder-info", 
					       NULL);

		g_free (info->filename);
		info->filename = 
			vfolder_build_uri (
				g_getenv ("GNOME_VFS_VFOLDER_INFODIR"),
				filename,
				NULL);
		g_free (filename);
	}

	if (g_getenv ("GNOME_VFS_VFOLDER_WRITEDIR")) {
		g_free (info->write_dir);
		info->write_dir = 
			vfolder_build_uri (
				g_getenv ("GNOME_VFS_VFOLDER_WRITEDIR"),
				info->scheme,
				NULL);
	}

	info->filename_monitor = 
		vfolder_monitor_file_new (info->filename,
					  filename_monitor_cb,
					  info);

	info->modification_time = time (NULL);
	info->loading = FALSE;
	info->dirty = FALSE;
}

static void
vfolder_info_destroy (VFolderInfo *info)
{
	if (info == NULL)
		return;

	vfolder_info_reset (info);

	if (info->filename_reload_tag)
		g_source_remove (info->filename_reload_tag);

	g_static_rw_lock_free (&info->rw_lock);

	g_free (info->scheme);

	while (info->requested_monitors) {
		GnomeVFSMethodHandle *monitor = info->requested_monitors->data;
		vfolder_info_cancel_monitor (monitor);
	}

	g_free (info);
}

/* 
 * Call to recursively list folder contents, causing them to allocate entries,
 * so that we get OnlyUnallocated folder counts correctly.
 */
static void
load_folders (Folder *folder)
{
	const GSList *iter;

	for (iter = folder_list_subfolders (folder); iter; iter = iter->next) {
		Folder *folder = iter->data;
		load_folders (folder);
	}
}

static GHashTable *infos = NULL;
G_LOCK_DEFINE_STATIC (vfolder_lock);

VFolderInfo *
vfolder_info_locate (const gchar *scheme)
{
	VFolderInfo *info = NULL;

	G_LOCK (vfolder_lock);

	if (!infos) {
		infos = 
			g_hash_table_new_full (
				g_str_hash, 
				g_str_equal,
				NULL,
				(GDestroyNotify) vfolder_info_destroy);
	}

	info = g_hash_table_lookup (infos, scheme);
	if (info) {
		G_UNLOCK (vfolder_lock);
		return info;
	}
	else {
		info = vfolder_info_new (scheme);
		g_hash_table_insert (infos, info->scheme, info);

		VFOLDER_INFO_WRITE_LOCK (info);
		G_UNLOCK (vfolder_lock);

		vfolder_info_init (info);

		if (!vfolder_info_read_info (info, NULL, NULL)) {
			D (g_print ("DESTROYING INFO FOR SCHEME: %s\n", 
				    scheme));

			G_LOCK (vfolder_lock);
			g_hash_table_remove (infos, info);
			G_UNLOCK (vfolder_lock);

			return NULL;
		} else {
			if (info->has_unallocated_folder) {
				info->loading = TRUE;
				load_folders (info->root);
				info->loading = FALSE;
			}

			VFOLDER_INFO_WRITE_UNLOCK (info);

			return info;
		}
	}
}

void
vfolder_info_set_dirty (VFolderInfo *info)
{
	if (info->loading)
		return;

	info->dirty = TRUE;
}

static Folder *
get_folder_for_path_list_n (Folder    *parent, 
			    gchar    **paths, 
			    gint       path_index,
			    gboolean   skip_last) 
{
	Folder *child;
	gchar *subname, *subsubname;

	if (!parent || folder_is_hidden (parent))
		return NULL;

	subname = paths [path_index];
	if (!subname)
		return parent;

	subsubname = paths [path_index + 1];
	if (!subsubname && skip_last)
		return parent;

	if (*subname == '\0')
		child = parent;
	else
		child = folder_get_subfolder (parent, subname);

	return get_folder_for_path_list_n (child, 
					   paths, 
					   path_index + 1, 
					   skip_last);
}

static Folder *
get_folder_for_path (Folder *root, const gchar *path, gboolean skip_last) 
{
	gchar **paths;
	Folder *folder;

	paths = g_strsplit (path, "/", -1);
	if (!paths)
		return NULL;

	folder = get_folder_for_path_list_n (root, paths, 0, skip_last);

	g_strfreev (paths);
	
	return folder;
}

Folder *
vfolder_info_get_parent (VFolderInfo *info, const gchar *path)
{
	return get_folder_for_path (info->root, path, TRUE);
}

Folder *
vfolder_info_get_folder (VFolderInfo *info, const gchar *path)
{
	return get_folder_for_path (info->root, path, FALSE);
}

Entry *
vfolder_info_get_entry (VFolderInfo *info, const gchar *path)
{
	Folder *parent;
	gchar *subname;

	parent = vfolder_info_get_parent (info, path);
	if (!parent)
		return NULL;

	subname = strrchr (path, '/');
	if (!subname)
		return NULL;
	else
		subname++;

	return folder_get_entry (parent, subname);
}

const GSList *
vfolder_info_list_all_entries (VFolderInfo *info)
{
	return info->entries;
}

Entry *
vfolder_info_lookup_entry (VFolderInfo *info, const gchar *name)
{
	return g_hash_table_lookup (info->entries_ht, name);
}

void 
vfolder_info_add_entry (VFolderInfo *info, Entry *entry)
{
	info->entries = g_slist_prepend (info->entries, entry);
	g_hash_table_insert (info->entries_ht, 
			     (gchar *) entry_get_displayname (entry),
			     entry);
}

void 
vfolder_info_remove_entry (VFolderInfo *info, Entry *entry)
{
	info->entries = g_slist_remove (info->entries, entry);
	g_hash_table_remove (info->entries_ht, 
			     entry_get_displayname (entry));
}

void 
vfolder_info_emit_change (VFolderInfo              *info,
			  const char               *path,
			  GnomeVFSMonitorEventType  event_type)
{
	GSList *iter;
	GnomeVFSURI *uri;
	gchar *uristr;

	if (info->loading) 
		return;

	uristr = g_strconcat (info->scheme, "://", path, NULL);
	uri = gnome_vfs_uri_new (uristr);
	g_free (uristr);

	for (iter = info->requested_monitors; iter; iter = iter->next) {
		MonitorHandle *handle = iter->data;

		if (gnome_vfs_uri_equal (uri, handle->uri) ||
		    (handle->type == GNOME_VFS_MONITOR_DIRECTORY &&
		     gnome_vfs_uri_is_parent (handle->uri, uri, FALSE))) {
			g_print ("EMITTING CHANGE: %s://%s for %s, %s%s%s\n",
				    info->scheme, 
				    path,
				    handle->uri->text,
				    event_type == GNOME_VFS_MONITOR_EVENT_CREATED ? "CREATED" : "",
				    event_type == GNOME_VFS_MONITOR_EVENT_DELETED ? "DELETED" : "",
				    event_type == GNOME_VFS_MONITOR_EVENT_CHANGED ? "CHANGED" : "");

			gnome_vfs_monitor_callback (
				(GnomeVFSMethodHandle *) handle,
				uri,
				event_type);
		}
	}

	gnome_vfs_uri_unref (uri);
}

void
vfolder_info_add_monitor (VFolderInfo           *info,
			  GnomeVFSMonitorType    type,
			  GnomeVFSURI           *uri,
			  GnomeVFSMethodHandle **handle)
{
	MonitorHandle *monitor = g_new0 (MonitorHandle, 1);
	monitor->info = info;
	monitor->type = type;

	monitor->uri = uri;
	gnome_vfs_uri_ref (uri);

	info->requested_monitors = g_slist_prepend (info->requested_monitors,
						    monitor);

	g_print ("EXTERNALLY WATCHING: %s\n", 
		    gnome_vfs_uri_to_string (uri, 0));
	
	*handle = (GnomeVFSMethodHandle *) monitor;
}

void 
vfolder_info_cancel_monitor (GnomeVFSMethodHandle  *handle)
{
	MonitorHandle *monitor = (MonitorHandle *) handle;

	monitor->info->requested_monitors = 
		g_slist_remove (monitor->info->requested_monitors, monitor);

	gnome_vfs_uri_unref (monitor->uri);
	g_free (monitor);
}

void
vfolder_info_destroy_all (void)
{
	G_LOCK (vfolder_lock);

	if (infos) {
		g_hash_table_destroy (infos);
		infos = NULL;
	}

	G_UNLOCK (vfolder_lock);
}

void
vfolder_info_dump_entries (VFolderInfo *info, int offset)
{
	g_slist_foreach (info->entries, 
			 (GFunc) entry_dump, 
			 GINT_TO_POINTER (offset));
}
