

#include <string.h>

#include <libgnomevfs/gnome-vfs-cancellable-ops.h>
#include <libgnomevfs/gnome-vfs-module.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "vfolder-common.h"
#include "vfolder-util.h"

#define UNSUPPORTED_INFO_FIELDS (GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS | \
				 GNOME_VFS_FILE_INFO_FIELDS_DEVICE | \
				 GNOME_VFS_FILE_INFO_FIELDS_INODE | \
				 GNOME_VFS_FILE_INFO_FIELDS_LINK_COUNT | \
				 GNOME_VFS_FILE_INFO_FIELDS_ATIME)


typedef struct {
	VFolderInfo *info;
	GnomeVFSHandle *handle;
	Entry *entry;
	gboolean write;
} FileHandle;

static FileHandle *
file_handle_new (GnomeVFSHandle *file_handle,
		 VFolderInfo *info,
		 Entry *entry,
		 gboolean write)
{
	if (file_handle != NULL) {
		FileHandle *handle = g_new0 (FileHandle, 1);

		handle->handle = file_handle;
		handle->info = info;
		handle->write = write;

		handle->entry = entry;
		entry_ref (entry);

		return handle;
	} else
		return NULL;
}

static void
file_handle_free (FileHandle *handle)
{
	entry_unref (handle->entry);
	g_free (handle);
}

#define NICE_UNLOCK_INFO(info, write) 		  	  \
	do {						  \
		if (write) 			          \
			VFOLDER_INFO_WRITE_UNLOCK (info); \
		else 					  \
			VFOLDER_INFO_READ_UNLOCK (info);  \
	} while (0)


/*
 * GnomeVFS Callbacks
 */
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
	Folder *parent;
	FolderChild child;
	GnomeVFSHandle *file_handle = NULL;
	FileHandle *vfolder_handle;
	VFolderURI vuri;
	gboolean want_write = mode & GNOME_VFS_OPEN_WRITE;

	VFOLDER_URI_PARSE (uri, &vuri);

	/* These can't be very nice FILE names */
	if (!vuri.file || vuri.ends_in_slash)
		return GNOME_VFS_ERROR_INVALID_URI;

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (want_write && (info->read_only || vuri.is_all_scheme))
		return GNOME_VFS_ERROR_READ_ONLY;

	if (want_write) 
		VFOLDER_INFO_WRITE_LOCK (info);
	else 
		VFOLDER_INFO_READ_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		NICE_UNLOCK_INFO (info, want_write);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (!folder_get_child (parent, vuri.file, &child)) {
		NICE_UNLOCK_INFO (info, want_write);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (child.type == FOLDER) {
		NICE_UNLOCK_INFO (info, want_write);
		return GNOME_VFS_ERROR_IS_DIRECTORY;
	}

	if (want_write) {
		if (!entry_make_user_private (child.entry, parent)) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_READ_ONLY;
		}
	}

	file_uri = entry_get_real_uri (child.entry);
	result = gnome_vfs_open_uri_cancellable (&file_handle,
						 file_uri,
						 mode,
						 context);
	gnome_vfs_uri_unref (file_uri);

	if (result == GNOME_VFS_ERROR_CANCELLED) {
		NICE_UNLOCK_INFO (info, want_write);
		return result;
	}

	vfolder_handle = file_handle_new (file_handle, 
					  info, 
					  child.entry, 
					  want_write);
	*method_handle = (GnomeVFSMethodHandle *) vfolder_handle;

	NICE_UNLOCK_INFO (info, want_write);

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
	GnomeVFSResult result = GNOME_VFS_OK;
	GnomeVFSHandle *file_handle;
	FileHandle *vfolder_handle;
	GnomeVFSURI *file_uri;
	VFolderURI vuri;
	VFolderInfo *info;
	Folder *parent;
	FolderChild child;
	Entry *new_entry;
	gchar *dirname, *filename;

	VFOLDER_URI_PARSE (uri, &vuri);

	/* These can't be very nice FILE names */
	if (vuri.file == NULL || vuri.ends_in_slash)
		return GNOME_VFS_ERROR_INVALID_URI;
	
	if (!vfolder_check_extension (vuri.file, ".desktop") &&
	    !vfolder_check_extension (vuri.file, ".directory")) {
		return GNOME_VFS_ERROR_INVALID_URI;
	}

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (info->read_only || vuri.is_all_scheme)
		return GNOME_VFS_ERROR_READ_ONLY;

	VFOLDER_INFO_WRITE_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (folder_get_child (parent, vuri.file, &child)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);

		if (child.type == FOLDER)
			return GNOME_VFS_ERROR_IS_DIRECTORY;
		else if (child.type == DESKTOP_FILE)
			return GNOME_VFS_ERROR_FILE_EXISTS;
	}

	/* 
	 * make a user-local copy, so the folder will be written to the user's
	 * private .vfolder-info file 
	 */
	if (!folder_make_user_private (parent)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	dirname = info->write_dir;
	if (dirname) {
		gchar *basename;

		/* Create uniquely named file in write_dir */
		basename = vfolder_timestamp_file_name (vuri.file);
		filename = vfolder_build_uri (dirname, basename, NULL);
		g_free (basename);
	} else {
		/* No writedir, try modifying the parent */
		dirname = folder_get_extend_uri (parent);
		if (!dirname) {
			/* Nowhere to create file, fail */
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_READ_ONLY;
		}

		/* Use regular filename in extended dir */
		filename = vfolder_build_uri (dirname, vuri.file, NULL);
	}

	/* Make sure the destination directory exists */
	result = vfolder_make_directory_and_parents (dirname, FALSE, 0700);
	if (result != GNOME_VFS_OK)
		return FALSE;

	file_uri = gnome_vfs_uri_new (filename);
	result = gnome_vfs_create_uri_cancellable (&file_handle,
						   file_uri,
						   mode,
						   exclusive,
						   perm,
						   context);
	gnome_vfs_uri_unref (file_uri);

	if (result != GNOME_VFS_OK) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return result;
	}

	/* Create it */
	new_entry = entry_new (info, filename, vuri.file, TRUE);
	g_free (filename);

	if (!new_entry) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	folder_add_entry (parent, new_entry);

	vfolder_info_emit_change (info, 
				  vuri.path, 
				  GNOME_VFS_MONITOR_EVENT_CREATED);

	vfolder_handle = file_handle_new (file_handle,
					  info,
					  new_entry,
					  mode & GNOME_VFS_OPEN_WRITE);
	*method_handle = (GnomeVFSMethodHandle *) vfolder_handle;

	VFOLDER_INFO_WRITE_UNLOCK (info);
	return result;
}


