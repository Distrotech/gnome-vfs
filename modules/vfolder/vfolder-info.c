
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include "vfolder-common.h"
#include "vfolder-util.h"

#define DOT_GNOME ".gnome2"


/*
  load
    load mergedir files to pool, relative uri pool, keyword pool (spawn a thread?)
      if .desktop, add to temp uri hash
    load desktopdir files to temp name list
      
    foreach folder recursively
      check extend uri
      match include/exclude: try as uri, representing extended uri, else represent mergedir pool
      <folder> subdir: add folder to folder hash, if overrides found uri remove uri 
      match pool against folder query, add matches to local file hash

    foreach pool entry with 0 allocs, add to unmatched pool

  path resolve:
    foreach path segment
      lookup subfolder with name, if found loop, minus segment
      if last segment, lookup local file hash
      lookup extended uri + requested path minus segment :: run through query

  directory list:
    return sort of entend uri children :: run through query + subfolders + match pool

  write:
    make private, add to include list

  delete:
    add to exclude list
    if private, delete private copy

  create:
    make private, add to include (?)
*/
      
/*
 load
   load include/mergedir items
     process query: include-first
   load parent uri items
     process query: exclude-first
   add monitors to accepted entries
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

	folder = folder_new (info, NULL);
	folder->user_private = user_private;

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
				folder_set_extend_uri (folder, parent);
				xmlFree (parent);
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
				folder_add_include (folder, file);
				xmlFree (file);
			}
		}
		else if (g_ascii_strcasecmp (node->name, "Exclude") == 0) {
			xmlChar *file = xmlNodeGetContent (node);

			if (file) {
				folder_add_exclude (folder, file);
				xmlFree (file);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Query") == 0) {
			Query *query;

			query = query_read (node);
			if (query)
				folder_set_query (folder, query);
		} 
		else if (g_ascii_strcasecmp (node->name, 
					     "OnlyUnallocated") == 0) {
			folder->only_unallocated = TRUE;
		} 
		else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *new_folder = folder_read (info, 
							  user_private,
							  node);

			if (new_folder != NULL) {
				folder->subfolders =
					g_slist_append (folder->subfolders,
							new_folder);
				new_folder->parent = folder;
				folder_ref (folder);
			}
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
	if (folder->name == NULL) {
		folder_unref (folder);
		folder = NULL;
	}

	folder->includes = g_slist_reverse (folder->includes);

	return folder;
}

/* FORMAT looks like:
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
 *     </Folder>
 *     <Folder>
 *       <Name>Test_Folder</Name>
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

static void
itemdir_monitor_cb (GnomeVFSMonitorHandle    *handle,
		    const gchar              *monitor_uri,
		    const gchar              *info_uri,
		    GnomeVFSMonitorEventType  event_type,
		    gpointer                  user_data)
{
}

static ItemDir *
itemdir_new (VFolderInfo *info, gchar *uri)
{
	ItemDir *ret;
	ret = g_new0 (ItemDir, 1);
	ret->uri = g_strdup (uri);
	ret->monitor = vfolder_monitor_directory_new (uri, 
						      itemdir_monitor_cb,
						      info);
	return ret;
}

static void
itemdir_free (ItemDir *itemdir)
{
	vfolder_monitor_cancel (itemdir->monitor);
	g_free (itemdir->uri);
	g_free (itemdir);
}

static gboolean
read_vfolder_from_file (VFolderInfo     *info,
			gchar           *filename,
			gboolean         user_private,
			GnomeVFSResult  *result,
			GnomeVFSContext *context)
{
	xmlDoc *doc;
	xmlNode *node;
	gboolean got_a_vfolder_dir = FALSE;
	GnomeVFSResult my_result;

	if (result == NULL)
		result = &my_result;

	if (access (filename, F_OK) != 0) {
		*result = GNOME_VFS_ERROR_INTERNAL;
		return FALSE;
	}

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
				ItemDir *id = itemdir_new (info, dir);
				info->merge_dirs = 
					g_slist_append (info->merge_dirs, id);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "ItemDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				ItemDir *id = itemdir_new (info, dir);

				/* 
				 * If we have an ItemDir element, free 
				 * the default itemdirs created in 
				 * vfolder_info_init.  
				 */
				if (!got_a_vfolder_dir) {
					g_slist_foreach (info->item_dirs,
							 (GFunc) itemdir_free, 
							 NULL);
					g_slist_free (info->item_dirs);
					info->item_dirs = NULL;
				}

				got_a_vfolder_dir = TRUE;
				info->item_dirs = 
					g_slist_append (info->item_dirs, id);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "WriteDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				g_free (info->write_dir);
				info->write_dir = dir;
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "DesktopDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				g_free (info->desktop_dir);
				info->desktop_dir = g_strdup (dir);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *folder = folder_read (info, 
						      user_private,
						      node);

			if (folder != NULL) {
				if (info->root != NULL)
					entry_unref ((Entry *)info->root);

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
	 /*Parent Dir */ /*Keyword*/
	{ "Development",  "Development" },
	{ "Editors",      "TextEditor" },
	{ "Games",        "Game" },
	{ "Graphics",     "Graphics" },
	{ "Internet",     "Network" },
	{ "Multimedia",   "AudioVideo" },
	{ "Office",       "Office" },
	{ "Settings",     "Settings" },
	{ "System",       "System" },
	{ "Utilities",    "Utility" }
};

