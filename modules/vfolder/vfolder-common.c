
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <sys/time.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-xfer.h>

#include "vfolder-common.h"


/* 
 * Entry Implementation
 */
Entry *
entry_new (VFolderInfo *info, 
	   gchar       *filename, 
	   gchar       *displayname, 
	   gboolean     user_private)
{
	Entry *entry;

	entry = g_new0 (Entry, 1);
	entry->refcnt = 1;
	entry->allocs = 0;
	entry->info = info;
	entry->filename = g_strdup (filename);
	entry->displayname = g_strdup (displayname);
	entry->user_private = user_private;

	/* Lame-O special case .directory handling */
	if (strcmp (displayname, ".directory") != 0)
		vfolder_info_add_entry (info, entry);

	return entry;
}

void 
entry_ref (Entry *entry)
{
	entry->refcnt++;
}

void 
entry_unref (Entry *entry)
{
	entry->refcnt--;

	if (entry->refcnt == 0) {
		vfolder_info_remove_entry (entry->info, entry);

		g_free (entry->filename);
		g_free (entry->displayname);
		g_slist_free (entry->keywords);
		g_free (entry);
	}
}

void
entry_alloc (Entry *entry)
{
	entry->allocs++;
}

void
entry_dealloc (Entry *entry)
{
	entry->allocs--;
}

gboolean 
entry_is_allocated (Entry *entry)
{
	return entry->allocs == 0;
}

static gchar *
uniqueify_file_name (gchar *file)
{
	struct timeval tv;
	gchar *ext;
	int len = strlen (file);

	gettimeofday (&tv, NULL);

	ext = strstr (file, ".desktop");
	if (ext) 
		len -= strlen (".desktop");
	
	return g_strdup_printf ("%*s-%d.desktop", 
				len, 
				file, 
				(int) (tv.tv_sec ^ tv.tv_usec));
}

gboolean 
entry_make_user_private (Entry *entry)
{
	GnomeVFSURI *src_uri, *dest_uri;
	GnomeVFSResult result;
	gchar *uniqname, *filename;

	if (entry->user_private)
		return TRUE;

	src_uri = entry_get_real_uri (entry);

	uniqname = uniqueify_file_name (entry_get_displayname (entry));
	filename = g_build_path (entry->info->write_dir, uniqname, NULL);
	g_free (uniqname);

	dest_uri = gnome_vfs_uri_new (filename);

	result = gnome_vfs_xfer_uri (src_uri, 
				     dest_uri, 
				     GNOME_VFS_XFER_USE_UNIQUE_NAMES, 
				     GNOME_VFS_XFER_ERROR_MODE_ABORT, 
				     GNOME_VFS_XFER_OVERWRITE_MODE_ABORT, 
				     NULL, 
				     NULL);

	gnome_vfs_uri_unref (src_uri);
	gnome_vfs_uri_unref (dest_uri);

	if (result == GNOME_VFS_OK) {
		entry_set_filename (entry, filename);
		entry->user_private = TRUE;		
	}

	g_free (filename);
	
	return result == GNOME_VFS_OK;
}

gboolean
entry_is_user_private (Entry *entry)
{
	return entry->user_private;
}

void
entry_set_dirty (Entry *entry)
{
	gboolean changed = FALSE;
	gchar *keywords, *only_show_in;
	int i;

	entry_quick_read_keys (entry, 
			       "Categories",
			       &keywords,
			       "OnlyShowIn",
			       &only_show_in);

	if (keywords) {
		char **parsed = g_strsplit (keywords, ";", -1);
		GSList *keylist = entry_get_keywords (entry);

		for (i = 0; parsed[i] != NULL; i++) {
			GQuark quark;
			const char *word = parsed[i];

			/* ignore empties (including end of list) */
			if (word[0] == '\0')
				continue;

			quark = g_quark_from_string (word);
			if (!g_slist_find (keylist, GINT_TO_POINTER (quark))) {
				D (g_print ("ADDING KEYWORD: %s\n", word));
				entry_add_implicit_keyword (
					entry, 
					g_quark_from_string (word));
				changed = TRUE;
			}
		}
		g_strfreev (parsed);
	}

	/* FIXME: Support this */
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
	}

	g_free (only_show_in);
	g_free (keywords);
}