static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	GnomeVFSResult result;
	FileHandle *handle = (FileHandle *) method_handle;

	if (method_handle == (GnomeVFSMethodHandle *) method)
		return GNOME_VFS_OK;
	
	result = gnome_vfs_close_cancellable (handle->handle, context);

	if (handle->write) {
		VFOLDER_INFO_WRITE_LOCK (handle->info);
		entry_set_dirty (handle->entry);
		VFOLDER_INFO_WRITE_UNLOCK (handle->info);
	} 

	file_handle_free (handle);

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
	FileHandle *handle = (FileHandle *)method_handle;
	
	result = gnome_vfs_read_cancellable (handle->handle,
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

	result = gnome_vfs_write_cancellable (handle->handle,
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
	
	result = gnome_vfs_seek_cancellable (handle->handle,
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
	
	result = gnome_vfs_tell (handle->handle, offset_return);

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
	
	result = gnome_vfs_truncate_handle_cancellable (handle->handle,
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
	Folder *parent;
	FolderChild child;
	VFolderURI vuri;

	VFOLDER_URI_PARSE (uri, &vuri);

	/* These can't be very nice FILE names */
	if (vuri.file == NULL || vuri.ends_in_slash)
		return GNOME_VFS_ERROR_INVALID_URI;

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (info->read_only || vuri.is_all_scheme)
		return GNOME_VFS_ERROR_READ_ONLY;

	VFOLDER_INFO_WRITE_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (!folder_get_child (parent, vuri.file, &child)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (child.type == FOLDER) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_IS_DIRECTORY;
	}

	if (!entry_make_user_private (child.entry, parent)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	file_uri = entry_get_real_uri (child.entry);
	
	VFOLDER_INFO_WRITE_UNLOCK (info);

	result = gnome_vfs_truncate_uri_cancellable (file_uri, where, context);
	gnome_vfs_uri_unref (file_uri);

	return result;
}


typedef struct {
	VFolderInfo             *info;
	Folder                  *folder;

	GnomeVFSFileInfoOptions  options;

	/* List of Entries */
	GSList                  *list;
	GSList                  *current;
} DirHandle;

static void
dir_handle_foreach_prepend (gpointer key, gpointer val, gpointer user_data)
{
	gchar *name = key;
	GSList **list = user_data;

	*list = g_slist_prepend (*list, name);
}

static GSList * 
dir_handle_prepend_sorted (gchar      *sortorder, 
			   GHashTable *name_hash)
{
	GSList *ret = NULL;
	gchar **split_ord;
	int i;

	if (!sortorder)
		return NULL;

	split_ord = g_strsplit (sortorder, ":", -1);
	if (split_ord && split_ord [0]) {
		for (i = 0; split_ord [i]; i++) {
			gchar *name = split_ord [i];

			if (g_hash_table_lookup (name_hash, name)) {
				g_hash_table_remove (name_hash, name);
				ret = g_slist_prepend (ret, name);
			}
		}
	}

	return ret;
}

static DirHandle *
dir_handle_new (VFolderInfo             *info,
		Folder                  *folder,
		GnomeVFSFileInfoOptions  options)
{
	DirHandle *ret;
	Entry *dot_directory;
	const GSList *iter;
	GHashTable *name_hash;

	ret = g_new0 (DirHandle, 1);
	ret->info = info;
	ret->options = options;
	ret->folder = folder;
	folder_ref (folder);

	/* FIXME: handle duplicate names here, by not using a hashtable */

	name_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (iter = folder_list_subfolders (folder); iter; iter = iter->next) {
		Folder *child = iter->data;

		g_hash_table_insert (name_hash, 
				     folder_get_name (child),
				     NULL);
	}

	for (iter = folder_list_entries (folder); iter; iter = iter->next) {
		Entry *entry = iter->data;

		g_hash_table_insert (name_hash, 
				     entry_get_displayname (entry),
				     NULL);
	}

	if (folder->only_unallocated) {
		Query *query = folder_get_query (folder);

		iter = vfolder_info_list_all_entries (info);
		for (; iter; iter = iter->next) {
			Entry *entry = iter->data;

			if (entry_is_allocated (entry))
				continue;

			if (query && !query_try_match (query, folder, entry))
				continue;

			g_hash_table_insert (name_hash, 
					     entry_get_displayname (entry),
					     NULL);
		}
	}		

	dot_directory = folder_get_entry (folder, ".directory");
	if (dot_directory) {
		gchar *sortorder;
		entry_quick_read_keys (dot_directory,
				       "SortOrder",
				       &sortorder,
				       NULL, 
				       NULL);
		if (sortorder) {
			ret->list = dir_handle_prepend_sorted (sortorder,
							       name_hash);
			g_free (sortorder);
		}
	}

	g_hash_table_foreach (name_hash, 
			      (GHFunc) dir_handle_foreach_prepend,
			      &ret->list);
	g_hash_table_destroy (name_hash);

	ret->list = g_slist_reverse (ret->list);
	ret->current = ret->list;

	return ret;
}

static DirHandle *
dir_handle_new_all (VFolderInfo             *info,
		    GnomeVFSFileInfoOptions  options)
{
	DirHandle *ret = g_new0 (DirHandle, 1);

	ret->info = info;
	ret->options = options;
	ret->list = g_slist_copy (vfolder_info_list_all_entries (info));
	ret->current = ret->list;

	return ret;
}

static void
dir_handle_free (DirHandle *handle)
{
	folder_unref (handle->folder);
	
	g_slist_free (handle->list);
	g_free (handle);
}


static GnomeVFSResult
do_open_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   GnomeVFSContext *context)
{
	VFolderURI vuri;
	DirHandle *dh = NULL;
	Folder *folder;
	VFolderInfo *info;

	VFOLDER_URI_PARSE (uri, &vuri);

	/* Read lock is kept until close_directory */
	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	VFOLDER_INFO_READ_LOCK (info);

	/* In the all- scheme just list all filenames */
	if (vuri.is_all_scheme) {
		if (vuri.path && strchr (vuri.path, '/')) {
			VFOLDER_INFO_READ_UNLOCK (info);
			return GNOME_VFS_ERROR_NOT_FOUND;
		}

		dh = dir_handle_new_all (info, options);
	} else {
		folder = vfolder_info_get_folder (info, vuri.path);
		if (!folder) {
			VFOLDER_INFO_READ_UNLOCK (info);
			return GNOME_VFS_ERROR_NOT_FOUND;
		}

		dh = dir_handle_new (info, folder, options);
	}

	VFOLDER_INFO_READ_UNLOCK (info);

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
	dir_handle_free (dh);

	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	DirHandle *dh;
	gchar *entry_name;
	GnomeVFSFileInfoOptions options;
	FolderChild child;

	dh = (DirHandle*) method_handle;

 READ_NEXT_ENTRY:

	if (!dh->current)
		return GNOME_VFS_ERROR_EOF;

	options = dh->options;

	entry_name = dh->current->data;
	dh->current = dh->current->next;

	if (!folder_get_child (dh->folder, entry_name, &child))
		goto READ_NEXT_ENTRY;

	if (child.type == DESKTOP_FILE) {
		GnomeVFSURI *file_uri;

		if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
			options &= ~GNOME_VFS_FILE_INFO_GET_MIME_TYPE;

		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_uri = entry_get_real_uri (child.entry);
		gnome_vfs_get_file_info_uri_cancellable (file_uri,
							 file_info,
							 options,
							 context);
		gnome_vfs_uri_unref (file_uri);

		g_free (file_info->name);
		file_info->name = 
			g_strdup (entry_get_displayname (child.entry));

		/* 
		 * we ignore errors from this since the file_info just won't be
		 * filled completely if there's an error, that's all 
		 */

		g_free (file_info->mime_type);
		file_info->mime_type = 
			g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		/* Now we wipe those fields we don't support */
		file_info->valid_fields &= ~(UNSUPPORTED_INFO_FIELDS);
	} 
	else if (child.type == FOLDER) {
		if (folder_is_hidden (child.folder))
			goto READ_NEXT_ENTRY;

		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_info->name = g_strdup (folder_get_name (child.folder));
		GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;

		file_info->mime_type = g_strdup ("x-directory/vfolder-desktop");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		file_info->ctime = dh->info->modification_time;
		file_info->mtime = dh->info->modification_time;
		file_info->valid_fields |= (GNOME_VFS_FILE_INFO_FIELDS_CTIME |
					    GNOME_VFS_FILE_INFO_FIELDS_MTIME);
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
	GnomeVFSResult result = GNOME_VFS_OK;
	VFolderURI vuri;
	VFolderInfo *info;
	Folder *parent;
	FolderChild child;

	VFOLDER_URI_PARSE (uri, &vuri);

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	VFOLDER_INFO_READ_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		VFOLDER_INFO_READ_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (!folder_get_child (parent, vuri.file, &child)) {
		VFOLDER_INFO_READ_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (child.type == DESKTOP_FILE) {
		GnomeVFSURI *file_uri;
		gchar *displayname;

		/* we always get mime-type by forcing it below */
		if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
			options &= ~GNOME_VFS_FILE_INFO_GET_MIME_TYPE;

		file_uri = entry_get_real_uri (child.entry);
		displayname = g_strdup (entry_get_displayname (child.entry));

		VFOLDER_INFO_READ_UNLOCK (info);

		result = gnome_vfs_get_file_info_uri_cancellable (file_uri,
								  file_info,
								  options,
								  context);
		gnome_vfs_uri_unref (file_uri);

		g_free (file_info->name);
		file_info->name = displayname;

		g_free (file_info->mime_type);
		file_info->mime_type = 
			g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		/* Now we wipe those fields we don't support */
		file_info->valid_fields &= ~(UNSUPPORTED_INFO_FIELDS);

		return result;
	}
	else if (child.type == FOLDER) {
		file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

		file_info->name = g_strdup (folder_get_name (child.folder));

		if (child.folder->read_only) {
			file_info->permissions = (GNOME_VFS_PERM_USER_READ |
						  GNOME_VFS_PERM_GROUP_READ |
						  GNOME_VFS_PERM_OTHER_READ);
			file_info->valid_fields |= 
				GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS;
		}

		VFOLDER_INFO_READ_UNLOCK (info);

		GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);

		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;

		file_info->mime_type = g_strdup ("x-directory/vfolder-desktop");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		file_info->ctime = info->modification_time;
		file_info->mtime = info->modification_time;
		file_info->valid_fields |= (GNOME_VFS_FILE_INFO_FIELDS_CTIME |
					    GNOME_VFS_FILE_INFO_FIELDS_MTIME);

		return GNOME_VFS_OK;
	}

	VFOLDER_INFO_READ_UNLOCK (info);
	return GNOME_VFS_ERROR_NOT_FOUND;
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

	if (method_handle == (GnomeVFSMethodHandle *) method) {
		g_free (file_info->mime_type);
		file_info->mime_type = g_strdup ("text/plain");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
		return GNOME_VFS_OK;
	}

	/* we always get mime-type by forcing it below */
	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE)
		options &= ~GNOME_VFS_FILE_INFO_GET_MIME_TYPE;

	result = 
		gnome_vfs_get_file_info_from_handle_cancellable (handle->handle,
								 file_info,
								 options,
								 context);

	g_free (file_info->name);
	file_info->name = g_strdup (entry_get_displayname (handle->entry));

	/* any file is of the .desktop type */
	g_free (file_info->mime_type);
	file_info->mime_type = g_strdup ("application/x-gnome-app-info");
	file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

	/* Now we wipe those fields we don't support */
	file_info->valid_fields &= ~(UNSUPPORTED_INFO_FIELDS);

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
	VFolderInfo *info;
	Folder *parent, *folder;
	VFolderURI vuri;

	VFOLDER_URI_PARSE (uri, &vuri);

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (info->read_only || vuri.is_all_scheme)
		return GNOME_VFS_ERROR_READ_ONLY;

	VFOLDER_INFO_WRITE_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (folder_get_entry (parent, vuri.file)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_FILE_EXISTS;
	}

	folder = folder_get_subfolder (parent, vuri.file);
	if (folder) {
		folder->dont_show_if_empty = FALSE;

		/* 
		 * HACK: Force .vfolder-info write if the folder is already
		 * user-private.
		 */
		if (folder_is_user_private (parent) && 
		    folder_is_user_private (folder))
			vfolder_info_set_dirty (info);
	} else 
		folder = folder_new (info, vuri.file);

	if (!folder_make_user_private (parent) || 
	    !folder_make_user_private (folder)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	folder_add_subfolder (parent, folder);

	vfolder_info_emit_change (info, 
				  vuri.path, 
				  GNOME_VFS_MONITOR_EVENT_CREATED);	

	VFOLDER_INFO_WRITE_UNLOCK (info);
	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	Folder *parent, *folder;
	VFolderInfo *info;
	VFolderURI vuri;

	VFOLDER_URI_PARSE (uri, &vuri);

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (info->read_only || vuri.is_all_scheme)
		return GNOME_VFS_ERROR_READ_ONLY;

	VFOLDER_INFO_WRITE_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	folder = folder_get_subfolder (parent, vuri.file);
	if (!folder || folder_is_hidden (folder)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (folder_list_subfolders (folder) ||
	    folder_list_entries (folder)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_DIRECTORY_NOT_EMPTY;
	}

	if (!folder_make_user_private (parent)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	folder_ref (folder);
	folder_add_exclude (parent, folder_get_name (folder));
	folder_remove_subfolder (parent, folder);
	folder_unref (folder);

	vfolder_info_emit_change (info, 
				  vuri.path, 
				  GNOME_VFS_MONITOR_EVENT_DELETED);

	VFOLDER_INFO_WRITE_UNLOCK (info);
	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	Folder *parent, *folder;
	Entry *entry;
	VFolderInfo *info;
	VFolderURI vuri;

	VFOLDER_URI_PARSE (uri, &vuri);

	if (!vuri.file)
		return GNOME_VFS_ERROR_INVALID_URI;
	else if (vuri.is_all_scheme)
		return GNOME_VFS_ERROR_READ_ONLY;

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	VFOLDER_INFO_WRITE_LOCK (info);

	parent = vfolder_info_get_parent (info, vuri.path);
	if (!parent) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	folder = folder_get_subfolder (parent, vuri.file);
	if (folder) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_IS_DIRECTORY;
	}

	entry = folder_get_entry (parent, vuri.file);
	if (!entry) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (entry_is_user_private (entry)) {
		GnomeVFSURI *uri;
		GnomeVFSResult result;
		
		/* Delete our local copy */
		uri = entry_get_real_uri (entry);
		result = gnome_vfs_unlink_from_uri_cancellable (uri, context);
		gnome_vfs_uri_unref (uri);

		if (result != GNOME_VFS_OK)
			return result;
	}

	if (!folder_make_user_private (parent)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	entry_ref (entry);
	folder_add_exclude (parent, entry_get_displayname (entry));
	folder_remove_entry (parent, entry);
	entry_unref (entry);

	vfolder_info_emit_change (info, 
				  vuri.path, 
				  GNOME_VFS_MONITOR_EVENT_DELETED);

	VFOLDER_INFO_WRITE_UNLOCK (info);
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
	Folder *old_parent, *new_parent;
	//Folder *old_folder, *new_folder;
	//Entry *old_entry, *new_entry;
	//gboolean old_is_directory_file, new_is_directory_file;
	VFolderURI old_vuri, new_vuri;
	FolderChild old_child, existing_child;

	VFOLDER_URI_PARSE (old_uri, &old_vuri);
	VFOLDER_URI_PARSE (new_uri, &new_vuri);

	if (!old_vuri.file)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (old_vuri.is_all_scheme || new_vuri.is_all_scheme)
		return GNOME_VFS_ERROR_READ_ONLY;

	if (strcmp (old_vuri.scheme, new_vuri.scheme) != 0)
		return GNOME_VFS_ERROR_NOT_SAME_FILE_SYSTEM;

	info = vfolder_info_locate (old_vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;
	
	if (info->read_only)
		return GNOME_VFS_ERROR_READ_ONLY;

	VFOLDER_INFO_WRITE_LOCK (info);

	old_parent = vfolder_info_get_parent (info, old_vuri.path);
	if (!old_parent || 
	    !folder_get_child (old_parent, old_vuri.file, &old_child)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	if (!folder_make_user_private (old_parent)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	new_parent = vfolder_info_get_parent (info, new_vuri.path);
	if (!new_parent) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_NOT_A_DIRECTORY;
	}
	
	if (!folder_make_user_private (new_parent)) {
		VFOLDER_INFO_WRITE_UNLOCK (info);
		return GNOME_VFS_ERROR_READ_ONLY;
	}

	if (folder_get_child (new_parent, new_vuri.file, &existing_child)) {
		if (!force_replace) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_FILE_EXISTS;
		}
	}

	if (old_child.type == DESKTOP_FILE) {
		if (existing_child.type == FOLDER) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_IS_DIRECTORY;
		}
		else if (existing_child.type == DESKTOP_FILE) {
			/* ref in case old_child is existing_child */
			entry_ref (old_child.entry);

			result = do_unlink (method,
					    new_uri,
					    context);
			if (result != GNOME_VFS_OK &&
			    result != GNOME_VFS_ERROR_NOT_FOUND) {
				VFOLDER_INFO_WRITE_UNLOCK (info);
				return result;
			}

			entry_set_displayname (old_child.entry, new_vuri.file);
			entry_make_user_private (old_child.entry, new_parent);

			folder_add_entry (new_parent, old_child.entry);

			entry_unref (old_child.entry);

			vfolder_info_emit_change (
				info, 
				new_vuri.path, 
				GNOME_VFS_MONITOR_EVENT_CREATED);
		}
	} 
	else if (old_child.type == FOLDER) {
		if (existing_child.type != FOLDER) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_NOT_A_DIRECTORY;
		}

		if (!strncmp (old_vuri.path, 
			      new_vuri.path, 
			      strlen (old_vuri.path))) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_LOOP;
		}

		/* ref in case old_child is existing_child */
		folder_ref (old_child.folder);

		result = do_remove_directory (method, 
					      new_uri,
					      context);
		if (result != GNOME_VFS_OK &&
		    result != GNOME_VFS_ERROR_NOT_FOUND) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return result;
		}

		folder_remove_subfolder (old_parent, old_child.folder);
		folder_set_name (old_child.folder, new_vuri.file);
		folder_make_user_private (old_child.folder);
		folder_add_subfolder (new_parent, old_child.folder);

		folder_unref (old_child.folder);
	}

	VFOLDER_INFO_WRITE_UNLOCK (info);
	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *source_uri,
		  GnomeVFSURI *target_uri,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{
	VFolderURI source_vuri, target_vuri;

	*same_fs_return = FALSE;

	VFOLDER_URI_PARSE (source_uri, &source_vuri);
	VFOLDER_URI_PARSE (target_uri, &target_vuri);

	if (strcmp (source_vuri.scheme, target_vuri.scheme) != 0 ||
	    source_vuri.is_all_scheme != target_vuri.is_all_scheme)
		*same_fs_return = FALSE;
	else
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
	VFolderURI vuri;

	VFOLDER_URI_PARSE (uri, &vuri);

	if (!vuri.file)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (mask & GNOME_VFS_SET_FILE_INFO_NAME) {
		GnomeVFSResult result = GNOME_VFS_OK;
		char *relative_name;
		GnomeVFSURI *new_uri;

		relative_name = g_strconcat ("../", info->name, NULL);
		new_uri = gnome_vfs_uri_resolve_relative (uri, relative_name);
		g_free (relative_name);

		if (!new_uri)
			return GNOME_VFS_ERROR_INVALID_URI;

		result = do_move (method,
				  uri,
				  new_uri,
				  FALSE /* force_replace */,
				  context);

		gnome_vfs_uri_unref (new_uri);	
		return result;
	} else {
		/* 
		 * We don't support setting any of this other permission,
		 * times and all that voodoo 
		 */
		return GNOME_VFS_ERROR_NOT_SUPPORTED;
	}
}


static GnomeVFSResult
do_monitor_add (GnomeVFSMethod *method,
		GnomeVFSMethodHandle **method_handle_return,
		GnomeVFSURI *uri,
		GnomeVFSMonitorType monitor_type)
{
	VFolderInfo *info;
	VFolderURI vuri;
	Folder *folder;
	Entry *entry;
	//GnomeVFSResult result;
	//GnomeVFSURI *file_uri;
	//FileMonitorHandle *handle;
	//gboolean is_directory_file;

	VFOLDER_URI_PARSE (uri, &vuri);

	info = vfolder_info_locate (vuri.scheme);
	if (!info)
		return GNOME_VFS_ERROR_INVALID_URI;

	VFOLDER_INFO_WRITE_LOCK (info);

	if (monitor_type == GNOME_VFS_MONITOR_DIRECTORY) {
		folder = vfolder_info_get_folder (info, vuri.path);
		if (!folder) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_NOT_FOUND;
		}
	} else {
		/* These can't be very nice FILE names */
		if (!vuri.file || vuri.ends_in_slash) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_INVALID_URI;
		}

		entry = vfolder_info_get_entry (info, vuri.path);
		if (!entry) {
			VFOLDER_INFO_WRITE_UNLOCK (info);
			return GNOME_VFS_ERROR_NOT_FOUND;
		}
	}

	vfolder_info_add_monitor (info, vuri.path, method_handle_return);

	VFOLDER_INFO_WRITE_UNLOCK (info);
	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_monitor_cancel (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle)
{
	if (method_handle == NULL)
		return GNOME_VFS_OK;

	vfolder_info_cancel_monitor (method_handle);

	return GNOME_VFS_OK;
}


/*
 * GnomeVFS Registration
 */
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
	NULL /* create_symbolic_link */,
	do_monitor_add,
	do_monitor_cancel
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
	vfolder_info_destroy_all ();
}