static GQuark
get_mergedir_keyword (gchar *dirname)
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

static gboolean
create_entries_from_directory (VFolderInfo     *info,
			       gchar           *uri,
			       GQuark           add_keyword)
{
	GList *flist, *iter;
	GnomeVFSResult result;

	result = gnome_vfs_directory_list_load (&flist, 
						uri,
						GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return FALSE;

	// FIXME: create monitor??

	for (iter = flist; iter; iter = iter->next) {
		GnomeVFSFileInfo *file_info = iter->data;
		gchar *file_uri;
		Entry *new_entry;

		if (!strcmp (file_info->name, ".") ||
		    !strcmp (file_info->name, ".."))
			continue;

		file_uri = g_build_filename (uri, file_info->name, NULL);

		if (file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			create_entries_from_directory (
				info,
				file_uri,
				get_mergedir_keyword (file_info->name));
			g_free (file_uri);
			continue;			
		}

		if (!check_extension (file_info->name, ".desktop")) {
			g_free (file_uri);
			continue;
		}

		new_entry = entry_new (info, file_uri, file_info->name, TRUE);
		g_free (file_uri);

		if (new_entry) {
			if (add_keyword)
				entry_add_implicit_keyword (new_entry, 
							    add_keyword);

			vfolder_info_add_entry (info, new_entry);
		}
	}

	gnome_vfs_file_info_list_free (flist);
	return TRUE;
}
		      
static gboolean
vfolder_info_read_info (VFolderInfo     *info,
			GnomeVFSResult  *result,
			GnomeVFSContext *context)
{
	gboolean ret = FALSE;
	GQuark core_quark;

	if (!info->filename)
		return FALSE;

	ret = read_vfolder_from_file (info, 
				      info->filename, 
				      FALSE,
				      result, 
				      context);

	if (ret) {
		GSList *iter;

		for (iter = info->item_dirs; iter; iter = iter->next) {
			ItemDir *id = iter->data;
			create_entries_from_directory (info, id->uri, 0);
		}

		core_quark = g_quark_from_static_string ("Core");

		for (iter = info->merge_dirs; iter; iter = iter->next) {
			ItemDir *id = iter->data;
			create_entries_from_directory (info, 
						       id->uri, 
						       core_quark);
		}

		if (info->write_dir)
			create_entries_from_directory (info, 
						       info->write_dir, 
						       0);
	}

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

#if 0
static gchar *
folder_get_extend_uri (Folder *source)
{
	gchar *ret;
	Folder *iter;
	GString *extend_path

	if (source->parent == NULL)
		return  g_strdup ("/");

	extend_path = g_string_new (NULL);

	for (iter = source; iter && iter->parent; iter = iter->parent) {
		if (folder_get_extend_uri (iter)) {
			g_string_prepend (extend_path, 
					  folder_get_extend_uri (iter));
			break;
		} else {
			g_string_prepend (extend_path, folder_get_name (iter));
			g_string_prepend_c (extend_path, '/');
		}
	}

	ret = extend_path->str;
	g_string_free (extend_path, FALSE);

	return ret;
}
#endif

static void
add_xml_tree_from_folder (xmlNode *parent, Folder *folder)
{
	GSList *li;
	xmlNode *folder_node;
	gchar *extend_uri;

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
	if (extend_uri != NULL) {
		xmlNewChild (folder_node /* parent */,
			     NULL /* ns */,
			     "Parent" /* name */,
			     extend_uri /* content */);
	}

	if (folder->user_private) {
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
			xmlNewChild (folder_node /* parent */,
				     NULL /* ns */,
				     "Desktop" /* name */,
				     entry_get_displayname (folder_get_desktop_file (folder)) /* content */);
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

	for (li = folder->subfolders; li != NULL; li = li->next) {
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

	for (li = info->merge_dirs; li != NULL; li = li->next) {
		ItemDir *merge_dir = li->data;
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "MergeDir" /* name */,
			     merge_dir->uri /* content */);
	}
	
	for (li = info->item_dirs; li != NULL; li = li->next) {
		ItemDir *item_dir = li->data;
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
static void
vfolder_info_write_user (VFolderInfo *info)
{
	xmlDoc *doc;

	if (info->inhibit_write > 0)
		return;

	if (info->filename == NULL)
		return;

	doc = xml_tree_from_vfolder (info);
	if (doc == NULL)
		return;

	/* FIXME: errors, anyone? */
#if 0
	ensure_dir (info->filename,
		    TRUE /* ignore_basename */);
#endif

	xmlSaveFormatFile (info->filename, doc, TRUE /* format */);
	/* not as good as a stat, but cheaper ... hmmm what is
	 * the likelyhood of this not being the same as ctime */
	info->filename_last_write = time (NULL);

	xmlFreeDoc(doc);

	info->dirty = FALSE;

	info->modification_time = time (NULL);
}

static void
filename_monitor_cb (GnomeVFSMonitorHandle *handle,
		     const gchar *monitor_uri,
		     const gchar *info_uri,
		     GnomeVFSMonitorEventType event_type,
		     gpointer user_data)
{
}

static void
write_dir_monitor_cb (GnomeVFSMonitorHandle *handle,
		      const gchar *monitor_uri,
		      const gchar *info_uri,
		      GnomeVFSMonitorEventType event_type,
		      gpointer user_data)
{
}


/* 
 * VFolderInfo Implementation
 */
static void
vfolder_info_init (VFolderInfo *info, const char *scheme)
{
	const char *path;
	GSList *list = NULL;

	g_static_rw_lock_init (&info->rw_lock);

	info->scheme = g_strdup (scheme);
		
	// FIXME: load from gconf
	if (strcmp (scheme, "applications-all-users") == 0) {
		info->filename = g_strconcat (SYSCONFDIR,
					      "/gnome-vfs-2.0/vfolders/",
					      scheme, 
					      ".vfolder-info",
					      NULL);

		info->desktop_dir = g_strdup (DATADIR "/gnome/vfolders/");
		info->write_dir = g_strdup (DATADIR "/applications/");
	} else if (strcmp (scheme, "applications") == 0) {
		info->filename = g_strconcat (g_get_home_dir (),
					      "/" DOT_GNOME "/vfolders/",
					      scheme, 
					      ".vfolder-info",
					      NULL);
		/* default write directory */
		info->write_dir = g_strconcat (g_get_home_dir (),
					       "/" DOT_GNOME "/vfolders/",
					       scheme,
					       NULL);
	}

	info->filename_monitor = 
		vfolder_monitor_file_new (info->filename,
					  filename_monitor_cb,
					  info);
	info->write_dir_monitor = 
		vfolder_monitor_directory_new (info->write_dir,
					       write_dir_monitor_cb,
					       info);

	if (info->desktop_dir) 
		info->desktop_dir_monitor = 
			vfolder_monitor_directory_new (info->desktop_dir,
						       write_dir_monitor_cb,
						       info);

	/* Init the desktop paths */
	list = g_slist_prepend (list, g_strdup ("/usr/share/applications/"));
	if (strcmp ("/usr/share/applications/", DATADIR "/applications/") != 0)
		list = g_slist_prepend (list, 
					g_strdup (DATADIR "/applications/"));

	path = g_getenv ("DESKTOP_FILE_PATH");
	if (path != NULL) {
		int i;
		char **ppath = g_strsplit (path, ":", -1);
		for (i = 0; ppath[i] != NULL; i++) {
			const char *dir = ppath[i];
			list = g_slist_prepend (list, g_strdup (dir));
		}
		g_strfreev (ppath);
	}
	info->item_dirs = g_slist_reverse (list);

	info->entries_ht = g_hash_table_new (g_str_hash, g_str_equal);

	info->root = folder_new (info, "Root");

	/* 
	 * Load all entries in the itemdir and mergedir directories.  Load all
	 * dirs in the config file. Set the directory dirty, and let
	 * folder_set_dirty load all its derived entries, and pull them from the
	 * global pool.  
	 */


	/* 
	 * Set the extend uri for the root folder to the -all-users version of
	 * the scheme, in case the user doesn't have a private .vfolder-info
	 * file yet.  */
	if (!strstr (scheme, "-all-users")) {
		gchar *all_user_scheme;

		all_user_scheme = g_strconcat (scheme, "-all-users:///", NULL);
		folder_set_extend_uri (info->root, all_user_scheme);
		g_free (all_user_scheme);
	}

	info->modification_time = time (NULL);
}

static void
vfolder_info_free_internals (VFolderInfo *info)
{
	GSList *iter;

	if (info == NULL)
		return;

	g_static_rw_lock_free (&info->rw_lock);
	
	if (info->filename_monitor != NULL) {
		vfolder_monitor_cancel (info->filename_monitor);
		info->filename_monitor = NULL;
	}

	if (info->desktop_dir_monitor != NULL) {
		vfolder_monitor_cancel (info->desktop_dir_monitor);
		info->desktop_dir_monitor = NULL;
	}

	if (info->write_dir_monitor != NULL) {
		vfolder_monitor_cancel (info->write_dir_monitor);
		info->write_dir_monitor = NULL;
	}

	for (iter = info->item_dirs; iter; iter = iter->next) {
		ItemDir *dir = iter->data;
		itemdir_free (dir);
	}
	info->item_dirs = NULL;

	for (iter = info->merge_dirs; iter; iter = iter->next) {
		ItemDir *dir = iter->data;
		itemdir_free (dir);
	}
	info->merge_dirs = NULL;

	g_free (info->scheme);
	info->scheme = NULL;

	g_free (info->filename);
	info->filename = NULL;

	g_free (info->desktop_dir);
	info->desktop_dir = NULL;

	g_free (info->write_dir);
	info->write_dir = NULL;

	g_slist_foreach (info->entries, (GFunc) entry_unref, NULL);
	g_slist_free (info->entries);
	info->entries = NULL;

	if (info->entries_ht)
		g_hash_table_destroy (info->entries_ht);
	info->entries_ht = NULL;

	folder_unref (info->root);
	info->root = NULL;

	//FIXME requested_monitors

	if (info->reread_queue != 0)
		g_source_remove (info->reread_queue);
	info->reread_queue = 0;
}

static void
vfolder_info_destroy (VFolderInfo *info)
{
	vfolder_info_free_internals (info);
	g_free (info);
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

	//info = g_hash_table_lookup (infos, scheme);
	if (!info) {
		info = g_new0 (VFolderInfo, 1);
		vfolder_info_init (info, scheme);

		if (!vfolder_info_read_info (info, NULL, NULL)) {
			vfolder_info_destroy (info);
			info = NULL;
		}
			//} else
			//g_hash_table_insert (infos, info->scheme, info);
	}

	G_UNLOCK (vfolder_lock);

	return info;
}

void
vfolder_info_set_dirty (VFolderInfo *info)
{
	vfolder_info_write_user (info);
}

static Folder *
get_folder_for_path_list_n (Folder    *parent, 
			    gchar    **paths, 
			    gint       path_index,
			    gboolean   skip_last) 
{
	Folder *child;
	gchar *subname, *subsubname;

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
get_folder_for_path (Folder *root, gchar *path, gboolean skip_last) 
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
vfolder_info_get_parent (VFolderInfo *info, gchar *path)
{
	return get_folder_for_path (info->root, path, TRUE);
}

Folder *
vfolder_info_get_folder (VFolderInfo *info, gchar *path)
{
	return get_folder_for_path (info->root, path, FALSE);
}

Entry *
vfolder_info_get_entry (VFolderInfo *info, gchar *path)
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

GSList *
vfolder_info_list_all_entries (VFolderInfo *info)
{
	return info->entries;
}

void 
vfolder_info_add_entry (VFolderInfo *info, Entry *entry)
{
	info->entries = g_slist_prepend (info->entries, entry);
	g_hash_table_insert (info->entries_ht, 
			     entry_get_filename (entry),
			     entry);
}

void 
vfolder_info_remove_entry (VFolderInfo *info, Entry *entry)
{
	info->entries = g_slist_remove (info->entries, entry);
	g_hash_table_remove (info->entries_ht, 
			     entry_get_filename (entry));

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
