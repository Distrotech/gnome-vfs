
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>

#include "vfolder-util.h"
#include "vfolder-common.h"

/* assumes vuri->path already set */
gboolean
vfolder_uri_parse_internal (GnomeVFSURI *uri, VFolderURI *vuri)
{
	vuri->scheme = (gchar *) gnome_vfs_uri_get_scheme (uri);

	vuri->ends_in_slash = FALSE;

	if (strncmp (vuri->scheme, "all-", strlen ("all-")) == 0) {
		vuri->scheme += strlen ("all-");
		vuri->is_all_scheme = TRUE;
	} else
		vuri->is_all_scheme = FALSE;

	if (vuri->path != NULL) {
		int last_slash = strlen (vuri->path) - 1;
		char *first;

		/* Note: This handling of paths is somewhat evil, may need a
		 * bit of a rework */

		/* kill leading slashes, that is make sure there is
		 * only one */
		for (first = vuri->path; *first == '/'; first++)
			;
		if (first != vuri->path) {
			first--;
			vuri->path = first;
		}

		/* kill trailing slashes (leave first if all slashes) */
		while (last_slash > 0 && vuri->path [last_slash] == '/') {
			vuri->path [last_slash--] = '\0';
			vuri->ends_in_slash = TRUE;
		}

		/* get basename start */
		while (last_slash >= 0 && vuri->path [last_slash] != '/')
			last_slash--;

		if (last_slash > -1)
			vuri->file = vuri->path + last_slash + 1;
		else
			vuri->file = vuri->path;

		if (vuri->file[0] == '\0' &&
		    strcmp (vuri->path, "/") == 0) {
			vuri->file = NULL;
		}
	} else {
		vuri->ends_in_slash = TRUE;
		vuri->path = "/";
		vuri->file = NULL;
	}

	vuri->uri = uri;

	return TRUE;
}


gboolean
check_extension (const char *name, const char *ext_check)
{
	const char *ext;

	ext = strrchr (name, '.');
	if (ext && !strcmp (ext, ext_check))
		return TRUE;
	else
		return FALSE;
}

#if 0
static void
dump_unallocated_folders (Folder *folder)
{
	GSList *li;
	for (li = folder->subfolders; li != NULL; li = li->next)
		dump_unallocated_folders (li->data);

	if (folder->only_unallocated &&
	    folder->entries != NULL) {
		g_slist_foreach (folder->entries,
				 (GFunc) entry_dealloc, 
				 NULL);
		g_slist_foreach (folder->entries,
				 (GFunc) entry_unref, 
				 NULL);
		g_slist_free (folder->entries);
		folder->entries = NULL;
	}
}

/* Run query, allocs and refs the entries */
void
append_query (VFolderInfo *info, Folder *folder)
{
	GSList *li;
	GList *extend_file_infos = NULL;
	GnomeVFSResult result;

	if (folder->query == NULL &&
	    ! folder->only_unallocated)
		return;

	if (folder->only_unallocated) {
		/* dump all folders that use unallocated
		 * items only.  This sucks if you keep
		 * reading one and then another such
		 * folder, but oh well, life sucks for
		 * you then, but at least you get
		 * consistent results */
		dump_unallocated_folders (info->root);

		/* ensure all other folders, so that
		 * after this we know which ones are
		 * unallocated */
		ensure_folder_unlocked (info,
					info->root,
					TRUE /* subfolders */,
					folder /* except */,
					/* avoid infinite loops */
					TRUE /* ignore_unallocated */);
	}

	/* add vfolder's entry pool (gotten from mergedirs & itemdirs) */
	for (li = info->entries; li != NULL; li = li->next) {
		Entry *entry = li->data;
		
		/* don't include if already explicitly included */
		if (folder->includes_ht &&
		    g_hash_table_lookup (folder->includes_ht, 
					 entry_get_displayname (entry)))
			continue;

		/* don't include if explicitly excluded */
		if (folder->excludes &&
		    g_hash_table_lookup (folder->excludes, 
					 entry_get_displayname (entry)))
			continue;

		if (folder->only_unallocated && !entry_is_allocated (entry))
			continue;

		if (matches_query (folder, entry, folder->query)) { 
			folder_add_entry (folder, entry);
			entry_alloc (entry);
		}
	}

	result = gnome_vfs_directory_list_load (&extend_file_infos,
						folder_get_extend_uri (folder),
						GNOME_VFS_FILE_INFO_GET_MIME_TYPE);
	if (result == GNOME_VFS_OK) {
		GList *iter;

		for (iter = extend_file_infos; iter; iter = iter->next) {
			GnomeVFSFileInfo *finfo = iter->data;

			if (finfo->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
				Folder *known_sub;

				/* Check is this folder is already extended */
				known_sub = folder_get_subfolder (folder, 
								  finfo->name);
				if (!known_sub) {
					known_sub = folder_new (folder->info, 
								finfo->name);
					folder_add_subfolder 
				}
			} else {
			}
		}

		gnome_vfs_file_info_list_free (extend_file_infos);
	}
}
#endif


VFolderMonitor *
vfolder_monitor_directory_new (gchar                   *uri,
			       GnomeVFSMonitorCallback  callback,
			       gpointer                 user_data)
{
	// FIXME
	return NULL;
}

VFolderMonitor *
vfolder_monitor_file_new (gchar                   *uri,
			  GnomeVFSMonitorCallback  callback,
			  gpointer                 user_data)
{
	// FIXME
	return NULL;
}

void 
vfolder_monitor_freeze (VFolderMonitor *monitor)
{
}

void 
vfolder_monitor_thaw (VFolderMonitor *monitor)
{
}

void 
vfolder_monitor_cancel (VFolderMonitor *monitor)
{
}
