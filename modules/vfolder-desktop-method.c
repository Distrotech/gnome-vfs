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

static GnomeVFSMethod *parent_method = NULL;

static VFolderInfo *programs_info = NULL;

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

struct _QueryResult {
	GHashTable *hash;
	GSList *list;
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
	GSList *keywords;
};

struct _Folder {
	Entry entry;

	char *desktop_file; /* the .directory file */
	Query *query;

	/* The following is for per file
	 * access */
	/* excluded by filename */
	GHashTable *excludes;
	/* included by filename */
	GSList *includes;

	GSList *subfolders;

	/* lazily done, will run query only when it
	 * needs to */
	gboolean up_to_date;
	gboolean sorted;
	GSList *entries;
};

struct _VFolderInfo
{
	char *filename;
	char *desktop_dir; /* directory with .directorys */

	GSList *item_dirs;

	/* old style dirs to merge in */
	GSList *merge_dirs;

	GSList *entries;

	/* entry hash by basename */
	GHashTable *entries_ht;

	Folder *root;
};

static Entry *	entry_ref	(Entry *entry);
static void	entry_unref	(Entry *entry);

static void
destroy_entry_file (EntryFile *efile)
{
	g_free (efile->filename);
	efile->filename = NULL;

	g_slist_free (efile->keywords);
	efile->keywords = NULL;

	g_free (efile);
}

static void
destroy_folder (Folder *folder)
{
	g_free (folder->desktop_file);
	folder->desktop_file = NULL;

	/* FIXME: free query */

	if (folder->excludes != NULL) {
		g_hash_table_destroy (folder->excludes);
		folder->excludes = NULL;
	}

	g_slist_foreach (folder->includes, (GFunc)g_free, NULL)
	g_slist_free (folder->includes);
	folder->includes = NULL;

	g_slist_foreach (folder->subfolders, (GFunc)entry_unref, NULL)
	g_slist_free (folder->subfolders);
	folder->subfolders = NULL;

	g_slist_foreach (folder->entries, (GFunc)entry_unref, NULL)
	g_slist_free (folder->entries);
	folder->entries = NULL;

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
		char *filename = li->data
		Entry *entry = g_hash_table_lookup (info->entries_ht, filename);
		if (entry != NULL)
			files = g_slist_prepend (files, entry_ref (entry));
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
			if (strcmp (qfilename->filename, ((Entry *)efile)->name) == 0)
				return INVERT_IF_NEEDED (TRUE);
			} else {
				return INVERT_IF_NEEDED (FALSE);
			}
		}
	}
#undef INVERT_IF_NEEDED
}

/* Run query */
static GSList *
run_query (VFolderInfo *info, GSList *result, Query *query)
{
	GList *li;

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

	/* Include subfolders */
	if (folder->subfolders != NULL) {
		GSList *subfolders = g_slist_copy (folder->subfolders);
		g_slist_foreach (subfolders, (GFunc)entry_ref, NULL);
		folder->entries = g_slist_concat (subfolders, folder->entries);
	}

	/* Run query */
	folder->entries = run_query (info, folder->entries, folder->query);

	/* Exclude excludes */
	if (folder->excludes != NULL) {
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
	}

	folder->up_to_date = TRUE;
	/* not yet sorted */
	folder->sorted = FALSE;
}

/* get entries in folder */
static void
ensure_folder_sort (VFolderInfo *info, Folder *folder)
{
	ensure_folder (info, folder);
	if (folder->sorted)
		return;

	/* FIXME: sort */

	folder->sorted = TRUE;
}

