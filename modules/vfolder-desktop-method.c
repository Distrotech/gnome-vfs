/* -*- Mode: C; tab-width: 8; indent-tabs-mode: 8; c-basic-offset: 8 -*- */

/* vfolder-desktop-method.c

   Copyright (C) 2001 Red Hat, Inc.
   Copyright (C) 2001 The Dark Prince

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

/* URI scheme for reading the "programs:" vfolder.  Lots of code stolen
 * from the original desktop reading URI scheme.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* Debugging foo: */
/*#define D(x) x */
#define D(x) ;

#include <glib.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <libgnomevfs/gnome-vfs-mime.h>

#include <libgnomevfs/gnome-vfs-module.h>
#include <libgnomevfs/gnome-vfs-method.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>

typedef struct _VFolderInfo VFolderInfo;
typedef struct _Query Query;
typedef struct _QueryKeyword QueryKeyword;
typedef struct _QueryFilename QueryFilename;
typedef struct _Entry Entry;
typedef struct _Folder Folder;
typedef struct _EntryFile EntryFile;
typedef struct _Keyword Keyword;

/* FIXME Maybe when chaining to file:, we should call the gnome-vfs wrapper
 * functions, instead of the file: methods directly.
 */
/* FIXME: support cancellations */

static GnomeVFSMethod *parent_method = NULL;

static GHashTable *infos = NULL;

/* Note: all keywords are quarks */
/* Note: basenames are unique */


enum {
	QUERY_OR,
	QUERY_AND,
	QUERY_KEYWORD,
	QUERY_FILENAME
};

struct _Query {
	int type;
	gboolean not;
	GSList *queries;
};

struct _QueryKeyword {
	int type;
	gboolean not;
	GQuark keyword;
};

struct _QueryFilename {
	int type;
	gboolean not;
	char *filename;
};

enum {
	ENTRY_FILE,
	ENTRY_FOLDER
};

struct _Entry {
	int type;
	int refcount;
	char *name;
};

struct _EntryFile {
	Entry entry;

	char *filename;
	gboolean per_user;
	GSList *keywords;

	gboolean implicit_keywords; /* the keywords were added by us */
};

struct _Folder {
	Entry entry;

	Folder *parent;

	char *desktop_file; /* the .directory file */

	Query *query;

	/* The following is for per file
	 * access */
	/* excluded by filename */
	GHashTable *excludes;
	/* included by filename */
	GSList *includes;

	GSList *subfolders;

	/* Some flags */
	gboolean read_only;
	gboolean dont_show_if_empty;

	/* lazily done, will run query only when it
	 * needs to */
	gboolean up_to_date;
	gboolean sorted;
	GSList *entries;
};

struct _VFolderInfo
{
	char *scheme;

	char *filename;
	char *user_filename;
	char *desktop_dir; /* directory with .directorys */
	char *user_desktop_dir; /* directory with .directorys */

	GSList *item_dirs;
	char *user_item_dir; /* dir where user changes to
				items are stored */

	/* old style dirs to merge in */
	GSList *merge_dirs;

	GSList *entries;

	/* entry hash by basename */
	GHashTable *entries_ht;

	/* The root folder */
	Folder *root;

	/* some flags */
	gboolean read_only;

	gboolean dirty;

	int inhibit_write;
};

#define ALL_SCHEME_P(scheme)	((scheme) != NULL && strncmp ((scheme), "all-", 4) == 0)

static Entry *	entry_ref	(Entry *entry);
static void	entry_unref	(Entry *entry);
static void	query_destroy	(Query *query);
/* An EVIL function for quick reading of .desktop files,
 * only reads in one or two keys, but that's ALL we need */
static void	readitem_entry	(const char *filename,
				 const char *key1,
				 char **result1,
				 const char *key2,
				 char **result2);

static gboolean
check_ext (const char *name, const char *ext_check)
{
	const char *ext;

	ext = strrchr (name, '.');
	if (ext == NULL ||
	    strcmp (ext, ext_check) != 0)
		return FALSE;
	else
		return TRUE;
}

static gboolean
ensure_dir (const char *dirname, gboolean ignore_basename)
{
	char *parsed, *p;

	if (dirname == NULL)
		return FALSE;

	if (ignore_basename)
		parsed = g_path_get_dirname (dirname);
	else
		parsed = g_strdup (dirname);

	if (g_file_test (parsed, G_FILE_TEST_IS_DIR)) {
		g_free (parsed);
		return TRUE;
	}

	p = strchr (parsed, '/');
	if (p == parsed)
		p = strchr (p+1, '/');

	while (p != NULL) {
		*p = '\0';
		if (mkdir (parsed, 0700) != 0 &&
		    errno != EEXIST) {
			g_free (parsed);
			return FALSE;
		}
		*p = '/';
		p = strchr (p+1, '/');
	}

	if (mkdir (parsed, 0700) != 0 &&
	    errno != EEXIST) {
		g_free (parsed);
		return FALSE;
	}

	g_free (parsed);
	return TRUE;
}

static char *
get_basename (GnomeVFSURI *uri)
{
	const char *path = gnome_vfs_uri_get_path (uri);

	return g_path_get_basename (path);
}

static void
destroy_entry_file (EntryFile *efile)
{
	if (efile == NULL)
		return;

	g_free (efile->filename);
	efile->filename = NULL;

	g_slist_free (efile->keywords);
	efile->keywords = NULL;

	g_free (efile);
}

static void
destroy_folder (Folder *folder)
{
	GSList *list;

	if (folder == NULL)
		return;

	if (folder->parent != NULL) {
		folder->parent->subfolders =
			g_slist_remove (folder->parent->subfolders, folder);
		folder->parent->up_to_date = FALSE;
		folder->parent = NULL;
	}

	g_free (folder->desktop_file);
	folder->desktop_file = NULL;

	query_destroy (folder->query);
	folder->query = NULL;

	if (folder->excludes != NULL) {
		g_hash_table_destroy (folder->excludes);
		folder->excludes = NULL;
	}

	g_slist_foreach (folder->includes, (GFunc)g_free, NULL);
	g_slist_free (folder->includes);
	folder->includes = NULL;

	list = folder->subfolders;
	folder->subfolders = NULL;
	g_slist_foreach (list, (GFunc)entry_unref, NULL);
	g_slist_free (list);

	list = folder->entries;
	folder->entries = NULL;
	g_slist_foreach (list, (GFunc)entry_unref, NULL);
	g_slist_free (list);

	g_free (folder);
}

static Entry *
entry_ref (Entry *entry)
{
	if (entry != NULL)
		entry->refcount++;
	return entry;
}

static void
entry_unref (Entry *entry)
{
	if (entry == NULL)
		return;

	entry->refcount--;

	if (entry->refcount == 0) {
		g_free (entry->name);
		entry->name = NULL;
		if (entry->type == ENTRY_FILE)
			destroy_entry_file ((EntryFile *)entry);
		else /* ENTRY_FOLDER */
			destroy_folder ((Folder *)entry);
	}
}

/* Handles ONLY files, not dirs */
static GSList *
entries_from_files (VFolderInfo *info, GSList *filenames)
{
	GSList *li;
	GSList *files;

	files = NULL;
	for (li = filenames; li != NULL; li = li->next) {
		char *filename = li->data;
		GSList *entry_list = g_hash_table_lookup (info->entries_ht, filename);
		if (entry_list != NULL)
			files = g_slist_prepend (files,
						 entry_ref (entry_list->data));
	}

	return g_slist_reverse (files);
}

static gboolean
matches_query (EntryFile *efile, Query *query)
{
	GSList *li;
#define INVERT_IF_NEEDED(val) (query->not ? !(val) : (val))
	switch (query->type) {
	case QUERY_OR:
		for (li = query->queries; li != NULL; li = li->next) {
			Query *subquery = li->data;
			if (matches_query (efile, subquery))
				return INVERT_IF_NEEDED (TRUE);
		}
		return INVERT_IF_NEEDED (FALSE);
	case QUERY_AND:
		for (li = query->queries; li != NULL; li = li->next) {
			Query *subquery = li->data;
			if ( ! matches_query (efile, subquery))
				return INVERT_IF_NEEDED (FALSE);
		}
		return INVERT_IF_NEEDED (TRUE);
	case QUERY_KEYWORD:
		{
			QueryKeyword *qkeyword = (QueryKeyword *)query;
			for (li = efile->keywords; li != NULL; li = li->next) {
				GQuark keyword = GPOINTER_TO_INT (li->data);
				if (keyword == qkeyword->keyword)
					return INVERT_IF_NEEDED (TRUE);
			}
			return INVERT_IF_NEEDED (FALSE);
		}
	case QUERY_FILENAME:
		{
			QueryFilename *qfilename = (QueryFilename *)query;
			if (strcmp (qfilename->filename, ((Entry *)efile)->name) == 0) {
				return INVERT_IF_NEEDED (TRUE);
			} else {
				return INVERT_IF_NEEDED (FALSE);
			}
		}
	}
#undef INVERT_IF_NEEDED
	g_assert_not_reached ();
	/* huh? */
	return FALSE;
}

/* Run query */
static GSList *
run_query (VFolderInfo *info, GSList *result, Query *query)
{
	GSList *li;

	if (query == NULL)
		return result;

	for (li = info->entries; li != NULL; li = li->next) {
		Entry *entry = li->data;
		if (entry->type != ENTRY_FILE)
			continue;
		if (matches_query ((EntryFile *)entry, query))
			result = g_slist_prepend (result, entry_ref (entry));
	}

	return result;
}

/* get entries in folder */
static void
ensure_folder (VFolderInfo *info, Folder *folder)
{
	if (folder->up_to_date)
		return;

	/* Include includes */
	g_slist_free (folder->entries);
	folder->entries = entries_from_files (info, folder->includes);

	/* Run query */
	folder->entries = run_query (info, folder->entries, folder->query);

	/* Include subfolders */
	/* we always whack them onto the beginning */
	if (folder->subfolders != NULL) {
		GSList *subfolders = g_slist_copy (folder->subfolders);
		g_slist_foreach (subfolders, (GFunc)entry_ref, NULL);
		folder->entries = g_slist_concat (subfolders, folder->entries);
	}

	/* Exclude excludes */
	if (folder->excludes != NULL) {
		GSList *li;
		GSList *entries = folder->entries;
	       	folder->entries = NULL;
		for (li = entries; li != NULL; li = li->next) {
			Entry *entry = li->data;
			if (g_hash_table_lookup (folder->excludes, entry->name) == NULL)
				folder->entries = g_slist_prepend (folder->entries, entry);
			else
				entry_unref (entry);

		}
		g_slist_free (entries);

		/* to preserve the Folders then everything else order */
		folder->entries = g_slist_reverse (folder->entries);
	}

	folder->up_to_date = TRUE;
	/* not yet sorted */
	folder->sorted = FALSE;
}

