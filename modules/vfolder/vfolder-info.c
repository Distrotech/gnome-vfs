
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-monitor-private.h>
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

static void
folder_create_dot_directory_entry (Folder *folder)
{
	Entry *entry = NULL;
	gchar *dot_directory = folder_get_desktop_file (folder);

	if (strchr (dot_directory, '/')) {
		/* Assume full path or URI */
		entry = entry_new (folder->info, 
				   dot_directory, 
				   ".directory", 
				   FALSE);
	} else {
		gchar *dirpath = NULL;
		gchar *full_path;

		if (folder->info->desktop_dir)
			dirpath = folder->info->desktop_dir;
		else if (folder->info->write_dir)
			dirpath = folder->info->write_dir;

		if (dirpath) {
			full_path = g_build_filename (dirpath,
						      dot_directory, 
						      NULL);
			entry = entry_new (folder->info,
					   full_path,
					   ".directory",
					   TRUE);
			g_free (full_path);
		}
	}

	if (entry)
		folder_add_entry (folder, entry);
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
				folder_create_dot_directory_entry (folder);
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

			if (new_folder != NULL)
				folder_add_subfolder (folder, new_folder);
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
	// FIXME
}

static ItemDir *
itemdir_new (VFolderInfo *info, gchar *uri, gboolean is_mergedir)
{
	ItemDir *ret;
	ret = g_new0 (ItemDir, 1);
	ret->uri = g_strdup (uri);
	ret->is_mergedir = is_mergedir;
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
	GnomeVFSResult my_result;

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
				ItemDir *id = itemdir_new (info, dir, TRUE);

				info->item_dirs = 
					g_slist_append (info->item_dirs, id);
				xmlFree (dir);
			}
		} 
		else if (g_ascii_strcasecmp (node->name, "ItemDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);

			if (dir != NULL) {
				ItemDir *id = itemdir_new (info, dir, FALSE);

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
create_entries_from_mergedir (VFolderInfo     *info,
			      gchar           *uri,
			      gboolean         add_core,
			      GQuark           inherit_keyword)
{
	GnomeVFSResult result;
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSFileInfo *file_info;
	GQuark merged, application, core_quark;

	merged = g_quark_from_static_string ("Merged");
	application = g_quark_from_static_string("Application");
	core_quark = g_quark_from_static_string ("Core");

	result = gnome_vfs_directory_open (&handle, 
					   uri,
					   GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return FALSE;

	file_info = gnome_vfs_file_info_new ();

	while (TRUE) {
		gchar *file_uri;
		Entry *new_entry;

		result = gnome_vfs_directory_read_next (handle, file_info);
		if (result != GNOME_VFS_OK)
			break;

		if (!strcmp (file_info->name, ".") ||
		    !strcmp (file_info->name, ".."))
			continue;

		/* FIXME: drop uniqueness tag from file_info->name */
		file_uri = g_build_filename (uri, file_info->name, NULL);

		if (file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			GQuark pass_keyword = 0;
			
			pass_keyword = get_mergedir_keyword (file_info->name);
			if (!pass_keyword)
				pass_keyword = inherit_keyword;

			create_entries_from_mergedir (info,
						      file_uri,
						      FALSE,
						      pass_keyword);

			goto NEXT_ENTRY;
		}

		if (!check_extension (file_info->name, ".desktop")) 
			goto NEXT_ENTRY;

		if (vfolder_info_lookup_entry (info, file_info->name)) {
			D (g_print ("EXCLUDING DUPLICATE ENTRY: %s\n", 
				    file_uri));
			goto NEXT_ENTRY;
		}

		new_entry = entry_new (info, file_uri, file_info->name, TRUE);
		if (!new_entry)
			goto NEXT_ENTRY;

		/* 
		 * Mergedirs have the 'Merged' and 'Appliction' keywords added.
		 */
		entry_add_implicit_keyword (new_entry, merged);
		entry_add_implicit_keyword (new_entry, application);

		if (add_core)
			entry_add_implicit_keyword (new_entry, core_quark);

		if (inherit_keyword)
			entry_add_implicit_keyword (new_entry, inherit_keyword);

	NEXT_ENTRY:
		g_free (file_uri);
	}

	gnome_vfs_file_info_unref (file_info);
	gnome_vfs_directory_close (handle);
	return TRUE;
}

static gboolean
create_entries_from_itemdir (VFolderInfo     *info,
			     gchar           *uri)
{
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSFileInfo *file_info;
	GnomeVFSResult result;

	result = gnome_vfs_directory_open (&handle, 
					   uri,
					   GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return FALSE;

	file_info = gnome_vfs_file_info_new ();

	while (TRUE) {
		gchar *file_uri;

		result = gnome_vfs_directory_read_next (handle, file_info);
		if (result != GNOME_VFS_OK)
			break;

		if (!strcmp (file_info->name, ".") ||
		    !strcmp (file_info->name, ".."))
			continue;

		/* FIXME: drop uniqueness tag from file_info->name */
		file_uri = g_build_filename (uri, file_info->name, NULL);

		if (file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			create_entries_from_itemdir (info, file_uri);
			goto NEXT_ENTRY;
		}

		if (file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			create_entries_from_itemdir (info, file_uri);
			goto NEXT_ENTRY;
		}

		if (!check_extension (file_info->name, ".desktop"))
			goto NEXT_ENTRY;

		if (vfolder_info_lookup_entry (info, file_info->name)) {
			D (g_print ("EXCLUDING DUPLICATE ENTRY: %s\n", 
				    file_uri));
			goto NEXT_ENTRY;
		}

		entry_new (info, file_uri, file_info->name, TRUE);

	NEXT_ENTRY:
		g_free (file_uri);
	}

	gnome_vfs_file_info_unref (file_info);
	gnome_vfs_directory_close (handle);
	return TRUE;
}

static gboolean
vfolder_info_read_info (VFolderInfo     *info,
			GnomeVFSResult  *result,
			GnomeVFSContext *context)
{
	gboolean ret = FALSE;

	if (!info->filename)
		return FALSE;

	/* Don't let set_dirty write out the file */
	info->inhibit_write = TRUE;

	ret = read_vfolder_from_file (info, 
				      info->filename, 
				      FALSE,
				      result, 
				      context);
	if (ret) {
		GSList *iter;

		/* Load WriteDir entries first */
		if (info->write_dir)
			create_entries_from_itemdir (info, info->write_dir);

		/* Followed by ItemDir/MergeDirs in order of appearance */
		for (iter = info->item_dirs; iter; iter = iter->next) {
			ItemDir *id = iter->data;
			if (id->is_mergedir)
				create_entries_from_mergedir (info, 
							      id->uri, 
							      TRUE,
							      0);
			else
				create_entries_from_itemdir (info, id->uri);
		}
	}

	/* Allow set_dirty to write config file again */
	info->inhibit_write = FALSE;

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
	const GSList *li;
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
			gchar *desktop_file = folder_get_desktop_file (folder);

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
		else
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

	if (info->inhibit_write)
		return;

	if (!info->filename)
		return;

	doc = xml_tree_from_vfolder (info);
	if (!doc)
		return;

	/* FIXME: errors, anyone? */
#if 0
	ensure_dir (info->filename,
		    TRUE /* ignore_basename */);
#endif

	xmlSaveFormatFile (info->filename, doc, TRUE /* format */);

	xmlFreeDoc(doc);

	info->dirty = FALSE;

	info->modification_time = time (NULL);
}

static void
vfolder_info_reset (VFolderInfo *info)
{
	GSList *iter;
	
	if (info->filename_monitor) {
		vfolder_monitor_cancel (info->filename_monitor);
		info->filename_monitor = NULL;
	}

	if (info->desktop_dir_monitor) {
		vfolder_monitor_cancel (info->desktop_dir_monitor);
		info->desktop_dir_monitor = NULL;
	}

	if (info->write_dir_monitor) {
		vfolder_monitor_cancel (info->write_dir_monitor);
		info->write_dir_monitor = NULL;
	}

	for (iter = info->item_dirs; iter; iter = iter->next) {
		ItemDir *dir = iter->data;
		itemdir_free (dir);
	}
	g_slist_free (info->item_dirs);
	info->item_dirs = NULL;

	g_free (info->filename);
	g_free (info->desktop_dir);
	g_free (info->write_dir);

	info->filename = NULL;
	info->desktop_dir = NULL;
	info->write_dir = NULL;

	g_slist_foreach (info->entries, (GFunc) entry_unref, NULL);
	g_slist_free (info->entries);
	info->entries = NULL;

	if (info->entries_ht) {
		g_hash_table_destroy (info->entries_ht);
		info->entries_ht = NULL;
	}

	folder_unref (info->root);
	info->root = NULL;
}

static void
filename_monitor_cb (GnomeVFSMonitorHandle *handle,
		     const gchar *monitor_uri,
		     const gchar *info_uri,
		     GnomeVFSMonitorEventType event_type,
		     gpointer user_data)
{
	VFolderInfo *info =  user_data;

	// FIXME

	vfolder_info_reset (info);
	vfolder_info_read_info (info, NULL, NULL);
}

static void
write_dir_monitor_cb (GnomeVFSMonitorHandle *handle,
		      const gchar *monitor_uri,
		      const gchar *info_uri,
		      GnomeVFSMonitorEventType event_type,
		      gpointer user_data)
{
	VFolderInfo *info =  user_data;

	// FIXME

	vfolder_info_reset (info);
	vfolder_info_read_info (info, NULL, NULL);
}


/* 
 * VFolderInfo Implementation
 */
static void
vfolder_info_init (VFolderInfo *info, const char *scheme)
{
	const char *path;
	GSList *list = NULL;
	ItemDir *id;

	g_static_rw_lock_init (&info->rw_lock);

	info->scheme = g_strdup (scheme);
	info->root = folder_new (info, "Root");

	// FIXME: load from gconf

	if (strstr (scheme, "-all-users")) {
		char *base_scheme;

		base_scheme = 
			g_strndup (scheme, 
				   strlen (scheme) - strlen ("-all-users"));
		info->filename = g_strconcat (SYSCONFDIR,
					      "/gnome-vfs-2.0/vfolders/",
					      base_scheme, 
					      ".vfolder-info",
					      NULL);
		g_free (base_scheme);

		path = g_getenv ("GNOME2_PATH");
		if (path) {
			int i;
			char *dir;
			char **ppath = g_strsplit (path, ":", -1);

			for (i = 0; ppath[i] != NULL; i++) {
				dir = g_build_filename (ppath[i], 
							"/share/applications/",
							NULL);
				id = itemdir_new (info, dir, FALSE);
				g_free (dir);

				list = g_slist_prepend (list, id);
			}
			g_strfreev (ppath);
		}

		info->item_dirs = g_slist_reverse (list);
	} else {
		gchar *all_user_scheme;

		/* 
		 * Set the extend uri for the root folder to the -all-users
		 * version of the scheme, in case the user doesn't have a
		 * private .vfolder-info file yet. 
		 */
		all_user_scheme = g_strconcat (scheme, "-all-users:///", NULL);
		folder_set_extend_uri (info->root, all_user_scheme);
		g_free (all_user_scheme);

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
	if (info->write_dir)
		info->write_dir_monitor = 
			vfolder_monitor_directory_new (info->write_dir,
						       write_dir_monitor_cb,
						       info);

	if (info->desktop_dir) 
		info->desktop_dir_monitor = 
			vfolder_monitor_directory_new (info->desktop_dir,
						       write_dir_monitor_cb,
						       info);

	info->entries_ht = g_hash_table_new (g_str_hash, g_str_equal);

	/* 
	 * Load all entries in the itemdir and mergedir directories.  Load all
	 * dirs in the config file. Set the directory dirty, and let
	 * folder_set_dirty load all its derived entries, and pull them from the
	 * global pool.  
	 */

	info->modification_time = time (NULL);
}

static void
vfolder_info_destroy (VFolderInfo *info)
{
	if (info == NULL)
		return;

	vfolder_info_reset (info);

	g_static_rw_lock_free (&info->rw_lock);

	g_free (info->scheme);

	while (info->requested_monitors) {
		GnomeVFSMethodHandle *monitor = info->requested_monitors->data;
		vfolder_info_cancel_monitor (monitor);
	}

	g_free (info);
}

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
	if (!info) {
		info = g_new0 (VFolderInfo, 1);
		vfolder_info_init (info, scheme);

		if (!vfolder_info_read_info (info, NULL, NULL)) {
			D (g_print ("DESTROYING INFO FOR SCHEME: %s\n", 
				    scheme));
			vfolder_info_destroy (info);
			info = NULL;
		} else
			g_hash_table_insert (infos, info->scheme, info);

		G_UNLOCK (vfolder_lock);
		
		VFOLDER_INFO_READ_LOCK (info);

		/* vfolder_info_dump_entries (info, 4); */
		load_folders (info->root);

		VFOLDER_INFO_READ_UNLOCK (info);
	} else
		G_UNLOCK (vfolder_lock);

	return info;
}

void
vfolder_info_set_dirty (VFolderInfo *info)
{
	if (info->inhibit_write)
		return;

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

Entry *
vfolder_info_lookup_entry (VFolderInfo *info, gchar *name)
{
	return g_hash_table_lookup (info->entries_ht, name);
}

void 
vfolder_info_add_entry (VFolderInfo *info, Entry *entry)
{
	info->entries = g_slist_prepend (info->entries, entry);
	g_hash_table_insert (info->entries_ht, 
			     entry_get_displayname (entry),
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
			  char                     *path,
			  GnomeVFSMonitorEventType  event_type)
{
	GSList *iter;
	GnomeVFSURI *uri;
	gchar *uristr;

	uristr = g_strconcat (info->scheme, "://", path, NULL);
	uri = gnome_vfs_uri_new (uristr);
	g_free (uristr);

	for (iter = info->requested_monitors; iter; iter = iter->next) {
		MonitorHandle *handle = iter->data;
		
		if (!strncmp (path, handle->path, strlen (handle->path))) {
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
			  gchar                 *path,
			  GnomeVFSMethodHandle **handle)
{
	MonitorHandle *monitor = g_new0 (MonitorHandle, 1);
	monitor->path = g_strdup (path);
	monitor->info = info;

	info->requested_monitors = g_slist_prepend (info->requested_monitors,
						    monitor);
	
	*handle = (GnomeVFSMethodHandle *) monitor;
}

void 
vfolder_info_cancel_monitor (GnomeVFSMethodHandle  *handle)
{
	MonitorHandle *monitor = (MonitorHandle *) handle;

	monitor->info->requested_monitors = 
		g_slist_remove (monitor->info->requested_monitors, monitor);

	g_free (monitor->path);
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