static Folder *
file_new (const char *name,
	  const char *filename)
{
	EntryFile *efile = g_new0 (EntryFile, 1);

	efile->entry.type = ENTRY_FILE;
	efile->entry.name = g_strdup (name);
	efile->entry.refcount = 1;
	efile->filename = g_strdup (filename);

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

static void
vfolder_info_init (VFolderInfo *info)
{
	const char *path;
	GSList *list;

	/* Init for programs: */
	info->filename = g_strconcat (SYSCONFDIR, "/gnome-vfs-2.0/vfolders/vfolder-info.xml", NULL);
	info->desktop_dir = g_strconcat (SYSCONFDIR, "/gnome-vfs-2.0/vfolders/", NULL);

	/* Init the desktop paths */
	list = NULL;
	list = g_slist_prepend (list, g_strdup ("/usr/share/applications/"));
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
	info->item_dirs = g_slist_reverse (list);;

	info->entries_ht = g_hash_table_new (g_str_hash, g_str_equal);

	info->root = NULL;
}

static void
vfolder_info_destroy (VFolderInfo *info)
{
	if (info == NULL)
		return;

	g_free (info->filename);
	info->filename = NULL;

	g_free (info->desktop_dir);
	info->desktop_dir = NULL;

	g_slist_foreach (info->item_dirs, (GFunc)g_free, NULL);
	g_slist_free (info->item_dirs);
	info->item_dirs = NULL;

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

static Folder *
folder_read (xmlNode *node)
{
	/* FIXME */
	return NULL;
}

/* Read vfolder-info.xml file */
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
 *       <Exclude>yetanother.desktop</Include>
 *     </Folder>
 *   </Folder>
 * </VFolderInfo>
 */

static void
vfolder_info_read_info (VFolderInfo *info)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	gboolean got_a_vfolder_dir = FALSE;

	doc = xmlParseFile (info->filename); 

	if (doc == NULL
	    || doc->xmlRootNode == NULL
	    || doc->xmlRootNode->name == NULL
	    || g_ascii_strcasecmp (doc->xmlRootNode->name, "VFolderInfo") != 0) {
		xmlFreeDoc(doc);
		return;
	}

	for (node = doc->xmlRootNode->xmlChildrenNode; node != NULL; node = node->next) {
		/* is this even possible? */
		if (node->name == NULL)
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
		} else if (g_ascii_strcasecmp (node->name, "DesktopDir") == 0) {
			xmlChar *dir = xmlNodeGetContent (node);
			if (dir != NULL) {
				g_free (info->desktop_dir);
				info->desktop_dir = g_strdup (dir);
				xmlFree (dir);
			}
		} else if (g_ascii_strcasecmp (node->name, "Folder") == 0) {
			Folder *folder = folder_read (node);
			if (folder != NULL) {
				if (info->root != NULL)
					entry_unref ((Entry *)info->root);
				info->root = folder;
			}
		}
	}

	xmlFreeDoc(doc);
}

static void
vfolder_info_read_items (VFolderInfo *info)
{
	/*FIXME */
}

static void
ensure_info (void)
{
	if (programs_info != NULL)
		return;

	programs_info = g_new0 (VFolderInfo);
	vfolder_info_init (programs_info);

	vfolder_info_read_info (programs_info);

	vfolder_info_read_items (programs_info);
}

static Entry *
find_entry (GSList *list, const char *name)
{
	GSList *li;
	for (li = list; li != NULL; li = li->next) {
		Entry *entry = li->data;
		if (strcmp (name, entry->name) == 0)
			return subfolder;
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
		gboolean create,
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
		if (ppath[i] == NULL) {
			return folder;
		} else {
			folder = (Folder *)find_entry (folder->subfolders, segment)
		}
	}

	g_strfreev (ppath);
	*result = GNOME_VFS_ERROR_NOT_FOUND;
	return NULL;
}


static const char *
resolve_path (VFolderInfo *info,
	      const char *path,
	      const char *basename,
	      gboolean create,
	      GnomeVFSResult *result)
{
	Entry *entry;
	Folder *folder = resolve_folder (info, path, FALSE /* create */, result);

	if (folder == NULL)
		return NULL;

	if (strcmp (basename, ".directory") == 0) {
		if (folder->desktop_file == NULL &&
		    create) {
			/* FIXME: create desktop */
			/* FIXME: write vfolder */
		}
		if (folder->desktop_file == NULL)
			*result = GNOME_VFS_ERROR_NOT_FOUND;
		return folder->desktop_file;
	}

	/* Make sure we have the entries here */
	ensure_folder (programs_info, folder);

	entry = find_entry (folder->entries, basename);

	if (entry == NULL && create) {
		/* FIXME: create file, add to vfolder */
		/* FIXME: write vfolder */
		return NULL;
	} else if (entry == NULL) {
		*result = GNOME_VFS_ERROR_NOT_FOUND;
		return NULL;
	} else if (entry->type != ENTRY_FILE) {
		*result = GNOME_VFS_ERROR_IS_DIRECTORY;
		return NULL;
	} else {
		return ((EntryFile *)entry)->filename;
	}
}

static GnomeVFSURI*
desktop_uri_to_file_uri (GnomeVFSURI *desktop_uri,
			 gboolean create,
			 GnomeVFSResult *result)
{
	const char *path;
	const char *basename;
	const char *filename;
	GnomeVFSURI *uri;
	char *s;

	path = gnome_vfs_uri_get_path (desktop_uri);
	basename = gnome_vfs_uri_get_basename (desktop_uri);

	/* huh? */
	if (path == NULL ||
	    basename == NULL) {
		*result = GNOME_VFS_ERROR_INVALID_URI;
		return NULL;
	}

	ensure_info ();

	/* if this is just the filename, get just the filename */
	if (strchr (path, '/') == NULL) {
		EntryFile *efile;
		efile = g_hash_table_lookup (programs_info->entries_ht, path);
		if (efile == NULL && create) {
			/* FIXME: create file, add to root vfolder */
			/* FIXME: write vfolder */
		}
		if (efile == NULL) {
			*result = GNOME_VFS_ERROR_NOT_FOUND;
			return NULL;
		}
		filename = efile->filename;
	} else {
		filename = resolve_path (programs_info, path, basename, create, result);
		if (filename == NULL)
			return NULL;
	}

	s = gnome_vfs_get_uri_from_local_path (filename);

	uri = gnome_vfs_uri_new (s);
	g_free (s);

	*result = GNOME_VFS_OK;
	return uri;
}