static char *
get_directory_file (VFolderInfo *info, Folder *folder)
{
	char *filename;

	/* FIXME: cache dir_files */

	if (folder->desktop_file == NULL)
		return NULL;

	if (folder->desktop_file[0] == G_DIR_SEPARATOR)
		return g_strdup (folder->desktop_file);

	/* Now try the user directory */
	if (info->user_desktop_dir != NULL) {
		filename = g_build_filename (info->user_desktop_dir,
					     folder->desktop_file,
					     NULL);
		if (access (filename, F_OK) == 0)
			return filename;

		g_free (filename);
	}

	filename = g_build_filename (info->desktop_dir,
				     folder->desktop_file,
				     NULL);
	if (access (filename, F_OK) == 0)
		return filename;
	g_free (filename);

	return NULL;
}

static GSList *
get_sort_order (VFolderInfo *info, Folder *folder)
{
	GSList *list;
	char **parsed;
	char *order;
	int i;
	char *filename;

	filename = get_directory_file (info, folder);
	if (filename == NULL)
		return NULL;

	order = NULL;
	readitem_entry (filename,
			"SortOrder",
			&order,
			NULL,
			NULL);
	g_free (filename);

	if (order == NULL)
		return NULL;

	parsed = g_strsplit (order, ":", -1);

	g_free (order);

	list = NULL;
	for (i = 0; parsed[i] != NULL; i++) {
		char *word = parsed[i];
		/* steal */
		parsed[i] = NULL;
		/* ignore empty */
		if (word[0] == '\0') {
			g_free (word);
			continue;
		}
		list = g_slist_prepend (list, word);
	}
	/* we've stolen all strings from it */
	g_free (parsed);

	return g_slist_reverse (list);
}

/* get entries in folder */
static void
ensure_folder_sort (VFolderInfo *info, Folder *folder)
{
	GSList *li, *sort_order;
	GSList *entries;
	GHashTable *entry_hash;

	ensure_folder (info, folder);
	if (folder->sorted)
		return;

	sort_order = get_sort_order (info, folder);
	if (sort_order == NULL) {
		folder->sorted = TRUE;
		return;
	}

	entries = folder->entries;
	folder->entries = NULL;

	entry_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (li = entries; li != NULL; li = li->next) {
		Entry *entry = li->data;
		g_hash_table_insert (entry_hash, entry->name, li);
	}

	for (li = sort_order; li != NULL; li = li->next) {
		char *word = li->data;
		GSList *entry_list;
		Entry *entry;

		/* we kill the words here */
		li->data = NULL;

		entry_list = g_hash_table_lookup (entry_hash, word);
		g_free (word);

		if (entry_list == NULL)
			continue;

		entry = entry_list->data;

		entries = g_slist_remove_link (entries, entry_list);
		g_slist_free_1 (entry_list);

		folder->entries = g_slist_prepend (folder->entries,
						   entry);
	}

	/* put on those that weren't mentioned in the sort */
	for (li = entries; li != NULL; li = li->next) {
		Entry *entry = li->data;

		folder->entries = g_slist_prepend (folder->entries,
						   entry);
	}

	g_hash_table_destroy (entry_hash);
	g_slist_free (entries);
	g_slist_free (sort_order);

	folder->sorted = TRUE;
}

static EntryFile *
file_new (const char *name)
{
	EntryFile *efile = g_new0 (EntryFile, 1);

	efile->entry.type = ENTRY_FILE;
	efile->entry.name = g_strdup (name);
	efile->entry.refcount = 1;

	return efile;
}

static Folder *
folder_new (const char *name)
{
	Folder *folder = g_new0 (Folder, 1);

	folder->entry.type = ENTRY_FOLDER;
	folder->entry.name = g_strdup (name);
	folder->entry.refcount = 1;

	return folder;
}

static Query *
query_new (int type)
{
	Query *query;

	if (type == QUERY_KEYWORD)
		query = (Query *)g_new0 (QueryKeyword, 1);
	else if (type == QUERY_FILENAME)
		query = (Query *)g_new0 (QueryFilename, 1);
	else
		query = g_new0 (Query, 1);

	query->type = type;

	return query;
}

static void
query_destroy (Query *query)
{
	if (query == NULL)
		return;

	if (query->type == QUERY_FILENAME) {
		QueryFilename *qfile = (QueryFilename *)query;
		g_free (qfile->filename);
		qfile->filename = NULL;
	} else if (query->type == QUERY_OR ||
		   query->type == QUERY_AND) {
		g_slist_foreach (query->queries, (GFunc)query_destroy, NULL);
		g_slist_free (query->queries);
		query->queries = NULL;
	}

	g_free (query);
}

static void
vfolder_info_init (VFolderInfo *info, const char *scheme)
{
	const char *path;
	GSList *list;

	info->scheme = g_strdup (scheme);

	/* Init for programs: */
	info->filename = g_strconcat (SYSCONFDIR,
				      "/gnome-vfs-2.0/vfolders/",
				      scheme, ".vfolder-info",
				      NULL);
	info->user_filename = g_strconcat (g_get_home_dir (),
					   "/.gnome/vfolders/",
					   scheme, ".vfolder-info",
					   NULL);
	info->desktop_dir = g_strconcat (SYSCONFDIR,
					 "/gnome-vfs-2.0/vfolders/",
					 NULL);
	info->user_desktop_dir = g_strconcat (g_get_home_dir (),
					      "/.gnome/vfolders/",
					      NULL);

	/* Init the desktop paths */
	list = NULL;
	list = g_slist_prepend (list, g_strdup ("/usr/share/applications/"));
	if (strcmp ("/usr/share/applications/", DATADIR "/applications/") != 0)
		list = g_slist_prepend (list, g_strdup (DATADIR "/applications/"));
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

	info->user_item_dir = g_strconcat (g_get_home_dir (),
					   "/.gnome/vfolders/",
					   scheme,
					   NULL);

	info->entries_ht = g_hash_table_new (g_str_hash, g_str_equal);

	info->root = folder_new ("Root");
}

static void
vfolder_info_destroy (VFolderInfo *info)
{
	if (info == NULL)
		return;

	g_free (info->scheme);
	info->scheme = NULL;

	g_free (info->filename);
	info->filename = NULL;

	g_free (info->user_filename);
	info->user_filename = NULL;

	g_free (info->desktop_dir);
	info->desktop_dir = NULL;

	g_free (info->user_desktop_dir);
	info->user_desktop_dir = NULL;

	g_slist_foreach (info->item_dirs, (GFunc)g_free, NULL);
	g_slist_free (info->item_dirs);
	info->item_dirs = NULL;

	g_free (info->user_item_dir);
	info->user_item_dir = NULL;

	g_slist_foreach (info->merge_dirs, (GFunc)g_free, NULL);
	g_slist_free (info->merge_dirs);
	info->merge_dirs = NULL;

	g_slist_foreach (info->entries, (GFunc)entry_unref, NULL);
	g_slist_free (info->entries);
	info->entries = NULL;

	g_hash_table_destroy (info->entries_ht);
	info->entries_ht = NULL;

	entry_unref ((Entry *)info->root);
	info->root = NULL;

	g_free (info);
}

static Query *
single_query_read (xmlNode *qnode)
{
	Query *query;
	xmlNode *node;

	if (qnode->type != XML_ELEMENT_NODE ||
	    qnode->name == NULL)
		return NULL;

	query = NULL;

	if (g_ascii_strcasecmp (qnode->name, "Not") == 0 &&
	    qnode->xmlChildrenNode != NULL) {
		xmlNode *iter;
		query = NULL;
		for (iter = qnode->xmlChildrenNode;
		     iter != NULL && query == NULL;
		     iter = iter->next)
			query = single_query_read (iter);
		if (query != NULL) {
			query->not = ! query->not;
		}
		return query;
	} else if (g_ascii_strcasecmp (qnode->name, "Keyword") == 0) {
		xmlChar *word = xmlNodeGetContent (qnode);
		if (word != NULL) {
			query = query_new (QUERY_KEYWORD);
			((QueryKeyword *)query)->keyword =
				g_quark_from_string (word);

			xmlFree (word);
		}
		return query;
	} else if (g_ascii_strcasecmp (qnode->name, "Filename") == 0) {
		xmlChar *file = xmlNodeGetContent (qnode);
		if (file != NULL) {
			query = query_new (QUERY_FILENAME);
			((QueryFilename *)query)->filename =
				g_strdup (file);

			xmlFree (file);
		}
		return query;
	} else if (g_ascii_strcasecmp (qnode->name, "Or") == 0) {
		query = query_new (QUERY_OR);
	} else if (g_ascii_strcasecmp (qnode->name, "And") == 0) {
		query = query_new (QUERY_AND);
	} else {
		/* We don't understand */
		return NULL;
	}

	/* This must be OR or AND */
	g_assert (query != NULL);

	for (node = qnode->xmlChildrenNode; node != NULL; node = node->next) {
		Query *new_query = single_query_read (node);

		if (new_query != NULL)
			query->queries = g_slist_prepend
				(query->queries, new_query);
	}

	query->queries = g_slist_reverse (query->queries);

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
		(*query)->queries = 
			g_slist_append ((*query)->queries, old_query);
		(*query)->queries = 
			g_slist_append ((*query)->queries, new_query);
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
folder_read (xmlNode *fnode)
{
	Folder *folder;
	xmlNode *node;

	folder = folder_new (NULL);

	for (node = fnode->xmlChildrenNode; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE ||
		    node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "Name") == 0) {
			xmlChar *name = xmlNodeGetContent (node);
			if (name != NULL) {
				g_free (folder->entry.name);
				folder->entry.name = g_strdup (name);
				xmlFree (name);
			}
		} else if (g_ascii_strcasecmp (node->name, "Desktop") == 0) {
			xmlChar *desktop = xmlNodeGetContent (node);
			if (desktop != NULL) {
				g_free (folder->desktop_file);
				folder->desktop_file = g_strdup (desktop);
				xmlFree (desktop);
			}
		} else if (g_ascii_strcasecmp (node->name, "Include") == 0) {
			xmlChar *file = xmlNodeGetContent (node);
			if (file != NULL) {
				folder->includes = g_slist_prepend
					(folder->includes, g_strdup (file));
				xmlFree (file);
			}
		} else if (g_ascii_strcasecmp (node->name, "Exclude") == 0) {
			xmlChar *file = xmlNodeGetContent (node);
			if (file != NULL) {
				char *s;
				if (folder->excludes == NULL) {
					folder->excludes = g_hash_table_new_full
						(g_str_hash,
						 g_str_equal,
						 (GDestroyNotify)g_free,
						 NULL);
				}
				s = g_strdup (file);
				g_hash_table_replace (folder->excludes, s, s);
				xmlFree (file);
			}
		} else if (g_ascii_strcasecmp (node->name, "Query") == 0) {
			Query *query = query_read (node);
			if (query != NULL) {
				if (folder->query != NULL)
					query_destroy (folder->query);
				folder->query = query;
			}
		} else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *new_folder = folder_read (node);
			if (new_folder != NULL) {
				folder->subfolders =
					g_slist_append (folder->subfolders,
							new_folder);
				new_folder->parent = folder;
			}
		} else if (g_ascii_strcasecmp (node->name, "ReadOnly") == 0) {
			folder->read_only = TRUE;
		} else if (g_ascii_strcasecmp (node->name,
					       "DontShowIfEmpty") == 0) {
			folder->dont_show_if_empty = TRUE;
		}
	}

	/* Name is required */
	if (folder->entry.name == NULL) {
		entry_unref ((Entry *)folder);
		folder = NULL;
	}

	folder->includes = g_slist_reverse (folder->includes);

	return folder;
}