void          
entry_set_filename (Entry *entry, gchar *name)
{
	g_free (entry->filename);
	entry->filename = g_strdup (name);

	entry_set_dirty (entry);
}

gchar *
entry_get_filename (Entry *entry)
{
	return entry->filename;
}

void
entry_set_displayname (Entry *entry, gchar *name)
{
	g_free (entry->displayname);
	entry->displayname = g_strdup (name);
}

gchar *
entry_get_displayname (Entry *entry)
{
	return entry->displayname;
}

GnomeVFSURI *
entry_get_real_uri (Entry *entry)
{
	if (!entry->uri)
		entry->uri = gnome_vfs_uri_new (entry->filename);

	gnome_vfs_uri_ref (entry->uri);
	return entry->uri;
}

GSList *
entry_get_keywords (Entry *entry)
{
	return entry->keywords;
}

void 
entry_add_implicit_keyword (Entry *entry, GQuark keyword)
{
	entry->has_implicit_keywords = TRUE;
	entry->keywords = g_slist_prepend (entry->keywords, 
					   GINT_TO_POINTER (keyword));
}

void 
entry_quick_read_keys (Entry  *entry,
		       gchar  *key1,
		       gchar **result1,
		       gchar  *key2,
		       gchar **result2)
{
	GnomeVFSHandle *handle;
	GnomeVFSFileSize readlen;
	char buf[1025];
	int keylen1, keylen2;

	*result1 = NULL;
	if (result2)
		*result2 = NULL;

	if (gnome_vfs_open (&handle, 
			    entry->filename, 
			    GNOME_VFS_OPEN_READ) != GNOME_VFS_OK)
		return;

	keylen1 = strlen (key1);
	if (key2 != NULL)
		keylen2 = strlen (key2);
	else
		keylen2 = -1;

	/* This is slightly wrong, it should only look
	 * at the correct section */
	while (gnome_vfs_read (handle, 
			       buf, 
			       sizeof (buf) - 1, 
			       &readlen) == GNOME_VFS_OK) {
		char *p;
		int len;
		int keylen;
		char **result = NULL;

		buf [readlen] = '\0';

		/* check if it's one of the keys */
		if (strncmp (buf, key1, keylen1) == 0) {
			result = result1;
			keylen = keylen1;
		} else if (keylen2 >= 0 && strncmp (buf, key2, keylen2) == 0) {
			result = result2;
			keylen = keylen2;
		} else {
			continue;
		}

		p = &buf[keylen];

		/* still not our key */
		if (!(*p == '=' || *p == ' ')) {
			continue;
		}
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

	gnome_vfs_close (handle);
}

void
entry_dump (Entry *entry, int indent)
{
	gchar *space = g_strnfill (indent, ' ');
	GSList *keywords = entry_get_keywords (entry), *iter;

	g_print ("%s%s\n%s  Filename: %s\n%s  Keywords: ",
		 space,
		 entry_get_displayname (entry),
		 space,
		 entry_get_filename (entry),
		 space);

	for (iter = keywords; iter; iter = iter->next) {
		GQuark quark = GPOINTER_TO_INT (iter->data);
		g_print (g_quark_to_string (quark));
	}

	g_print ("\n");

	g_free (space);
}



/* 
 * Folder Implementation
 */
Folder *
folder_new (VFolderInfo *info, gchar *name)
{
	Folder *folder = g_new0 (Folder, 1);
	folder->name = g_strdup (name);
	folder->info = info;
	folder->refcnt = 1;
	return folder;
}

void 
folder_ref (Folder *folder)
{
	folder->refcnt++;
}

void
folder_unref (Folder *folder)
{
	folder->refcnt--;

	if (folder->refcnt == 0) {
		D (g_print ("DESTORYING FOLDER: %s\n", folder->name));

		g_free (folder->name);
		g_free (folder->extend_uri);
		query_free (folder->query);

		if (folder->excludes)
			g_hash_table_destroy (folder->excludes);
		if (folder->includes_ht)
			g_hash_table_destroy (folder->includes_ht);
		g_slist_free (folder->includes);

		g_slist_foreach (folder->subfolders, 
				 (GFunc) folder_unref, 
				 NULL);
		g_slist_free (folder->subfolders);

		if (folder->subfolders_ht)
			g_hash_table_destroy (folder->subfolders_ht);

		g_slist_foreach (folder->entries, 
				 (GFunc) entry_unref, 
				 NULL);
		g_slist_free (folder->entries);

		if (folder->entries_ht)
			g_hash_table_destroy (folder->entries_ht);

		g_free (folder);
	}
}

gboolean
folder_make_user_private (Folder *folder)
{	
	if (folder->user_private)
		return TRUE;

	if (folder->parent) {
		if (folder->parent->read_only ||
		    !folder_make_user_private (folder->parent))
			return FALSE;

		if (!folder->parent->has_user_private_subfolders) {
			Folder *iter;

			for (iter = folder->parent; iter; iter = iter->parent)
				iter->has_user_private_subfolders = TRUE;
		}
	}

	folder->user_private = TRUE;

	vfolder_info_set_dirty (folder->info);

	return TRUE;
}

gboolean 
folder_is_user_private (Folder *folder)
{
	return folder->user_private;
}

/* 1 = Include, -1 = Exclude, 0 = Unknown */
static int
check_include_exclude (Folder *folder, gchar *name)
{
	int retval = 0;

	if (folder->includes_ht && 
	    g_hash_table_lookup (folder->includes_ht, name))
		retval++;

	if (folder->excludes &&
	    g_hash_table_lookup (folder->excludes, name))
		retval--;

	return retval;
}

static gboolean
read_extended_entries (Folder *folder)
{
	GnomeVFSResult result;
	Query *query;
	GList *flist, *iter;
	gboolean changed = FALSE;
	gchar *extend_uri;

	query = folder_get_query (folder);

	extend_uri = folder_get_extend_uri (folder);
	result = gnome_vfs_directory_list_load (&flist,
						extend_uri,
						GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return FALSE;

	for (iter = flist; iter; iter = iter->next) {
		GnomeVFSFileInfo *file_info = iter->data;
		gchar *file_uri;

		file_uri = g_build_filename (extend_uri, file_info->name, NULL);

		if (file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			Folder *sub;

			/* Include/Exclude of directories is just dirname */
			if (check_include_exclude (folder, 
						   file_info->name) == -1) {
				g_free (file_uri);
				continue;
			}

			/* Don't allow duplicate named subfolders */
			if (folder_get_subfolder (folder, file_info->name)) {
				g_free (file_uri);
				continue;
			}

			sub = folder_new (folder->info, file_info->name);
			folder_set_extend_uri (sub, file_uri);

			folder_add_subfolder (folder, sub);

			changed = TRUE;
		}
		else {
			Entry *entry;

			/* Include/Exclude of files are full uris */
			if (check_include_exclude (folder, file_uri) == -1) {
				g_free (file_uri);
				continue;
			}

			// FIXME: uniqueify file_info.name

			entry = entry_new (folder->info, 
					   file_uri,
					   file_info->name, 
					   FALSE);
			entry_set_filename (entry, file_uri);

			/* Include unless specifically excluded by query */
			if (!query || query_try_match (query, folder, entry)) {
				D (g_print ("ADDING EXTENDED ENTRY: "
					    "%s, %s, #%d!\n",
					    folder_get_name (folder),
					    entry_get_displayname (entry),
					    g_slist_length ((GSList*)
						folder_list_entries (folder))));

				folder_add_entry (folder, entry);
				entry_alloc (entry);

				changed = TRUE;
			}
			else 
				entry_unref (entry);
		}

		g_free (file_uri);
	}

	gnome_vfs_file_info_list_free (flist);

	return changed;
}

static gboolean
read_info_entry_pool (Folder *folder)
{
	GSList *all_entries, *iter;
	Query *query;
	gboolean changed = FALSE;

	if (folder->only_unallocated)
		return FALSE;

	query = folder_get_query (folder);
	all_entries = vfolder_info_list_all_entries (folder->info);

	for (iter = all_entries; iter; iter = iter->next) {
		Entry *entry = iter->data;
		int include;

		include = check_include_exclude (folder, 
						 entry_get_filename (entry));
		if (include == -1)
			continue;

		/* Only include if matches query, or is explicitly included */
		if (include == 1 || 
		    (query && query_try_match (query, folder, entry))) {
			D (g_print ("ADDING POOL ENTRY: %s, %s, #%d!!!!\n",
				    folder_get_name (folder),
				    entry_get_displayname (entry),
				    g_slist_length ((GSList*)
					    folder_list_entries (folder))));

			folder_add_entry (folder, entry);
			entry_alloc (entry);

			changed = TRUE;
		} 
	}

	return changed;
}

static void
folder_emit_changed (Folder                   *folder,
		     GnomeVFSMonitorEventType  event_type)
{
	Folder *iter;
	gchar *uri = NULL, *tmp;

	for (iter = folder; 
	     iter != NULL && iter != folder->info->root; 
	     iter = iter->parent) {
		tmp = g_strconcat ("/", 
				   folder_get_name (iter), 
				   uri,
				   NULL);
		g_free (uri);
		uri = tmp;
	}
	
	vfolder_info_emit_change (folder->info, uri, event_type);

	g_free (uri);
}

static void
folder_reload_if_needed (Folder *folder)
{
	gboolean changed = FALSE;

	if (!folder->dirty || folder->loading)
		return;

	folder->loading = TRUE;

	if (folder_get_extend_uri (folder)) 
		changed = read_extended_entries (folder);

	if (folder_get_query (folder))
		changed = read_info_entry_pool (folder);

	if (changed)
		folder_emit_changed (folder, GNOME_VFS_MONITOR_EVENT_CHANGED);	

	folder->loading = FALSE;
	folder->dirty = FALSE;
}

void
folder_set_dirty (Folder *folder)
{
	folder->dirty = TRUE;
}

void 
folder_set_name (Folder *folder, gchar *name)
{
	g_free (folder->name);
	folder->name = g_strdup (name);

	vfolder_info_set_dirty (folder->info);
}

gchar *
folder_get_name (Folder *folder)
{
	return folder->name;
}

void
folder_set_query (Folder *folder, Query *query)
{
	if (folder->query)
		query_free (folder->query);

	folder->query = query;

	folder_set_dirty (folder);
	vfolder_info_set_dirty (folder->info);
}

Query *
folder_get_query (Folder *folder)
{
	return folder->query;
}

void
folder_set_extend_uri (Folder *folder, gchar *uri)
{
	g_free (folder->extend_uri);
	folder->extend_uri = g_strdup (uri);

	folder_set_dirty (folder);
	vfolder_info_set_dirty (folder->info);
}

gchar *
folder_get_extend_uri (Folder *folder)
{
	return folder->extend_uri;
}

void 
folder_set_desktop_file (Folder *folder, gchar *filename)
{
	g_free (folder->desktop_file);
	folder->desktop_file = g_strdup (filename);

	folder_set_dirty (folder); /* Is this needed? Just cache the sort */
	vfolder_info_set_dirty (folder->info);
}

gchar *
folder_get_desktop_file (Folder *folder)
{
	return folder->desktop_file;
}

gboolean 
folder_get_child  (Folder *folder, gchar *name, FolderChild *child)
{
	Folder *subdir;
	Entry *file;

	memset (child, 0, sizeof (FolderChild));

	if (name)
		subdir = folder_get_subfolder (folder, name);
	else
		/* No name, just return the parent folder */
		subdir = folder;

	if (subdir) {
		child->type = FOLDER;
		child->folder = subdir;
		return TRUE;
	}

	file = folder_get_entry (folder, name);
	if (file) {
		child->type = DESKTOP_FILE;
		child->entry = file;
		return TRUE;
	}

	if (folder->only_unallocated) {
		file = vfolder_info_lookup_entry (folder->info, name);
		if (file) {
			child->type = DESKTOP_FILE;
			child->entry = file;
			return TRUE;
		}
	}

	return FALSE;
}

Entry *
folder_get_entry (Folder *folder, gchar *filename)
{
	folder_reload_if_needed (folder);

	if (!folder->entries_ht)
		return NULL;

	return g_hash_table_lookup (folder->entries_ht, filename);
}

const GSList *
folder_list_entries (Folder *folder)
{
	folder_reload_if_needed (folder);

	return folder->entries;
}

/* 
 * This doesn't set the folder dirty. 
 * Use the include/exclude functions for that.
 */
void 
folder_remove_entry (Folder *folder, Entry *entry)
{
	gchar *name;

	if (!folder->entries_ht)
		return;

	name = entry_get_displayname (entry);
	if (g_hash_table_lookup (folder->entries_ht, name)) {
		g_hash_table_remove (folder->entries_ht, name);
		folder->entries = g_slist_remove (folder->entries, entry);

		entry_unref (entry);
	}
}

/* 
 * This doesn't set the folder dirty. 
 * Use the include/exclude functions for that.
 */
void 
folder_add_entry (Folder *folder, Entry *entry)
{
	folder_remove_entry (folder, entry);

	if (!folder->entries_ht) 
		folder->entries_ht = g_hash_table_new (g_str_hash, g_str_equal);

	g_hash_table_insert (folder->entries_ht, 
			     entry_get_displayname (entry),
			     entry);
	folder->entries = g_slist_append (folder->entries, entry);

	entry_ref (entry);
}

void
folder_add_include (Folder *folder, gchar *include)
{
	char *str = g_strdup (include);
	
	folder_remove_include (folder, include);
	
	if (!folder->includes_ht)
		folder->includes_ht = 
			g_hash_table_new_full (g_str_hash, 
					       g_str_equal,
					       (GDestroyNotify) g_free,
					       NULL);

	folder->includes = g_slist_prepend (folder->includes, str);
	g_hash_table_insert (folder->includes_ht, str, folder->includes);

	folder_set_dirty (folder);
	vfolder_info_set_dirty (folder->info);
}

void 
folder_remove_include (Folder *folder, gchar *file)
{
	GSList *li;

	if (!folder->includes_ht)
		return;

	li = g_hash_table_lookup (folder->includes_ht, file);
	if (li) {
		g_free (li->data);
		/* 
		 * Note: this will NOT change
		 * folder->includes pointer! 
		 */
		folder->includes = g_slist_delete_link (folder->includes, li);
		g_hash_table_remove (folder->includes_ht, li);
	}

	folder_set_dirty (folder);
	vfolder_info_set_dirty (folder->info);
}

void
folder_add_exclude (Folder *parent, gchar *exclude)
{
	char *s;

	if (!parent->excludes)
		parent->excludes = 
			g_hash_table_new_full (g_str_hash,
					       g_str_equal,
					       (GDestroyNotify) g_free,
					       NULL);

	s = g_strdup (exclude);
	g_hash_table_replace (parent->excludes, s, s);

	folder_set_dirty (parent);
	vfolder_info_set_dirty (parent->info);
}

void 
folder_remove_exclude (Folder *folder, gchar *file)
{
	if (!folder->excludes)
		return;

	g_hash_table_remove (folder->excludes, file);

	folder_set_dirty (folder);
	vfolder_info_set_dirty (folder->info);
}

Folder *
folder_get_subfolder (Folder *folder, gchar *name)
{
	Folder *ret; 

	folder_reload_if_needed (folder);

	if (!folder->subfolders_ht)
		return NULL;

	ret = g_hash_table_lookup (folder->subfolders_ht, name);
	if (ret) 
		folder_ref (ret);

	return ret;
}

const GSList * 
folder_list_subfolders (Folder *parent)
{
	folder_reload_if_needed (parent);

	return parent->subfolders;
}

void
folder_remove_subfolder (Folder *parent, Folder *child)
{
	gchar *name;

	if (child->parent != parent || !parent->subfolders_ht)
		return;

	name = folder_get_name (child);

	if (!child->user_private) {
		folder_remove_include (parent, name);
		folder_add_exclude (parent, name);
	}

	g_hash_table_remove (parent->subfolders_ht, name);
	parent->subfolders = g_slist_remove (parent->subfolders, child);

	folder_emit_changed (child, GNOME_VFS_MONITOR_EVENT_DELETED);
	folder_unref (child);

	folder_set_dirty (parent);
	vfolder_info_set_dirty (parent->info);
}

void
folder_add_subfolder (Folder *parent, Folder *child)
{
	if (child->parent == parent)
		return;

	if (child->user_private && !parent->has_user_private_subfolders) {
		Folder *iter;

		for (iter = parent; iter != NULL; iter = iter->parent)
			iter->has_user_private_subfolders = TRUE;
	}

	if (!parent->subfolders_ht)
		parent->subfolders_ht = g_hash_table_new (g_str_hash, 
							  g_str_equal);

	g_hash_table_insert (parent->subfolders_ht, 
			     folder_get_name (child),
			     child);
	parent->subfolders = g_slist_append (parent->subfolders, child);

	child->parent = parent;

	folder_ref (child);
	folder_emit_changed (child, GNOME_VFS_MONITOR_EVENT_CREATED);

	folder_set_dirty (parent);
	vfolder_info_set_dirty (parent->info);
}

void
folder_dump_tree (Folder *folder, int indent)
{
	const GSList *iter;
	gchar *space = g_strnfill (indent, ' ');

	g_print ("%s(%p): %s\n",
		 space,
		 folder,
		 folder ? folder_get_name (folder) : NULL);

	g_free (space);

	for (iter = folder_list_subfolders (folder); iter; iter = iter->next) {
		Folder *child = iter->data;

		folder_dump_tree (child, indent + 2);
	}
}

/* This is a pretty lame hack */
gboolean
folder_is_hidden (Folder *folder)
{
	const GSList *iter, *ents;

	if (folder->hidden || 
	    folder->dont_show_if_empty == FALSE)
		return FALSE;

	if (folder->only_unallocated) {
		iter = vfolder_info_list_all_entries (folder->info);
		for (; iter; iter = iter->next) {
			Entry *entry = iter->data;

			if (!entry_is_allocated (entry))
				return FALSE;
		}
	}

	ents = folder_list_entries (folder);
	if (ents) {
		/* If there is only one entry, check it is not .directory */
		if (!ents->next) {
			Entry *dot_directory = ents->data;
			gchar *name = entry_get_displayname (dot_directory);

			if (strcmp (".directory", name) != 0)
				return FALSE;
		} else
			return FALSE;
	}

	iter = folder_list_subfolders (folder);
	for (; iter; iter = iter->next) {
		Folder *child = iter->data;

		if (!folder_is_hidden (child))
			return FALSE;
	}

	return TRUE;
}



/* 
 * Query Implementation
 */
Query *
query_new (int type)
{
	Query *query;

	query = g_new0 (Query, 1);
	query->type = type;

	return query;
}

void
query_free (Query *query)
{
	if (query == NULL)
		return;

	if (query->type == QUERY_OR || query->type == QUERY_AND) {
		g_slist_foreach (query->val.queries, 
				 (GFunc) query_free, 
				 NULL);
		g_slist_free (query->val.queries);
	}
	else if (query->type == QUERY_FILENAME)
		g_free (query->val.filename);

	g_free (query);
}

#define INVERT_IF_NEEDED(val) (query->not ? !(val) : (val))

gboolean
query_try_match (Query  *query,
		 Folder *folder,
		 Entry  *efile)
{
	GSList *li;

	if (query == NULL)
		return TRUE;

	switch (query->type) {
	case QUERY_OR:
		for (li = query->val.queries; li != NULL; li = li->next) {
			Query *subquery = li->data;

			if (query_try_match (subquery, folder, efile))
				return INVERT_IF_NEEDED (TRUE);
		}
		return INVERT_IF_NEEDED (FALSE);
	case QUERY_AND:
		for (li = query->val.queries; li != NULL; li = li->next) {
			Query *subquery = li->data;

			if (!query_try_match (subquery, folder, efile))
				return INVERT_IF_NEEDED (FALSE);
		}
		return INVERT_IF_NEEDED (TRUE);
	case QUERY_PARENT:
		{
			gchar *extend_uri;
			
			extend_uri = folder_get_extend_uri (folder);
			if (extend_uri &&
			    strncmp (entry_get_filename (efile), 
				     extend_uri,
				     strlen (extend_uri)) == 0) 
				return INVERT_IF_NEEDED (TRUE);
			else
				return INVERT_IF_NEEDED (FALSE);
		}
	case QUERY_KEYWORD:
		{ 
			GSList *keywords = entry_get_keywords (efile);

			for (li = keywords; li; li = li->next) {
				GQuark keyword = GPOINTER_TO_INT (li->data);

				if (keyword == query->val.keyword)
					return INVERT_IF_NEEDED (TRUE);
			}
		}
		return INVERT_IF_NEEDED (FALSE);
	case QUERY_FILENAME:
		if (strchr (query->val.filename, '/') &&
		    !strcmp (query->val.filename, entry_get_filename (efile)))
			return INVERT_IF_NEEDED (TRUE);
		else if (!strcmp (query->val.filename, 
				  entry_get_displayname (efile)))
			return INVERT_IF_NEEDED (TRUE);
		else
			return INVERT_IF_NEEDED (FALSE);
	}

	g_assert_not_reached ();
	return FALSE;
}