static char*
create_file_uri_in_dir (const char *dir,
			const char *filename)
{
	char *dir_uri;
	char *retval;
	
	dir_uri = gnome_vfs_get_uri_from_local_path (dir);

	retval = g_strconcat (dir_uri, "/", filename, NULL);

	g_free (dir_uri);
	
	return retval;
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

	file_uri = desktop_uri_to_file_uri (uri, FALSE /* create */, &result);
	if (file_uri == NULL)
		return result;

	result = (* parent_method->open) (parent_method,
					  method_handle,
					  file_uri,
					  mode,
					  context);
	gnome_vfs_uri_unref (file_uri);

	return result;
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
	/* FIXME: For now a read only FS */
	return GNOME_VFS_ERROR_READ_ONLY;
	/*
	GnomeVFSURI *file_uri;
	GnomeVFSResult result;


	file_uri = desktop_uri_to_file_uri (uri);
	result = (* parent_method->create) (parent_method,
					    method_handle,
					    file_uri,
					    mode,
					    exclusive,
					    perm,
					    context);
	gnome_vfs_uri_unref (file_uri);

	return result;
	*/
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	
	result = (* parent_method->close) (parent_method,
					   method_handle,
					   context);

	return result;
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
	
	result = (* parent_method->read) (parent_method,
					  method_handle,
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
	
	result = (* parent_method->write) (parent_method,
					   method_handle,
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
	
	result = (* parent_method->seek) (parent_method,
					  method_handle,
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
	
	result = (* parent_method->tell) (parent_method,
					  method_handle,
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
	
	result = (* parent_method->truncate_handle) (parent_method,
						     method_handle,
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

	file_uri = desktop_uri_to_file_uri (uri, FALSE /* create */, &result);
	if (file_uri == NULL)
		return result;

	result = (* parent_method->truncate) (parent_method,
					      file_uri,
					      where,
					      context);

	gnome_vfs_uri_unref (file_uri);

	return result;
}

typedef struct _DirHandle DirHandle;
struct _DirHandle {
	Folder *folder;

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
	DirHandler *dh;
	Folder *folder;

	path = gnome_vfs_uri_get_path (uri);
	if (path == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	ensure_info ();

	folder = resolve_folder (programs_info, path, FALSE /* create */, &result);
	if (folder == NULL)
		return result;

	/* Make sure we have the entries and sorted here */
	ensure_folder_sort (programs_info, folder);

	dh = g_new0 (DirHandle, 1);
	dh->folder = (Folder *)entry_ref ((Entry *)folder);
	dh->list = g_slist_copy (folder->entries);
	g_slist_foreach (folder->list, (GFunc)entry_ref, NULL);
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
	dh->folder = NULL;

	g_free (dh);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	GnomeVFSResult result;
	DirHandle *dh;
	Entry *entry;

	dh = (DirHandle*) method_handle;

	if (dh->current == NULL) {
		return GNOME_VFS_ERROR_EOF;
	}

	entry = dh->current->data;
	dh->current = dh->current->next;

	if (entry->type == ENTRY_FILE) {
		EntryFile *efile = (EntryFile *)entry;
		char *furi = gnome_vfs_get_uri_from_local_path (efile->filename);
		GnomeVFSURI *uri = gnome_vfs_uri_new (furi);

		/* Get the file info for this */
		(* parent_method->get_file_info) (parent_method,
						  uri,
						  file_info,
						  0 /* options */,
						  context);

		/* we ignore errors from this since the file_info just
		 * won't be filled completely if there's an error, that's all */

		g_free (furi);
	} else /* ENTRY_FOLDER */ {
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

	file_uri = desktop_uri_to_file_uri (uri, FALSE /* create */, &result);
	if (file_uri == NULL) {
		const char *path;
		Folder *folder;
		path = gnome_vfs_uri_get_path (uri);
		if (path == NULL)
			return GNOME_VFS_ERROR_INVALID_URI;

		ensure_info ();

		folder = resolve_folder (programs_info, path, FALSE /* create */, &result);
		if (folder == NULL)
			return result;

		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_info->name = g_strdup (folder->entry.name);
		GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;

		file_info->mime_type = g_strdup ("x-directory/normal");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		return result;
	}

	result = (* parent_method->get_file_info) (parent_method,
						   file_uri,
						   file_info,
						   options,
						   context);

	gnome_vfs_uri_unref (file_uri);

	return result;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      GnomeVFSContext *context)
{
	GnomeVFSResult result;

	result = (* parent_method->get_file_info_from_handle) (parent_method,
							       method_handle,
							       file_info,
							       options,
							       context);

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
	/* FIXME: For now a read only FS */
	return GNOME_VFS_ERROR_READ_ONLY;
	/*
	GnomeVFSURI *file_uri;
	GnomeVFSResult result;

	file_uri = desktop_uri_to_file_uri (uri);
	result = (* parent_method->make_directory) (parent_method,
						    file_uri,
						    perm,
						    context);
	
	gnome_vfs_uri_unref (file_uri);

	return result;
	*/
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	/* FIXME: For now a read only FS */
	return GNOME_VFS_ERROR_READ_ONLY;
	/*
	GnomeVFSURI *file_uri;
	GnomeVFSResult result;

	file_uri = desktop_uri_to_file_uri (uri);
	result = (* parent_method->remove_directory) (parent_method,
						      file_uri,
						      context);
	
	gnome_vfs_uri_unref (file_uri);

	return result;
	*/
}

static GnomeVFSResult
do_find_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *near_uri,
		   GnomeVFSFindDirectoryKind kind,
		   GnomeVFSURI **result_uri,
		   gboolean create_if_needed,
		   gboolean find_if_needed,
		   guint permissions,
		   GnomeVFSContext *context)
{
	/* FIXME: Must figure out what the fuck this is */
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
	/*
	GnomeVFSURI *file_uri;
	GnomeVFSURI *file_result_uri;
	GnomeVFSResult result;

	file_result_uri = NULL;
	file_uri = desktop_uri_to_file_uri (near_uri);
	result = (* parent_method->find_directory) (parent_method,
						    file_uri,
						    kind,
						    &file_result_uri,
						    create_if_needed,
						    find_if_needed,
						    permissions,
						    context);
	
	gnome_vfs_uri_unref (file_uri);

	if (result_uri)
		*result_uri = file_result_uri;
	
	if (file_result_uri == NULL)
		result = GNOME_VFS_ERROR_NOT_FOUND;
		
	return result;
	*/
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	/* FIXME: For now a read only FS */
	return GNOME_VFS_ERROR_READ_ONLY;
	/*
	GnomeVFSURI *old_file_uri;
	GnomeVFSURI *new_file_uri;
	GnomeVFSResult result;

	old_file_uri = desktop_uri_to_file_uri (old_uri);
	new_file_uri = desktop_uri_to_file_uri (new_uri);

	result = (* parent_method->move) (parent_method,
					  old_file_uri,
					  new_file_uri,
					  force_replace,
					  context);
	gnome_vfs_uri_unref (old_file_uri);
	gnome_vfs_uri_unref (new_file_uri);

	return result;
	*/
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	/* FIXME: For now a read only FS */
	return GNOME_VFS_ERROR_READ_ONLY;
	/*
	GnomeVFSURI *file_uri;
	GnomeVFSResult result;

	file_uri = desktop_uri_to_file_uri (uri);
	result = (* parent_method->unlink) (parent_method,
					    file_uri,
					    context);
	
	gnome_vfs_uri_unref (file_uri);

	return result;	
	*/
}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod *method,
			 GnomeVFSURI *uri,
			 const char *target_reference,
			 GnomeVFSContext *context)
{
	/* FIXME: For now a read only FS */
	/* FIXME: I don't think we want to support symlinks anyway */
	return GNOME_VFS_ERROR_READ_ONLY;
	/*
	GnomeVFSURI *file_uri;
	GnomeVFSResult result;

	file_uri = desktop_uri_to_file_uri (uri);
	result = (* parent_method->create_symbolic_link) (parent_method,
							  file_uri,
							  target_reference,
							  context);
	
	gnome_vfs_uri_unref (file_uri);

	return result;	
	*/
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *source_uri,
		  GnomeVFSURI *target_uri,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{
	*same_fs_return = TRUE;
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{
	GnomeVFSURI *file_uri;
	GnomeVFSResult result;

	/* FIXME: what to do with folders? I suppose nothing */
	file_uri = desktop_uri_to_file_uri (uri, FALSE /* create */, &result);
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
	do_find_directory,
	do_create_symbolic_link
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, 
		 const char *args)
{
	int i;
	
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
	if (programs_info == NULL)
		return;

	vfolder_info_destroy (programs_info);
	programs_info = NULL;
}