static char *
subst_home (const char *dir)
{
	if (dir[0] == '~')
		return g_strconcat (g_get_home_dir (),
				    &dir[1],
				    NULL);
	else	
		return g_strdup (dir);
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
vfolder_info_read_info (VFolderInfo *info)
{
	xmlDoc *doc;
	xmlNode *node;
	gboolean got_a_vfolder_dir = FALSE;

	doc = NULL;
	if (info->user_filename != NULL &&
	    access (info->user_filename, F_OK) == 0)
		doc = xmlParseFile (info->user_filename); 
	if (doc == NULL &&
	    access (info->filename, F_OK) == 0)
		doc = xmlParseFile (info->filename); 

	if (doc == NULL
	    || doc->xmlRootNode == NULL
	    || doc->xmlRootNode->name == NULL
	    || g_ascii_strcasecmp (doc->xmlRootNode->name, "VFolderInfo") != 0) {
		xmlFreeDoc(doc);
		return;
	}

	for (node = doc->xmlRootNode->xmlChildrenNode; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE ||
		    node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "MergeDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);
			if (dir != NULL) {
				info->merge_dirs = g_slist_append (info->merge_dirs,
								   g_strdup (dir));
				xmlFree (dir);
			}
		} else if (g_ascii_strcasecmp (node->name, "ItemDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);
			if (dir != NULL) {
				if ( ! got_a_vfolder_dir) {
					g_slist_foreach (info->item_dirs,
							 (GFunc)g_free, NULL);
					g_slist_free (info->item_dirs);
					info->item_dirs = NULL;
				}
				got_a_vfolder_dir = TRUE;
				info->item_dirs = g_slist_append (info->item_dirs,
								  g_strdup (dir));
				xmlFree (dir);
			}
		} else if (g_ascii_strcasecmp (node->name, "UserItemDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);
			if (dir != NULL) {
				g_free (info->user_item_dir);
				info->user_item_dir = subst_home (dir);
				xmlFree (dir);
			}
		} else if (g_ascii_strcasecmp (node->name, "DesktopDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);
			if (dir != NULL) {
				g_free (info->desktop_dir);
				info->desktop_dir = g_strdup (dir);
				xmlFree (dir);
			}
		} else if (g_ascii_strcasecmp (node->name, "UserDesktopDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);
			if (dir != NULL) {
				g_free (info->user_desktop_dir);
				info->user_desktop_dir = subst_home (dir);
				xmlFree (dir);
			}
		} else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *folder = folder_read (node);
			if (folder != NULL) {
				if (info->root != NULL)
					entry_unref ((Entry *)info->root);
				info->root = folder;
			}
		} else if (g_ascii_strcasecmp (node->name, "ReadOnly") == 0) {
			info->read_only = TRUE;
		}
	}

	xmlFreeDoc(doc);
}

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
		QueryKeyword *qkeyword = (QueryKeyword *)query;
		const char *string = g_quark_to_string (qkeyword->keyword);

		xmlNewChild (real_parent /* parent */,
			     NULL /* ns */,
			     "Keyword" /* name */,
			     string /* content */);
	} else if (query->type == QUERY_FILENAME) {
		QueryFilename *qfilename = (QueryFilename *)query;

		xmlNewChild (real_parent /* parent */,
			     NULL /* ns */,
			     "Filename" /* name */,
			     qfilename->filename /* content */);
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

		for (li = query->queries; li != NULL; li = li->next) {
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
	GSList *li;
	xmlNode *folder_node;


	folder_node = xmlNewChild (parent /* parent */,
				   NULL /* ns */,
				   "Folder" /* name */,
				   NULL /* content */);

	xmlNewChild (folder_node /* parent */,
		     NULL /* ns */,
		     "Name" /* name */,
		     folder->entry.name /* content */);

	if (folder->desktop_file != NULL) {
		xmlNewChild (folder_node /* parent */,
			     NULL /* ns */,
			     "Desktop" /* name */,
			     folder->desktop_file /* content */);
	}

	for (li = folder->subfolders; li != NULL; li = li->next) {
		Folder *subfolder = li->data;
		add_xml_tree_from_folder (folder_node, subfolder);
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

	if (folder->query != NULL) {
		xmlNode *query_node;
		query_node = xmlNewChild (folder_node /* parent */,
					  NULL /* ns */,
					  "Query" /* name */,
					  NULL /* content */);

		add_xml_tree_from_query (query_node, folder->query);
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
		const char *merge_dir = li->data;
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "MergeDir" /* name */,
			     merge_dir /* content */);
	}
	
	for (li = info->item_dirs; li != NULL; li = li->next) {
		const char *item_dir = li->data;
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "ItemDir" /* name */,
			     item_dir /* content */);
	}

	if (info->user_item_dir != NULL) {
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "UserItemDir" /* name */,
			     info->user_item_dir /* content */);
	}

	if (info->desktop_dir != NULL) {
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "DesktopDir" /* name */,
			     info->desktop_dir /* content */);
	}

	if (info->user_desktop_dir != NULL) {
		xmlNewChild (topnode /* parent */,
			     NULL /* ns */,
			     "UserDesktopDir" /* name */,
			     info->user_desktop_dir /* content */);
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

	if (info->user_filename == NULL)
		return;

	doc = xml_tree_from_vfolder (info);
	if (doc == NULL)
		return;

	/* FIXME: errors, anyone? */
	ensure_dir (info->user_filename,
		    TRUE /* ignore_basename */);

	xmlSaveFormatFile (info->user_filename, doc, TRUE /* format */);

	xmlFreeDoc(doc);

	info->dirty = FALSE;
}

/* An EVIL function for quick reading of .desktop files,
 * only reads in one or two keys, but that's ALL we need */
static void
readitem_entry (const char *filename,
		const char *key1,
		char **result1,
		const char *key2,
		char **result2)
{
	FILE *fp;
	char buf[1024];
	int keylen1, keylen2;

	*result1 = NULL;
	if (result2 != NULL)
		*result2 = NULL;

	fp = fopen (filename, "r");

	if (fp == NULL)
		return;

	keylen1 = strlen (key1);
	if (key2 != NULL)
		keylen2 = strlen (key2);
	else
		keylen2 = -1;

	/* This is slightly wrong, it should only look
	 * at the correct section */
	while (fgets (buf, sizeof (buf), fp) != NULL) {
		char *p;
		int len;
		int keylen;
		char **result = NULL;

		/* check if it's one of the keys */
		if (strncmp (buf, key1, keylen1) == 0) {
			result = result1;
			keylen = keylen1;
		} else if (keylen2 >= 0 &&
			   strncmp (buf, key2, keylen2) == 0) {
			result = result2;
			keylen = keylen2;
		} else {
			continue;
		}

		p = &buf[keylen];

		/* still not our key */
		if (*p != ' ' || *p != '=')
			continue;

		do
			p++;
		while (*p == ' ' || *p == '=');

		/* get rid of trailing \n */
		len = strlen (p);
		if (p[len-1] == '\n' ||
		    p[len-1] == '\r')
			p[len-1] = '\0';

		*result = g_strdup (p);

		if (*result1 == NULL ||
		    (result2 != NULL && *result2 == NULL))
			break;
	}

	fclose (fp);
}

static void
invalidate_folder (Folder *folder)
{
	GSList *li;

	folder->up_to_date = FALSE;

	for (li = folder->subfolders; li != NULL; li = li->next) {
		Folder *subfolder = li->data;

		invalidate_folder (subfolder);
	}
}


static void
vfolder_info_insert_entry (VFolderInfo *info, EntryFile *efile)
{
	GSList *entry_list;

	entry_list = g_hash_table_lookup (info->entries_ht, efile->entry.name);
	if (entry_list != NULL) {
		Entry *entry = entry_list->data;
		info->entries = g_slist_remove_link (info->entries, 
						     entry_list);
		g_slist_free_1 (entry_list);
		entry_unref (entry);
	}

	info->entries = g_slist_prepend (info->entries, efile);
	/* The hash table contains the GSList pointer */
	g_hash_table_insert (info->entries_ht, efile->entry.name, 
			     info->entries);
}

static void
set_keywords (EntryFile *efile, const char *keywords)
{
	if (keywords != NULL) {
		int i;
		char **parsed = g_strsplit (keywords, ";", -1);
		for (i = 0; parsed[i] != NULL; i++) {
			GQuark quark;
			const char *word = parsed[i];
			/* ignore empties (including end of list) */
			if (word[0] == '\0')
				continue;
			quark = g_quark_from_string (word);
			efile->keywords = g_slist_prepend
				(efile->keywords,
				 GINT_TO_POINTER (quark));
		}
		g_strfreev (parsed);
	}
}

static EntryFile *
make_entry_file (const char *dir, const char *name)
{
	EntryFile *efile;
	char *categories;
	char *only_show_in;
	char *filename;
	int i;

	filename = g_build_filename (dir, name, NULL);

	readitem_entry (filename,
			"Categories",
			&categories,
			"OnlyShowIn",
			&only_show_in);

	if (only_show_in != NULL) {
		gboolean show = FALSE;
		char **parsed = g_strsplit (only_show_in, ";", -1);
		for (i = 0; parsed[i] != NULL; i++) {
			if (strcmp (parsed[i], "GNOME") == 0) {
				show = TRUE;
				break;
			}
		}
		g_strfreev (parsed);
		if ( ! show) {
			g_free (filename);
			g_free (only_show_in);
			g_free (categories);
			return NULL;
		}
	}

	efile = file_new (name);
	efile->filename = filename;

	set_keywords (efile, categories);

	g_free (only_show_in);
	g_free (categories);

	return efile;
}

static void
vfolder_info_read_items_from (VFolderInfo *info,
			      const char *item_dir,
			      gboolean per_user)
{
	DIR *dir;
	struct dirent *de;

	dir = opendir (item_dir);
	if (dir == NULL)
		return;

	while ((de = readdir (dir)) != NULL) {
		EntryFile *efile;

		/* files MUST be called .desktop */
		if (de->d_name[0] == '.' ||
		    ! check_ext (de->d_name, ".desktop"))
			continue;

		efile = make_entry_file (item_dir, de->d_name);
		if (efile == NULL)
			continue;

		efile->per_user = per_user;

		vfolder_info_insert_entry (info, efile);
	}

	closedir (dir);
}

static void
vfolder_info_read_items_merge (VFolderInfo *info, const char *merge_dir, const char *subdir)
{
	DIR *dir;
	struct dirent *de;
	GQuark extra_keyword;
	GQuark Application;

	dir = opendir (merge_dir);
	if (dir == NULL)
		return;

	Application = g_quark_from_static_string ("Application");

	/* FIXME: this should be a hash or something */
	extra_keyword = 0;
	if (subdir == NULL)
		extra_keyword = g_quark_from_static_string ("Core");
	/*else if (g_ascii_strcasecmp (subdir, "Applications") == 0)
		 ;*/
	else if (g_ascii_strcasecmp (subdir, "Development") == 0)
		extra_keyword = g_quark_from_static_string ("Development");
	else if (g_ascii_strcasecmp (subdir, "Editors") == 0)
		extra_keyword = g_quark_from_static_string ("TextEditor");
	else if (g_ascii_strcasecmp (subdir, "Games") == 0)
		extra_keyword = g_quark_from_static_string ("Game");
	else if (g_ascii_strcasecmp (subdir, "Graphics") == 0)
		extra_keyword = g_quark_from_static_string ("Graphics");
	else if (g_ascii_strcasecmp (subdir, "Internet") == 0)
		extra_keyword = g_quark_from_static_string ("Network");
	else if (g_ascii_strcasecmp (subdir, "Multimedia") == 0)
		extra_keyword = g_quark_from_static_string ("AudioVideo");
	else if (g_ascii_strcasecmp (subdir, "Settings") == 0)
		extra_keyword = g_quark_from_static_string ("Settings");
	else if (g_ascii_strcasecmp (subdir, "System") == 0)
		extra_keyword = g_quark_from_static_string ("System");
	else if (g_ascii_strcasecmp (subdir, "Utilities") == 0)
		extra_keyword = g_quark_from_static_string ("Utility");

	while ((de = readdir (dir)) != NULL) {
		EntryFile *efile;

		/* ignore hidden */
		if (de->d_name[0] == '.')
			continue;

		/* files MUST be called .desktop, so
		 * treat all others as dirs.  If we're wrong,
		 * the open will fail, which is ok */
		if ( ! check_ext (de->d_name, ".desktop")) {
			/* if this is a directory recurse */
			char *fullname = g_build_filename (merge_dir, de->d_name, NULL);
			vfolder_info_read_items_merge (info, fullname, de->d_name);
			g_free (fullname);
			continue;
		}

		/* FIXME: add some keywords about some known apps
		 * like gimp and whatnot, perhaps take these from the vfolder
		 * file or some such */

		efile = make_entry_file (merge_dir, de->d_name);
		if (efile == NULL)
			continue;

		/* If no keywords set, then add the standard ones */
		if (efile->keywords == NULL) {
			efile->keywords = g_slist_prepend
				(efile->keywords,
				 GINT_TO_POINTER (Application));
			if (extra_keyword != 0) {
				efile->keywords = g_slist_prepend
					(efile->keywords,
					 GINT_TO_POINTER (extra_keyword));
			}
			efile->implicit_keywords = TRUE;
		}

		vfolder_info_insert_entry (info, efile);
	}

	closedir (dir);
}

static void
vfolder_info_read_items (VFolderInfo *info)
{
	GSList *li;

	/* First merge */
	for (li = info->merge_dirs; li != NULL; li = li->next) {
		const char *merge_dir = li->data;

		vfolder_info_read_items_merge (info, merge_dir, NULL);
	}

	/* Then read the real thing (later overrides) */
	for (li = info->item_dirs; li != NULL; li = li->next) {
		const char *item_dir = li->data;

		vfolder_info_read_items_from (info, item_dir,
					      FALSE /* per_user */);
	}

	if (info->user_item_dir != NULL)
		vfolder_info_read_items_from (info,
					      info->user_item_dir,
					      TRUE /* per_user */);
}

static VFolderInfo *
get_vfolder_info (const char *scheme)
{
	VFolderInfo *info;

	/* if this is an all scheme, skip first 4
	 * bytes to read the info */
	if (ALL_SCHEME_P (scheme))
		scheme += 4;

	if (infos != NULL &&
	    (info = g_hash_table_lookup (infos, scheme)) != NULL) {
		return info;
	}

	if (infos == NULL)
		infos = g_hash_table_new_full
			(g_str_hash, g_str_equal,
			 (GDestroyNotify)g_free,
			 (GDestroyNotify)vfolder_info_destroy);

	info = g_new0 (VFolderInfo, 1);
	vfolder_info_init (info, scheme);

	vfolder_info_read_info (info);

	vfolder_info_read_items (info);

	g_hash_table_insert (infos, g_strdup (scheme), info);

	return info;
}

static char *
keywords_to_string (GSList *keywords)
{
	GSList *li;
	GString *str = g_string_new (NULL);

	for (li = keywords; li != NULL; li = li->next) {
		GQuark word = GPOINTER_TO_INT (li->data);
		g_string_append (str, g_quark_to_string (word));
		g_string_append_c (str, ';');
	}

	return g_string_free (str, FALSE);
}

/* copy file and add keywords line */
static gboolean
copy_file_with_keywords (const char *from, const char *to, GSList *keywords)
{
	FILE *fp;
	FILE *wfp;
	int wfd;
	char buf[BUFSIZ];
	char *keyword_string;

	if ( ! ensure_dir (to,
			   TRUE /* ignore_basename */))
		return FALSE;

	wfd = open (to, O_CREAT | O_WRONLY | O_TRUNC, 0600);
	if (wfd < 0) {
		return FALSE;
	}

	keyword_string = keywords_to_string (keywords);

	wfp = fdopen (wfd, "w");

	fp = fopen (from, "r");
	if (fp != NULL) {
		gboolean wrote_keywords = FALSE;
		while (fgets (buf, sizeof (buf), fp) != NULL) {
			fprintf (wfp, "%s", buf);
			if ( ! wrote_keywords &&
			    (strncmp (buf, "[Desktop Entry]",
				      strlen ("[Desktop Entry]")) == 0 ||
			     strncmp (buf, "[KDE Desktop Entry]",
				      strlen ("[KDE Desktop Entry]")) == 0)) {
				fprintf (wfp, "Categories=%s\n",
					 keyword_string);
				wrote_keywords = TRUE;
			}
		}

		fclose (fp);
	} else {
		fprintf (wfp, "[Desktop Entry]\nCategories=%s\n",
			 keyword_string);
	}

	/* FIXME: does this close wfd???? */
	fclose (wfp);

	close (wfd);

	g_free (keyword_string);

	return TRUE;
}

static gboolean
copy_file (const char *from, const char *to)
{
	int fd;
	int wfd;

	if ( ! ensure_dir (to,
			   TRUE /* ignore_basename */))
		return FALSE;

	wfd = open (to, O_CREAT | O_WRONLY | O_TRUNC, 0600);
	if (wfd < 0) {
		return FALSE;
	}

	fd = open (from, O_RDONLY);
	if (fd >= 0) {
		char buf[1024];
		ssize_t n;

		while ((n = read (fd, buf, sizeof(buf))) > 0) {
			write (wfd, buf, n);
		}

		close (fd);
	}

	close (wfd);

	return TRUE;
}

static gboolean
make_file_private (VFolderInfo *info, EntryFile *efile)
{
	char *newfname;

	if (efile->per_user)
		return TRUE;

	newfname = g_build_filename (g_get_home_dir (),
				     ".gnome",
				     "vfolders",
				     info->scheme,
				     efile->entry.name,
				     NULL);

	if (efile->implicit_keywords) {
		if (efile->filename != NULL &&
		    ! copy_file_with_keywords (efile->filename,
					       newfname,
					       efile->keywords)) {
			g_free (newfname);
			return FALSE;
		}
	} else {
		if (efile->filename != NULL &&
		    ! copy_file (efile->filename, newfname)) {
			g_free (newfname);
			return FALSE;
		}
	}

	/* we didn't copy but ensure path anyway */
	if (efile->filename == NULL &&
	    ! ensure_dir (newfname,
			  TRUE /* ignore_basename */)) {
		g_free (newfname);
		return FALSE;
	}


	g_free (efile->filename);
	efile->filename = newfname;
	efile->per_user = TRUE;

	return TRUE;
}

static void
make_new_dirfile (VFolderInfo *info, Folder *folder)
{
	char *name = g_strdup (folder->entry.name);
	char *fname;
	char *p;
	int i;
	int fd;

	for (p = name; *p != '\0'; p++) {
		if ( ! ( (*p >= 'a' && *p <= 'z') ||
			 (*p >= 'A' && *p <= 'Z') ||
			 (*p >= '0' && *p <= '9') ||
			 *p == '_')) {
			*p = '_';
		}
	}

	i = 0;
	fname = NULL;
	do {
		char *fullname;

		g_free (fname);

		if (i > 0) {
			fname = g_strdup_printf ("%s-%d.directory", name, i);
		} else {
			fname = g_strdup_printf ("%s.directory", name);
		}

		fullname = g_build_filename
			(info->user_desktop_dir, fname, NULL);
		fd = open (fullname, O_CREAT | O_WRONLY | O_EXCL, 0600);
		g_free (fullname);
	} while (fd < 0);

	close (fd);

	folder->desktop_file = fname;
	info->dirty = TRUE;
}

static gboolean
make_dirfile_private (VFolderInfo *info, Folder *folder)
{
	char *fname;
	char *desktop_file;

	if (info->user_desktop_dir == NULL)
		return FALSE;

	if ( ! ensure_dir (info->user_desktop_dir,
			   FALSE /* ignore_basename */))
		return FALSE;


	if (folder->desktop_file == NULL) {
		make_new_dirfile (info, folder);
		return TRUE;
	}

	fname = g_build_filename (info->user_desktop_dir,
				  folder->desktop_file,
				  NULL);

	if (access (fname, F_OK) == 0) {
		g_free (fname);
		return TRUE;
	}

	desktop_file = get_directory_file (info, folder);

	if (desktop_file == NULL) {
		int fd = open (fname, O_CREAT | O_EXCL | O_WRONLY, 0600);
		g_free (fname);
		if (fd >= 0) {
			close (fd);
			return TRUE;
		}
		return FALSE;
	}

	if ( ! copy_file (desktop_file, fname)) {
		g_free (desktop_file);
		g_free (fname);
		return FALSE;
	}

	g_free (desktop_file);
	g_free (fname);

	return TRUE;
}

static Entry *
find_entry (GSList *list, const char *name)
{
	GSList *li;
	for (li = list; li != NULL; li = li->next) {
		Entry *entry = li->data;
		if (strcmp (name, entry->name) == 0)
			return entry;
	}
	return NULL;
}

static int
next_non_empty (char **vec, int i)
{
	do
		i++;
	while (vec[i] != NULL &&
	       *(vec[i]) == '\0');

	return i;
}

static Folder *
resolve_folder (VFolderInfo *info,
		const char *path,
		gboolean ignore_basename,
		GnomeVFSResult *result)
{
	char **ppath;
	int i;
	Folder *folder = info->root;

	ppath = g_strsplit (path, "/", -1);

	if (ppath == NULL ||
	    ppath[0] == NULL) {
		g_strfreev (ppath);
		*result = GNOME_VFS_ERROR_INVALID_URI;
		return NULL;
	}

	/* find first non_empty */
	i = next_non_empty (ppath, -1);
	while (ppath[i] != NULL && folder != NULL) {
		const char *segment = ppath[i];
		i = next_non_empty (ppath, i);
		if (ignore_basename && ppath[i] == NULL) {
			return folder;
		} else {
			folder = (Folder *)find_entry (folder->subfolders, segment);
		}
	}
	g_strfreev (ppath);

	if (folder == NULL) {
		*result = GNOME_VFS_ERROR_NOT_FOUND;
	}
	return folder;
}

static Entry *
resolve_path (VFolderInfo *info,
	      const char *path,
	      const char *basename,
	      Folder **return_folder,
	      GnomeVFSResult *result)
{
	Entry *entry;
	Folder *folder;

	if (strcmp (path, "/") == 0)
		return (Entry *)info->root;

	folder = resolve_folder (info, path,
				 TRUE /* ignore_basename */,
				 result);

	if (return_folder != NULL)
		*return_folder = folder;

	if (folder == NULL) {
		*result = GNOME_VFS_ERROR_NOT_FOUND;
		return NULL;
	}

	/* Make sure we have the entries here */
	ensure_folder (info, folder);

	entry = find_entry (folder->entries, basename);

	if (entry == NULL)
		*result = GNOME_VFS_ERROR_NOT_FOUND;

	return entry;
}

static VFolderInfo *
vfolder_info_from_uri (GnomeVFSURI *uri, GnomeVFSResult *result)
{
	const char *scheme;

	scheme = gnome_vfs_uri_get_scheme (uri);

	/* huh? */
	if (scheme == NULL) {
		*result = GNOME_VFS_ERROR_INVALID_URI;
		return NULL;
	}

	return get_vfolder_info (scheme);
}

static Entry *
get_entry (GnomeVFSURI *uri,
	   Folder **parent,
	   gboolean *is_directory_file,
	   GnomeVFSResult *result)
{
	const char *path;
	char *basename;
	const char *scheme;
	VFolderInfo *info;
	Entry *entry;

	if (is_directory_file != NULL)
		*is_directory_file = FALSE;
	if (parent != NULL)
		*parent = NULL;

	path = gnome_vfs_uri_get_path (uri);
	basename = get_basename (uri);
	scheme = gnome_vfs_uri_get_scheme (uri);

	/* huh? */
	if (path == NULL ||
	    basename == NULL ||
	    scheme == NULL) {
		g_free (basename);
		*result = GNOME_VFS_ERROR_INVALID_URI;
		return NULL;
	}

	info = get_vfolder_info (scheme);
	g_assert (info != NULL);

	if (ALL_SCHEME_P (scheme)) {
		/* FIXME: if there is a dir structure in the uri,
		 * file shouldn't found no matter what */
		path = basename;
	}

	/* if this is just the filename, get just the filename */
	if (strchr (path, '/') == NULL) {
		GSList *efile_list;

		efile_list = g_hash_table_lookup (info->entries_ht, path);

		g_free (basename);

		if (efile_list == NULL) {
			*result = GNOME_VFS_ERROR_NOT_FOUND;
			return NULL;
		} else {
			return efile_list->data;
		}
	} else if (strcmp (basename, ".directory") == 0) {
		Folder *folder;

		folder = resolve_folder (info, path,
					 TRUE /* ignore_basename */,
					 result);

		g_free (basename);

		if (folder == NULL) {
			*result = GNOME_VFS_ERROR_NOT_FOUND;
			return FALSE;
		}

		if (parent != NULL)
			*parent = folder;

		return (Entry *)folder;
	} else {
		entry = resolve_path (info, path, basename, parent, result);
		g_free (basename);
		if (entry == NULL)
			return NULL;
		return entry;
	}
}

/* only works for files and only those that exist */
static GnomeVFSURI *
desktop_uri_to_file_uri (GnomeVFSURI *desktop_uri,
			 Entry **the_entry,
			 gboolean *the_is_directory_file,
			 Folder **the_folder,
			 gboolean privatize,
			 GnomeVFSResult *result)
{
	gboolean is_directory_file;
	GnomeVFSURI *ret_uri;
	Folder *folder = NULL;
	Entry *entry;

	entry = get_entry (desktop_uri,
			   &folder,
			   &is_directory_file,
			   result);

	if (the_folder != NULL)
		*the_folder = folder;

	if (the_entry != NULL)
		*the_entry = entry;
	if (the_is_directory_file != NULL)
		*the_is_directory_file = is_directory_file;

	if (entry == NULL) {
		*result = GNOME_VFS_ERROR_NOT_FOUND;
		return NULL;
	} else if (is_directory_file &&
		   entry->type == ENTRY_FOLDER) {
		char *desktop_file;
		VFolderInfo *info;

		folder = (Folder *)entry;

		if (the_folder != NULL)
			*the_folder = folder;

		/* we'll be doing something write like */
		if (folder->read_only &&
		    privatize) {
			*result = GNOME_VFS_ERROR_READ_ONLY;
			return NULL;
		}

		info = vfolder_info_from_uri (desktop_uri, result);
		if (info == NULL)
			return NULL;

		if (privatize) {
			char *fname;
			if ( ! make_dirfile_private (info, folder)) {
				*result = GNOME_VFS_ERROR_GENERIC;
				return NULL;
			}
			fname = g_build_filename (g_get_home_dir (),
						  folder->desktop_file,
						  NULL);
			ret_uri = gnome_vfs_uri_new (fname);
			g_free (fname);
			return ret_uri;
		}

		desktop_file = get_directory_file (info, folder);
		if (desktop_file != NULL) {
			char *s = gnome_vfs_get_uri_from_local_path
				(desktop_file);

			g_free (desktop_file);

			ret_uri = gnome_vfs_uri_new (s);
			g_free (s);

			return ret_uri;
		} else {
			*result = GNOME_VFS_ERROR_NOT_FOUND;
			return NULL;
		}
	} else if (entry->type == ENTRY_FILE) {
		EntryFile *efile = (EntryFile *)entry;
		VFolderInfo *info;
		char *s;

		info = vfolder_info_from_uri (desktop_uri, result);
		if (info == NULL)
			return NULL;

		/* we'll be doing something write like */
		if (folder != NULL &&
		    folder->read_only &&
		    privatize) {
			*result = GNOME_VFS_ERROR_READ_ONLY;
			return NULL;
		}

		if (privatize &&
		    ! make_file_private (info, efile)) {
			*result = GNOME_VFS_ERROR_GENERIC;
			return NULL;
		}

		s = gnome_vfs_get_uri_from_local_path (efile->filename);
		ret_uri = gnome_vfs_uri_new (s);
		g_free (s);

		return ret_uri;
	} else {
		if (the_folder != NULL)
			*the_folder = (Folder *)entry;
		*result = GNOME_VFS_ERROR_IS_DIRECTORY;
		return NULL;
	}
}

static void
remove_file (Folder *folder, const char *basename)
{
	GSList *li;
	char *s;

	for (li = folder->includes; li != NULL; li = li->next) {
		const char *include = li->data;
		if (strcmp (include, basename) == 0) {
			folder->includes = g_slist_remove_link
				(folder->includes, li);
			g_slist_free_1 (li);
			break;
		}
	}

	if (folder->excludes == NULL) {
		folder->excludes = g_hash_table_new_full
			(g_str_hash, g_str_equal,
			 (GDestroyNotify)g_free,
			 NULL);
	}
	s = g_strdup (basename);
	g_hash_table_replace (folder->excludes, s, s);
}

static void
add_file (Folder *folder, const char *basename)
{
	GSList *li;

	for (li = folder->includes; li != NULL; li = li->next) {
		const char *include = li->data;
		if (strcmp (include, basename) == 0)
			break;
	}

	/* if not found */
	if (li == NULL)
		folder->includes =
			g_slist_prepend (folder->includes,
					 g_strdup (basename));
	if (folder->excludes != NULL)
		g_hash_table_remove (folder->excludes, basename);
}

static gboolean
open_check (GnomeVFSMethod *method,
	    GnomeVFSURI *uri,
	    GnomeVFSMethodHandle **method_handle,
	    GnomeVFSOpenMode mode,
	    GnomeVFSResult *result)
{
	const char *path = gnome_vfs_uri_get_path (uri);
	const char *scheme = gnome_vfs_uri_get_scheme (uri);

	if (path == NULL ||
	    scheme == NULL) {
		*result = GNOME_VFS_ERROR_INVALID_URI;
		return FALSE;
	}

	if (ALL_SCHEME_P (scheme)) {
		if (mode & GNOME_VFS_OPEN_WRITE) {
			*result = GNOME_VFS_ERROR_READ_ONLY;
			return FALSE;
		}
	}

	if (strcmp (path, "gibberish.txt") == 0) {
		/* a bit of evil never hurt no-one */
		*result = GNOME_VFS_OK;
		*method_handle = (GnomeVFSMethodHandle *)method;
		return FALSE;
	}

	return TRUE;
}

typedef struct _FileHandle FileHandle;
struct _FileHandle {
	VFolderInfo *info;
	GnomeVFSMethodHandle *handle;
	Entry *entry;
	gboolean write;
	gboolean is_directory_file;
};

static void
make_handle (GnomeVFSMethodHandle **method_handle,
	     GnomeVFSMethodHandle *file_handle,
	     VFolderInfo *info,
	     Entry *entry,
	     gboolean is_directory_file,
	     gboolean write)
{
	if (file_handle != NULL) {
		FileHandle *handle = g_new0 (FileHandle, 1);

		handle->info = info;
		handle->handle = file_handle;
		handle->entry = entry_ref (entry);
		handle->is_directory_file = is_directory_file;
		handle->write = write;

		*method_handle = (GnomeVFSMethodHandle *) handle;
	} else {
		*method_handle = NULL;
	}
}

static void
whack_handle (FileHandle *handle)
{
	entry_unref (handle->entry);
	handle->entry = NULL;

	handle->handle = NULL;
	handle->info = NULL;

	g_free (handle);
}

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	GnomeVFSURI *file_uri;
	GnomeVFSResult result = GNOME_VFS_OK;
	VFolderInfo *info;
	Entry *entry;
	gboolean is_directory_file;
	GnomeVFSMethodHandle *file_handle;

	info = vfolder_info_from_uri (uri, &result);
	if (info == NULL)
		return result;

	if (info->read_only &&
	    mode & GNOME_VFS_OPEN_WRITE) {
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	if ( ! open_check (method, uri, method_handle, mode, &result))
		return result;

	file_uri = desktop_uri_to_file_uri (uri,
					    &entry,
					    &is_directory_file,
					    NULL /* the_folder */,
					    mode & GNOME_VFS_OPEN_WRITE,
					    &result);
	if (file_uri == NULL)
		return result;

	result = (* parent_method->open) (parent_method,
					  &file_handle,
					  file_uri,
					  mode,
					  context);

	make_handle (method_handle,
		     file_handle,
		     info,
		     entry,
		     is_directory_file,
		     mode & GNOME_VFS_OPEN_WRITE);

	gnome_vfs_uri_unref (file_uri);

	if (info->dirty)
		vfolder_info_write_user (info);

	return result;
}

static void
remove_from_all_except (Folder *root,
			const char *name,
			Folder *except)
{
	GSList *li;

	if (root != except) {
		remove_file (root, name);
		if (root->up_to_date) {
			for (li = root->entries; li != NULL; li = li->next) {
				Entry *entry = li->data;
				if (strcmp (name, entry->name) == 0) {
					root->entries = 
						g_slist_remove_link
						   (root->entries, li);
					g_slist_free_1 (li);
					break;
				}
			}
		}
	}

	for (li = root->subfolders; li != NULL; li = li->next) {
		Folder *subfolder = li->data;

		remove_from_all_except (subfolder, name, except);
	}
}

static GnomeVFSResult
do_create (GnomeVFSMethod *method,
	   GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm,
	   GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	GnomeVFSMethodHandle *file_handle;
	GnomeVFSURI *file_uri;
	const char *scheme;
	const char *basename;
	const char *path;
	VFolderInfo *info;
	Folder *parent;
	Entry *entry;
	EntryFile *efile;
	char *s;
	GSList *li;

	scheme = gnome_vfs_uri_get_scheme (uri);
	basename = get_basename (uri);
	path = gnome_vfs_uri_get_path (uri);
	if (scheme == NULL ||
	    basename == NULL ||
	    path == NULL ||
	    ( ! check_ext (basename, ".desktop") &&
	      ! strcmp (basename, ".directory") == 0)) {
		return GNOME_VFS_ERROR_INVALID_URI;
	}

	/* all scheme is read only */
	if (ALL_SCHEME_P (scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	info = get_vfolder_info (scheme);
	g_assert (info != NULL);

	if (info->user_filename == NULL ||
	    info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	parent = resolve_folder (info, path,
				 TRUE /* ignore_basename */,
				 &result);
	if (parent == NULL)
		return result;

	if (parent->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	if (strcmp (basename, ".directory") == 0) {
		char *fname;
		if (exclusive) {
			char *desktop_file;
			desktop_file = get_directory_file (info, parent);
			if (desktop_file != NULL) {
				g_free (desktop_file);
				return GNOME_VFS_ERROR_FILE_EXISTS;
			}
		}

		if ( ! make_dirfile_private (info, parent))
			return GNOME_VFS_ERROR_GENERIC;
		fname = g_build_filename (g_get_home_dir (),
					  parent->desktop_file,
					  NULL);
		s = gnome_vfs_get_uri_from_local_path (fname);
		file_uri = gnome_vfs_uri_new (s);
		g_free (fname);
		g_free (s);

		if (file_uri == NULL)
			return GNOME_VFS_ERROR_GENERIC;

		result = (* parent_method->create) (parent_method,
						    &file_handle,
						    file_uri,
						    mode,
						    exclusive,
						    perm,
						    context);
		gnome_vfs_uri_unref (file_uri);

		make_handle (method_handle,
			     file_handle,
			     info,
			     (Entry *)parent,
			     TRUE /* is_directory_file */,
			     TRUE /* write */);

		if (info->dirty)
			vfolder_info_write_user (info);

		return result;
	}

	ensure_folder (info, parent);

	entry = find_entry (parent->entries, basename);

	if (entry != NULL &&
	    entry->type == ENTRY_FOLDER)
		return GNOME_VFS_ERROR_IS_DIRECTORY;

	efile = (EntryFile *)entry;

	if (efile != NULL) {
		if (exclusive)
			return GNOME_VFS_ERROR_FILE_EXISTS;

		if ( ! make_file_private (info, efile)) {
			return GNOME_VFS_ERROR_GENERIC;
		}

		s = gnome_vfs_get_uri_from_local_path (efile->filename);
		file_uri = gnome_vfs_uri_new (s);
		g_free (s);

		if (file_uri == NULL)
			return GNOME_VFS_ERROR_GENERIC;

		result = (* parent_method->create) (parent_method,
						    &file_handle,
						    file_uri,
						    mode,
						    exclusive,
						    perm,
						    context);
		gnome_vfs_uri_unref (file_uri);

		make_handle (method_handle,
			     file_handle,
			     info,
			     (Entry *)efile,
			     FALSE /* is_directory_file */,
			     TRUE /* write */);

		return result;
	}
	
	li = g_hash_table_lookup (info->entries_ht, basename);

	if (exclusive && li != NULL)
		return GNOME_VFS_ERROR_FILE_EXISTS;

	if (li == NULL) {
		efile = file_new (basename);
		vfolder_info_insert_entry (info, efile);
	}

	/* this will make a private name for this */
	if ( ! make_file_private (info, efile))
		return GNOME_VFS_ERROR_GENERIC;

	add_file (parent, basename);
	parent->sorted = FALSE;

	if (parent->up_to_date)
		parent->entries = g_slist_prepend (parent->entries, efile);

	/* if we created a brand new name, then we exclude it
	 * from everywhere else to ensure overall sanity */
	if (li == NULL)
		remove_from_all_except (info->root, basename, parent);

	s = gnome_vfs_get_uri_from_local_path (efile->filename);
	file_uri = gnome_vfs_uri_new (s);
	g_free (s);

	result = (* parent_method->create) (parent_method,
					    &file_handle,
					    file_uri,
					    mode,
					    exclusive,
					    perm,
					    context);
	gnome_vfs_uri_unref (file_uri);

	make_handle (method_handle,
		     file_handle,
		     info,
		     (Entry *)efile,
		     FALSE /* is_directory_file */,
		     TRUE /* write */);

	vfolder_info_write_user (info);

	return result;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;

	/* FIXME: if this was a writing open, reread
	 * keywords */

	if (method_handle == (GnomeVFSMethodHandle *)method)
		return GNOME_VFS_OK;
	
	result = (* parent_method->close) (parent_method,
					   handle->handle,
					   context);
	handle->handle = NULL;

	/* we reread the Categories keyword */
	if (handle->write &&
	    handle->entry != NULL &&
	    handle->entry->type == ENTRY_FILE) {
		EntryFile *efile = (EntryFile *)handle->entry;
		char *categories;
		readitem_entry (efile->filename,
				"Categories",
				&categories,
				NULL,
				NULL);
		set_keywords (efile, categories);
		g_free (categories);
		/* FIXME: what about OnlyShowIn */

		/* FIXME: check if the keywords changed, if not, do
		 * nothing */

		/* Perhaps a bit drastic */
		invalidate_folder (handle->info->root);
	}

	whack_handle (handle);

	return result;
}

static void
fill_buffer (gpointer buffer,
	     GnomeVFSFileSize num_bytes,
	     GnomeVFSFileSize *bytes_read)
{
	char *buf = buffer;
	GnomeVFSFileSize i;
	for (i = 0; i < num_bytes; i++) {
		if (rand () % 32 == 0 ||
		    i == num_bytes-1)
			buf[i] = '\n';
		else
			buf[i] = ((rand()>>4) % 94) + 32;
	}
	if (bytes_read != 0)
		*bytes_read = i;
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;

	if (method_handle == (GnomeVFSMethodHandle *)method) {
		if ((rand () >> 4) & 0x3) {
			fill_buffer (buffer, num_bytes, bytes_read);
			return GNOME_VFS_OK;
		} else {
			return GNOME_VFS_ERROR_EOF;
		}
	}
	
	result = (* parent_method->read) (parent_method,
					  handle->handle,
					  buffer, num_bytes,
					  bytes_read,
					  context);

	return result;
}

static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;

	if (method_handle == (GnomeVFSMethodHandle *)method)
		return GNOME_VFS_OK;

	result = (* parent_method->write) (parent_method,
					   handle->handle,
					   buffer, num_bytes,
					   bytes_written,
					   context);

	return result;
}


static GnomeVFSResult
do_seek (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset,
	 GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;

	if (method_handle == (GnomeVFSMethodHandle *)method)
		return GNOME_VFS_OK;
	
	result = (* parent_method->seek) (parent_method,
					  handle->handle,
					  whence, offset,
					  context);

	return result;
}

static GnomeVFSResult
do_tell (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;
	
	result = (* parent_method->tell) (parent_method,
					  handle->handle,
					  offset_return);

	return result;
}


static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSFileSize where,
		    GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;

	if (method_handle == (GnomeVFSMethodHandle *)method)
		return GNOME_VFS_OK;
	
	result = (* parent_method->truncate_handle) (parent_method,
						     handle->handle,
						     where,
						     context);

	return result;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod *method,
	     GnomeVFSURI *uri,
	     GnomeVFSFileSize where,
	     GnomeVFSContext *context)
{
	GnomeVFSURI *file_uri;
	GnomeVFSResult result = GNOME_VFS_OK;
	VFolderInfo *info;
	Entry *entry;
	const char *scheme;

	scheme = gnome_vfs_uri_get_scheme (uri);
	if (scheme == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (ALL_SCHEME_P (scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	info = vfolder_info_from_uri (uri, &result);
	if (info == NULL)
		return result;

	if (info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	file_uri = desktop_uri_to_file_uri (uri,
					    &entry,
					    NULL /* the_is_directory_file */,
					    NULL /* the_folder */,
					    TRUE /* privatize */,
					    &result);
	if (file_uri == NULL)
		return result;

	result = (* parent_method->truncate) (parent_method,
					      file_uri,
					      where,
					      context);

	gnome_vfs_uri_unref (file_uri);

	if (info->dirty)
		vfolder_info_write_user (info);

	if (entry->type == ENTRY_FILE) {
		EntryFile *efile = (EntryFile *)entry;
		g_slist_free (efile->keywords);
		efile->keywords = NULL;
	}

	/* Perhaps a bit drastic, but oh well */
	invalidate_folder (info->root);

	return result;
}

typedef struct _DirHandle DirHandle;
struct _DirHandle {
	VFolderInfo *info;
	Folder *folder;

	GnomeVFSFileInfoOptions options;

	/* List of Entries */
	GSList *list;
	GSList *current;
};

static GnomeVFSResult
do_open_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	const char *path;
	const char *scheme;
	DirHandle *dh;
	Folder *folder;
	VFolderInfo *info;
	char *desktop_file;

	path = gnome_vfs_uri_get_path (uri);
	scheme = gnome_vfs_uri_get_scheme (uri);
	if (path == NULL ||
	    scheme == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	info = get_vfolder_info (scheme);
	g_assert (info != NULL);

	/* In the all- scheme just list all filenames */
	if (ALL_SCHEME_P (scheme)) {
		/* FIXME: if directory not / then return not found */
		dh = g_new0 (DirHandle, 1);
		dh->info = info;
		dh->options = options;
		dh->folder = NULL;
		dh->list = g_slist_copy (info->entries);
		g_slist_foreach (dh->list, (GFunc)entry_ref, NULL);
		dh->current = dh->list;
		*method_handle = (GnomeVFSMethodHandle*) dh;
		return GNOME_VFS_OK;
	}

	folder = resolve_folder (info, path,
				 FALSE /* ignore_basename */,
				 &result);
	if (folder == NULL)
		return result;

	/* Make sure we have the entries and sorted here */
	ensure_folder_sort (info, folder);

	dh = g_new0 (DirHandle, 1);
	dh->info = info;
	dh->options = options;
	dh->folder = (Folder *)entry_ref ((Entry *)folder);
	dh->list = g_slist_copy (folder->entries);
	g_slist_foreach (folder->entries, (GFunc)entry_ref, NULL);

	desktop_file = get_directory_file (info, folder);
	if (desktop_file != NULL) {
		EntryFile *efile = file_new (".directory");
		dh->list = g_slist_prepend (dh->list, efile);
		g_free (desktop_file);
	}

	dh->current = dh->list;

	*method_handle = (GnomeVFSMethodHandle*) dh;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext *context)
{
	DirHandle *dh;

	dh = (DirHandle*) method_handle;

	g_slist_foreach (dh->list, (GFunc)entry_unref, NULL);
	g_slist_free (dh->list);
	dh->list = NULL;

	dh->current = NULL;

	if (dh->folder != NULL)
		entry_unref ((Entry *)dh->folder);
	dh->folder = NULL;

	dh->info = NULL;

	g_free (dh);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	DirHandle *dh;
	Entry *entry;
	GnomeVFSFileInfoOptions options;

	dh = (DirHandle*) method_handle;

read_directory_again:

	if (dh->current == NULL) {
		return GNOME_VFS_ERROR_EOF;
	}

	entry = dh->current->data;
	dh->current = dh->current->next;

	options = dh->options;

	if (entry->type == ENTRY_FILE &&
	    ((EntryFile *)entry)->filename != NULL) {
		EntryFile *efile = (EntryFile *)entry;
		char *furi = gnome_vfs_get_uri_from_local_path (efile->filename);
		GnomeVFSURI *uri = gnome_vfs_uri_new (furi);

		/* we always get mime-type by forcing it below */
		if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
			options &= ~GNOME_VFS_FILE_INFO_GET_MIME_TYPE;

		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		/* Get the file info for this */
		(* parent_method->get_file_info) (parent_method,
						  uri,
						  file_info,
						  options,
						  context);

		/* we ignore errors from this since the file_info just
		 * won't be filled completely if there's an error, that's all */

		g_free (file_info->mime_type);
		file_info->mime_type = g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		gnome_vfs_uri_unref (uri);
		g_free (furi);
	} else if (entry->type == ENTRY_FILE) {
		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_info->name = g_strdup (entry->name);
		GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

		file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;

		/* FIXME: there should be a mime-type for these */
		file_info->mime_type = g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	} else /* ENTRY_FOLDER */ {
		Folder *folder = (Folder *)entry;

		/* Skip empty folders if they have
		 * the flag set */
		if (folder->dont_show_if_empty) {
			/* Make sure we have the entries */
			ensure_folder (dh->info, folder);

			if (folder->entries == NULL) {
				/* start this function over on the
				 * next item */
				goto read_directory_again;
			}
		}

		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_info->name = g_strdup (entry->name);
		GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;

		file_info->mime_type = g_strdup ("x-directory/normal");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  GnomeVFSContext *context)
{
	GnomeVFSURI *file_uri;
	GnomeVFSResult result = GNOME_VFS_OK;
	Folder *folder;

	file_uri = desktop_uri_to_file_uri (uri,
					    NULL /* the_entry */,
					    NULL /* the_is_directory_file */,
					    &folder,
					    FALSE /* privatize */,
					    &result);
	if (file_uri == NULL &&
	    result != GNOME_VFS_ERROR_IS_DIRECTORY)
		return result;

	if (file_uri != NULL) {

		/* we always get mime-type by forcing it below */
		if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
			options &= ~GNOME_VFS_FILE_INFO_GET_MIME_TYPE;

		result = (* parent_method->get_file_info) (parent_method,
							   file_uri,
							   file_info,
							   options,
							   context);

		g_free (file_info->mime_type);
		file_info->mime_type = g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		gnome_vfs_uri_unref (file_uri);

		return result;
	} else if (folder != NULL) {
		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_info->name = g_strdup (folder->entry.name);
		GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;

		file_info->mime_type = g_strdup ("x-directory/normal");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		return GNOME_VFS_OK;
	} else {
		return GNOME_VFS_ERROR_NOT_FOUND;
	}
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *)method_handle;

	if (method_handle == (GnomeVFSMethodHandle *)method) {
		g_free (file_info->mime_type);
		file_info->mime_type = g_strdup ("text/plain");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
		return GNOME_VFS_OK;
	}

	/* we always get mime-type by forcing it below */
	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
		options &= ~GNOME_VFS_FILE_INFO_GET_MIME_TYPE;

	result = (* parent_method->get_file_info_from_handle) (parent_method,
							       handle->handle,
							       file_info,
							       options,
							       context);

	/* any file is of the .desktop type */
	g_free (file_info->mime_type);
	file_info->mime_type = g_strdup ("application/x-gnome-app-info");
	file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

	return result;
}


static gboolean
do_is_local (GnomeVFSMethod *method,
	     const GnomeVFSURI *uri)
{
	return TRUE;
}


static GnomeVFSResult
do_make_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	const char *scheme;
	const char *path;
	char *basename;
	VFolderInfo *info;
	Folder *parent, *folder;

	scheme = gnome_vfs_uri_get_scheme (uri);
	path = gnome_vfs_uri_get_path (uri);
	if (scheme == NULL ||
	    path == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (ALL_SCHEME_P (scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	info = get_vfolder_info (scheme);
	g_assert (info != NULL);

	if (info->user_filename == NULL ||
	    info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	parent = resolve_folder (info, path,
				 TRUE /* ignore_basename */,
				 &result);
	if (parent == NULL)
		return result;

	if (parent->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	basename = get_basename (uri);

	folder = (Folder *)find_entry (parent->subfolders,
				       basename);
	if (folder != NULL) {
		g_free (basename);
		return GNOME_VFS_ERROR_FILE_EXISTS;
	}

	folder = folder_new (basename);
	parent->subfolders = g_slist_append (parent->subfolders, folder);
	parent->up_to_date = FALSE;

	vfolder_info_write_user (info);

	g_free (basename);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	Folder *folder;
	const char *scheme;
	const char *path;
	VFolderInfo *info;

	scheme = gnome_vfs_uri_get_scheme (uri);
	path = gnome_vfs_uri_get_path (uri);
	if (scheme == NULL ||
	    path == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (ALL_SCHEME_P (scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	info = get_vfolder_info (scheme);
	g_assert (info != NULL);

	if (info->user_filename == NULL ||
	    info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	folder = resolve_folder (info, path,
				 FALSE /* ignore_basename */,
				 &result);
	if (folder == NULL)
		return result;

	if (folder->read_only ||
	    (folder->parent != NULL &&
	     folder->parent->read_only))
		return GNOME_VFS_ERROR_READ_ONLY;

	/* don't make removing directories easy */
	if (folder->desktop_file != NULL) {
		return GNOME_VFS_ERROR_DIRECTORY_NOT_EMPTY;
	}

	/* Make sure we have the entries */
	ensure_folder (info, folder);

	/* don't make removing directories easy */
	if (folder->entries != NULL) {
		return GNOME_VFS_ERROR_DIRECTORY_NOT_EMPTY;
	}

	if (folder == info->root) {
		info->root = NULL;
		entry_unref ((Entry *)folder);
		info->root = folder_new ("Root");
	} else {
		Folder *parent = folder->parent;

		g_assert (parent != NULL);

		parent->subfolders =
			g_slist_remove (parent->subfolders, folder);

		parent->up_to_date = FALSE;

		entry_unref ((Entry *)folder);
	}

	vfolder_info_write_user (info);

	return GNOME_VFS_OK;
}

/* a fairly evil function that does the whole move bit by copy and
 * remove */
static GnomeVFSResult
long_move (GnomeVFSMethod *method,
	   GnomeVFSURI *old_uri,
	   GnomeVFSURI *new_uri,
	   gboolean force_replace,
	   GnomeVFSContext *context)
{
	GnomeVFSResult result;
	GnomeVFSMethodHandle *handle;
	GnomeVFSURI *file_uri;
	const char *path;
	int fd;
	char buf[BUFSIZ];
	int bytes;
	VFolderInfo *info;

	info = vfolder_info_from_uri (old_uri, &result);
	if (info == NULL)
		return result;

	file_uri = desktop_uri_to_file_uri (old_uri,
					    NULL /* the_entry */,
					    NULL /* the_is_directory_file */,
					    NULL /* the_folder */,
					    FALSE /* privatize */,
					    &result);
	if (file_uri == NULL)
		return result;

	path = gnome_vfs_uri_get_path (file_uri);
	if (path == NULL) {
		gnome_vfs_uri_unref (file_uri);
		return GNOME_VFS_ERROR_INVALID_URI;
	}

	fd = open (path, O_RDONLY);
	if (fd < 0) {
		gnome_vfs_uri_unref (file_uri);
		return gnome_vfs_result_from_errno ();
	}

	gnome_vfs_uri_unref (file_uri);

	info->inhibit_write++;

	result = method->create (method,
				 &handle,
				 new_uri,
				 GNOME_VFS_OPEN_WRITE,
				 force_replace /* exclusive */,
				 0600 /* perm */,
				 context);
	if (result != GNOME_VFS_OK) {
		close (fd);
		info->inhibit_write--;
		return result;
	}

	while ((bytes = read (fd, buf, BUFSIZ)) > 0) {
		GnomeVFSFileSize bytes_written = 0;
		result = method->write (method,
					handle,
					buf,
					bytes,
					&bytes_written,
					context);
		if (result == GNOME_VFS_OK &&
		    bytes_written != bytes)
			result = GNOME_VFS_ERROR_NO_SPACE;
		if (result != GNOME_VFS_OK) {
			close (fd);
			method->close (method, handle, context);
			/* FIXME: is this completely correct ? */
			method->unlink (method,
					new_uri,
					context);
			info->inhibit_write--;
			vfolder_info_write_user (info);
			return result;
		}
	}

	close (fd);

	result = method->close (method, handle, context);
	if (result != GNOME_VFS_OK) {
		info->inhibit_write--;
		vfolder_info_write_user (info);
		return result;
	}

	result = method->unlink (method, old_uri, context);

	info->inhibit_write--;
	vfolder_info_write_user (info);

	return result;
}

static GnomeVFSResult
move_directory_file (VFolderInfo *info,
		     Folder *old_folder,
		     Folder *new_folder)
{
	if (old_folder->desktop_file == NULL)
		return GNOME_VFS_ERROR_NOT_FOUND;

	/* "move" the desktop file */
	g_free (new_folder->desktop_file);
	new_folder->desktop_file = old_folder->desktop_file;
	old_folder->desktop_file = NULL;

	vfolder_info_write_user (info);

	return GNOME_VFS_OK;
}

static gboolean
is_sub (Folder *master, Folder *sub)
{
	GSList *li;

	for (li = master->subfolders; li != NULL; li = li->next) {
		Folder *subfolder = li->data;

		if (subfolder == sub ||
		    is_sub (subfolder, sub))
			return TRUE;
	}

	return FALSE;
}

static GnomeVFSResult
move_folder (VFolderInfo *info,
	     Folder *old_folder, Entry *old_entry,
	     Folder *new_folder, Entry *new_entry)
{
	Folder *source = (Folder *)old_entry;
	Folder *target;

	if (new_entry != NULL &&
	    new_entry->type != ENTRY_FOLDER)
		return GNOME_VFS_ERROR_NOT_A_DIRECTORY;
	if (new_entry != NULL)
		target = (Folder *)new_entry;
	else
		target = new_folder;

	/* move to where we are, yay, we're done :) */
	if (source->parent == target)
		return GNOME_VFS_OK;

	if (source == target ||
	    is_sub (source, target))
		return GNOME_VFS_ERROR_LOOP;

	/* this will never happen, but we're paranoid */
	if (source->parent == NULL)
		return GNOME_VFS_ERROR_LOOP;

	source->parent->subfolders = g_slist_remove (source->parent->subfolders,
						     source);
	target->subfolders = g_slist_append (source->parent->subfolders,
					     source);

	source->parent = target;

	vfolder_info_write_user (info);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	VFolderInfo *info;
	Folder *old_folder, *new_folder;
	Entry *old_entry, *new_entry;
	gboolean old_is_directory_file, new_is_directory_file;
	const char *old_scheme, *new_scheme;

	old_scheme = gnome_vfs_uri_get_scheme (old_uri);
	new_scheme = gnome_vfs_uri_get_scheme (new_uri);
	if (old_scheme == NULL ||
	    new_scheme == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (strcmp (old_scheme, new_scheme) != 0)
		return GNOME_VFS_ERROR_NOT_SAME_FILE_SYSTEM;

	if (ALL_SCHEME_P (old_scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	info = vfolder_info_from_uri (old_uri, &result);
	if (info == NULL)
		return result;

	if (info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	old_entry = get_entry (old_uri,
			       &old_folder,
			       &old_is_directory_file,
			       &result);

	if (old_entry == NULL)
		return result;

	if (old_folder != NULL && old_folder->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	new_entry = get_entry (new_uri,
			       &new_folder,
			       &new_is_directory_file,
			       &result);

	if (new_entry == NULL && new_folder == NULL)
		return result;

	if (new_folder != NULL && new_folder->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	if (new_is_directory_file != old_is_directory_file) {
		/* this will do another set of lookups
		 * perhaps this can be done in a nicer way,
		 * but is this the common case? I don't think so */
		return long_move (method, old_uri, new_uri,
				  force_replace, context);
	}
	
	if (new_is_directory_file) {
		g_assert (old_entry != NULL);
		g_assert (new_entry != NULL);
		return move_directory_file (info,
					    (Folder *)old_entry,
					    (Folder *)new_entry);
	}

	if (old_entry->type == ENTRY_FOLDER) {
		return move_folder (info,
				    old_folder, old_entry,
				    new_folder, new_entry);
	}

	/* move into self, just whack the old one */
	if (old_entry == new_entry) {
		char *old_basename;

		/* same folder */
		if (new_folder == old_folder)
			return GNOME_VFS_OK;

		if ( ! force_replace)
			return GNOME_VFS_ERROR_FILE_EXISTS;

		old_basename = get_basename (old_uri);
		if (old_basename == NULL)
			return GNOME_VFS_ERROR_INVALID_URI;

		remove_file (old_folder, old_basename);

		old_folder->entries = g_slist_remove
			(old_folder->entries, old_entry);
		entry_unref (old_entry);

		g_free (old_basename);

		vfolder_info_write_user (info);

		return GNOME_VFS_OK;
	}

	/* this is a simple move */
	if (new_entry == NULL ||
	    new_entry->type == ENTRY_FOLDER) {
		if (new_entry != NULL) {
			new_folder = (Folder *)new_entry;
		} else {
			/* well, let's see new_entry == NULL */
			const char *basename = get_basename (new_uri);
			/* a file and a totally different one */
			if (basename != NULL &&
			    strcmp (basename, old_entry->name) != 0) {
				/* yay, a long move */
				/* this will do another set of lookups
				 * perhaps this can be done in a nicer way,
				 * but is this the common case? I don't think
				 * so */
				return long_move (method, old_uri, new_uri,
						  force_replace, context);
			}
		}

		/* same folder */
		if (new_folder == old_folder)
			return GNOME_VFS_OK;

		remove_file (old_folder, old_entry->name);
		add_file (new_folder, old_entry->name);

		new_folder->entries = g_slist_prepend
			(new_folder->entries, old_entry);
		entry_ref (old_entry);
		new_folder->sorted = FALSE;

		old_folder->entries = g_slist_remove
			(old_folder->entries, old_entry);
		entry_unref (old_entry);

		vfolder_info_write_user (info);

		return GNOME_VFS_OK;
	}

	/* do we EVER get here? */

	/* this will do another set of lookups
	 * perhaps this can be done in a nicer way,
	 * but is this the common case? I don't think so */
	return long_move (method, old_uri, new_uri,
			  force_replace, context);
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	Entry *entry;
	Folder *the_folder;
	gboolean is_directory_file;
	VFolderInfo *info;
	const char *basename;
	const char *scheme;

	info = vfolder_info_from_uri (uri, &result);
	if (info == NULL)
		return result;

	scheme = gnome_vfs_uri_get_scheme (uri);

	if (scheme == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (ALL_SCHEME_P (scheme) ||
	    info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	basename = get_basename (uri);
	if (basename == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	entry = get_entry (uri,
			   &the_folder,
			   &is_directory_file,
			   &result);

	if (the_folder != NULL &&
	    the_folder->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	if (entry->type == ENTRY_FOLDER &&
	    is_directory_file) {
		Folder *folder = (Folder *)entry;

		if (folder->desktop_file == NULL)
			return GNOME_VFS_ERROR_NOT_FOUND;
		g_free (folder->desktop_file);
		folder->desktop_file = NULL;

		vfolder_info_write_user (info);

		return GNOME_VFS_OK;
	} else if (entry->type == ENTRY_FOLDER) {
		return GNOME_VFS_ERROR_IS_DIRECTORY;
	} else if (the_folder == NULL) {
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	the_folder->entries = g_slist_remove (the_folder->entries,
					      entry);
	entry_unref (entry);

	remove_file (the_folder, basename);

	/* FIXME: if this was a user file and this is the only
	 * reference to it, unlink it. */

	vfolder_info_write_user (info);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *source_uri,
		  GnomeVFSURI *target_uri,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{
	const char *source_scheme, *target_scheme;

	*same_fs_return = FALSE;

	source_scheme = gnome_vfs_uri_get_scheme (source_uri);
	target_scheme = gnome_vfs_uri_get_scheme (target_uri);
	if (source_scheme == NULL ||
	    target_scheme == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (strcmp (source_scheme, target_scheme) == 0 &&
	    ! ALL_SCHEME_P (source_scheme))
		*same_fs_return = TRUE;
	else
		*same_fs_return = FALSE;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	GnomeVFSURI *file_uri;
	const char *scheme;

	scheme = gnome_vfs_uri_get_scheme (uri);

	if (scheme == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (ALL_SCHEME_P (scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	/* FIXME: check read_onlyness of info and folder! */

	/* FIXME: can one set mime-type? if so circumvent it */

	/* FIXME: what to do with folders? I suppose nothing */
	file_uri = desktop_uri_to_file_uri (uri,
					    NULL /* the_entry */,
					    NULL /* the_is_directory_file */,
					    NULL /* the_folder */,
					    /* FIXME: should this privatize?,
					     * probably only if we can't
					     * access the file or something,
					     * I dunno */
					    FALSE /* privatize */,
					    &result);
	if (file_uri == NULL)
		return result;

	result = (* parent_method->set_file_info) (parent_method,
						   file_uri,
						   info,
						   mask,
						   context);
	
	gnome_vfs_uri_unref (file_uri);

	return result;	
}


/* gnome-vfs bureaucracy */

static GnomeVFSMethod method = {
	sizeof (GnomeVFSMethod),
	do_open,
	do_create,
	do_close,
	do_read,
	do_write,
	do_seek,
	do_tell,
	do_truncate_handle,
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,
	do_get_file_info_from_handle,
	do_is_local,
	do_make_directory,
	do_remove_directory,
	do_move,
	do_unlink,
	do_check_same_fs,
	do_set_file_info,
	do_truncate,
	NULL /* find_directory */,
	NULL /* create_symbolic_link */
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, 
		 const char *args)
{
	parent_method = gnome_vfs_method_get ("file");

	if (parent_method == NULL) {
		g_error ("Could not find 'file' method for gnome-vfs");
		return NULL;
	}

	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
	if (infos == NULL)
		return;

	g_hash_table_destroy (infos);
	infos = NULL;
}
