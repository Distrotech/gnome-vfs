/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* network-method.c - The

   Copyright (C) 2003 Red Hat, Inc

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

   Authors:
         Alexander Larsson <alexl@redhat.com>
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gconf/gconf-client.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-i18n.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-module.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-monitor-private.h>

#define PATH_GCONF_GNOME_VFS_SMB "/system/smb"
#define PATH_GCONF_GNOME_VFS_SMB_WORKGROUP "/system/smb/workgroup"


typedef struct {
	char *file_name; /* not escaped */
        char *contents;
} NetworkFile;

/* gconf settings */
static char *current_workgroup;

static gboolean have_smb;

static time_t smb_timestamp = 0;
static GList *smb_workgroup = NULL;
static NetworkFile *smb_network = NULL;

G_LOCK_DEFINE_STATIC (network);

static NetworkFile *
network_file_new (char *name, char *contents)
{
	NetworkFile *file;

	file = g_new0 (NetworkFile, 1);
	file->file_name = g_strdup (name);
	file->contents = g_strdup (contents);
	
	return file;
}

static void
network_file_free (NetworkFile *file)
{
	g_free (file->file_name);
	g_free (file->contents);
	g_free (file);
}

#if 0
static GnomeVFSURI *
network_file_get_uri (NetworkFile *file) {
	GnomeVFSURI *uri;
	GnomeVFSURI *tmp;

	uri = gnome_vfs_uri_new ("network:///");
	if (file != NULL) {
		tmp = uri;
		uri = gnome_vfs_uri_append_file_name (uri, file->file_name);
		gnome_vfs_uri_unref (tmp);
	}
	return uri;
}
#endif

static NetworkFile *
get_file_from_list (char *name, GList *list)
{
	GList *l;
	NetworkFile *file;

	for (l = list; l != NULL; l = l->next) {
		file = l->data;
		if (strcmp (file->file_name, name) == 0) {
			return file;
		}
	}
	return NULL;
}


static NetworkFile *
get_file (char *name)
{
	NetworkFile *file;

	if (have_smb) {
		if (strcmp (smb_network->file_name, name) == 0) {
			return smb_network;
		}
		
		file = get_file_from_list (name, smb_workgroup);
		if (file != NULL) {
			return file;
		}
	}

	return NULL;
}

typedef struct {
	char *data;
	int len;
	int pos;
} FileHandle;

static FileHandle *
file_handle_new (char *data)
{
	FileHandle *result;
	result = g_new (FileHandle, 1);

	result->data = g_strdup (data);
	result->len = strlen (data);
	result->pos = 0;

	return result;
}

static void
file_handle_destroy (FileHandle *handle)
{
	g_free (handle->data);
	g_free (handle);
}

static char *
get_data_for_link (const char *uri, 
		   const char *display_name, 
		   const char *icon)
{
	char *data;

	data = g_strdup_printf ("[Desktop Entry]\n"
				"Encoding=UTF-8\n"
				"Name=%s\n"
				"Type=FSDevice\n"
				"Icon=%s\n"
				"URL=%s\n",
				display_name,
				icon,
				uri);
	return data;
}

#define READ_CHUNK_SIZE 8192

static GnomeVFSResult
read_entire_file (const char *uri,
		  int *file_size,
		  char **file_contents)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	char *buffer;
	GnomeVFSFileSize total_bytes_read;
	GnomeVFSFileSize bytes_read;

	*file_size = 0;
	*file_contents = NULL;

	/* Open the file. */
	result = gnome_vfs_open (&handle, uri, GNOME_VFS_OPEN_READ);
	if (result != GNOME_VFS_OK) {
		return result;
	}

	/* Read the whole thing. */
	buffer = NULL;
	total_bytes_read = 0;
	do {
		buffer = g_realloc (buffer, total_bytes_read + READ_CHUNK_SIZE);
		result = gnome_vfs_read (handle,
					 buffer + total_bytes_read,
					 READ_CHUNK_SIZE,
					 &bytes_read);
		if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_EOF) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return result;
		}

		/* Check for overflow. */
		if (total_bytes_read + bytes_read < total_bytes_read) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return GNOME_VFS_ERROR_TOO_BIG;
		}

		total_bytes_read += bytes_read;
	} while (result == GNOME_VFS_OK);

	/* Close the file. */
	result = gnome_vfs_close (handle);
	if (result != GNOME_VFS_OK) {
		g_free (buffer);
		return result;
	}

	/* Return the file. */
	*file_size = total_bytes_read;
	*file_contents = g_realloc (buffer, total_bytes_read);
	return GNOME_VFS_OK;
}


static void
update_smb_data (void)
{
	time_t now;
	GnomeVFSDirectoryHandle *dirhandle;
	NetworkFile *file;
	char *workgroup_escaped, *file_escaped, *filename;
	char *uri, *file_uri;
	int file_size;
	char *data;
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;

	if (!have_smb) {
		return;
	}

	if (smb_network == NULL) {
		data = get_data_for_link ("smb://",
					  _("Windows Network"),
					  "gnome-fs-network");
		smb_network = network_file_new ("windows network", data);
		g_free (data);
	}

	if (smb_timestamp != 0) {
		now = time (NULL);
		if (now >= smb_timestamp &&
		    now - smb_timestamp <= 10) {
			return;
		}
		smb_timestamp = now;
	} else {
		smb_timestamp = time (NULL);
	}

	g_list_foreach (smb_workgroup, (GFunc) network_file_free, NULL);
	g_list_free (smb_workgroup);
	smb_workgroup = NULL;
	
	workgroup_escaped = gnome_vfs_escape_string (current_workgroup);
	uri = g_strdup_printf ("smb://%s", workgroup_escaped);

	if (gnome_vfs_directory_open (&dirhandle,
				      uri, GNOME_VFS_FILE_INFO_DEFAULT) == GNOME_VFS_OK) {
		info = gnome_vfs_file_info_new ();
	
		result = gnome_vfs_directory_read_next (dirhandle, info);
		while (result == GNOME_VFS_OK) {
			file_escaped = gnome_vfs_escape_string (info->name);
			file_uri = g_strdup_printf ("smb://%s/%s", workgroup_escaped, file_escaped);
			g_free (file_escaped);
			
			if (read_entire_file (file_uri, &file_size,
					      &data) == GNOME_VFS_OK) {
				filename = g_strconcat ("smb-", info->name, NULL);
				file = network_file_new (filename, data);
				g_free (filename);
				g_free (data);

				smb_workgroup = g_list_prepend (smb_workgroup, file);
			}
			g_free (file_uri);

			gnome_vfs_file_info_clear (info);
			result = gnome_vfs_directory_read_next (dirhandle, info);
		} 

		gnome_vfs_directory_close (dirhandle);
	}

	g_free (uri);
	g_free (workgroup_escaped);


}

static void
update_root (void)
{
	update_smb_data ();
}

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	NetworkFile *file;
	char *name;
	
	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if (mode & GNOME_VFS_OPEN_WRITE) {
		return GNOME_VFS_ERROR_NOT_PERMITTED;
	}

	if (strcmp (uri->text, "/") == 0) {
		return GNOME_VFS_ERROR_NOT_PERMITTED;
	}

	G_LOCK (network);

	update_root ();
	
	name = gnome_vfs_uri_extract_short_name (uri);
	file = get_file (name);
	g_free (name);
	
	if (file == NULL) {
		G_UNLOCK (network);
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	file_handle = file_handle_new (file->contents);

	G_UNLOCK (network);
		
	*method_handle = (GnomeVFSMethodHandle *) file_handle;

	return GNOME_VFS_OK;
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
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	FileHandle *file_handle;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	file_handle_destroy (file_handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	int read_len;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;
	*bytes_read = 0;
	
	if (file_handle->pos >= file_handle->len) {
		return GNOME_VFS_ERROR_EOF;
	}

	read_len = MIN (num_bytes, file_handle->len - file_handle->pos);

	memcpy (buffer, file_handle->data + file_handle->pos, read_len);
	*bytes_read = read_len;
	file_handle->pos += read_len;

	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}


static GnomeVFSResult
do_seek (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;

	file_handle = (FileHandle *) method_handle;

	switch (whence) {
	case GNOME_VFS_SEEK_START:
		file_handle->pos = offset;
		break;
	case GNOME_VFS_SEEK_CURRENT:
		file_handle->pos += offset;
		break;
	case GNOME_VFS_SEEK_END:
		file_handle->pos = file_handle->len + offset;
		break;
	}

	if (file_handle->pos < 0) {
		file_handle->pos = 0;
	}
	
	if (file_handle->pos > file_handle->len) {
		file_handle->pos = file_handle->len;
	}
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_tell (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
	FileHandle *file_handle;

	file_handle = (FileHandle *) method_handle;

	*offset_return = file_handle->pos;
	return GNOME_VFS_OK;
}


static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSFileSize where,
		    GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod *method,
	     GnomeVFSURI *uri,
	     GnomeVFSFileSize where,
	     GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

typedef struct {
	GnomeVFSFileInfoOptions options;
	GList *filenames;
} DirectoryHandle;

static DirectoryHandle *
directory_handle_new (GnomeVFSFileInfoOptions options)
{
	DirectoryHandle *result;

	result = g_new (DirectoryHandle, 1);
	result->options = options;
	result->filenames = NULL;

	return result;
}

static void
directory_handle_destroy (DirectoryHandle *dir_handle)
{
	g_list_foreach (dir_handle->filenames, (GFunc)g_free, NULL);
	g_list_free (dir_handle->filenames);
	g_free (dir_handle);
}

static void
directory_handle_add_filename (DirectoryHandle *dir_handle, NetworkFile *file)
{
	if (file != NULL) {
		dir_handle->filenames = g_list_prepend (dir_handle->filenames, g_strdup (file->file_name));
	}
} 

static void
directory_handle_add_filenames (DirectoryHandle *dir_handle, GList *files)
{
	while (files != NULL) {
		directory_handle_add_filename (dir_handle, files->data);
		files = files->next;
	}
} 

static GnomeVFSResult
do_open_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions options,
		   GnomeVFSContext *context)
{
	DirectoryHandle *dir_handle;

	dir_handle = directory_handle_new (options);

	G_LOCK (network);

	update_root ();

	directory_handle_add_filenames (dir_handle, smb_workgroup);
	directory_handle_add_filename (dir_handle, smb_network);

	G_UNLOCK (network);

	*method_handle = (GnomeVFSMethodHandle *) dir_handle;
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext *context)
{
	DirectoryHandle *dir_handle;

	dir_handle = (DirectoryHandle *) method_handle;

	directory_handle_destroy (dir_handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	DirectoryHandle *handle;
	GList *entry;

	handle = (DirectoryHandle *) method_handle;

	if (handle->filenames == NULL) {
		return GNOME_VFS_ERROR_EOF;
	}

	entry = handle->filenames;
	handle->filenames = g_list_remove_link (handle->filenames, entry);
	
	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;
	file_info->name = g_strdup (entry->data);
	g_free (entry->data);
	g_list_free_1 (entry);

	file_info->mime_type = g_strdup ("application/x-desktop");
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->valid_fields |=
		GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE |
		GNOME_VFS_FILE_INFO_FIELDS_TYPE;

	file_info->permissions =
		GNOME_VFS_PERM_USER_READ |
		GNOME_VFS_PERM_OTHER_READ |
		GNOME_VFS_PERM_GROUP_READ;
	file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS;
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  GnomeVFSContext *context)
{
	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	if (strcmp (uri->text, "/") == 0) {
		file_info->name = g_strdup ("/");
		
		file_info->mime_type = g_strdup ("x-directory/normal");
		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		file_info->valid_fields |=
			GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE |
			GNOME_VFS_FILE_INFO_FIELDS_TYPE;
	} else {
		file_info->name = gnome_vfs_uri_extract_short_name (uri);
		
		file_info->mime_type = g_strdup ("application/x-desktop");
		file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
		file_info->valid_fields |=
			GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE |
			GNOME_VFS_FILE_INFO_FIELDS_TYPE;
	}
	file_info->permissions =
		GNOME_VFS_PERM_USER_READ |
		GNOME_VFS_PERM_OTHER_READ |
		GNOME_VFS_PERM_GROUP_READ;
	file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS;
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      GnomeVFSContext *context)
{
	FileHandle *file_handle;

	file_handle = (FileHandle *) method_handle;

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;
	
	file_info->mime_type = g_strdup ("application/x-desktop");
	file_info->size = file_handle->len;
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->valid_fields |=
		GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE |
		GNOME_VFS_FILE_INFO_FIELDS_SIZE |
		GNOME_VFS_FILE_INFO_FIELDS_TYPE;

	return GNOME_VFS_OK;
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
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
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
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod *method,
			 GnomeVFSURI *uri,
			 const char *target_reference,
			 GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}

/* When checking whether two locations are on the same file system, we are
   doing this to determine whether we can recursively move or do other
   sorts of transfers.  When a symbolic link is the "source", its
   location is the location of the link file, because we want to
   know about transferring the link, whereas for symbolic links that
   are "targets", we use the location of the object being pointed to,
   because that is where we will be moving/copying to. */
static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
		  GnomeVFSURI *source_uri,
		  GnomeVFSURI *target_uri,
		  gboolean *same_fs_return,
		  GnomeVFSContext *context)
{
	return TRUE;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_PERMITTED;
}


static GnomeVFSResult
do_file_control (GnomeVFSMethod *method,
		 GnomeVFSMethodHandle *method_handle,
		 const char *operation,
		 gpointer operation_data,
		 GnomeVFSContext *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

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
	do_create_symbolic_link,
	NULL, /* do_monitor_add */
	NULL, /* do_monitor_cancel */
	do_file_control
};

static void
notify_gconf_value_changed (GConfClient *client,
			    guint        cnxn_id,
			    GConfEntry  *entry,
			    gpointer     data)
{
	G_LOCK (network);

	g_free (current_workgroup);

	current_workgroup = gconf_client_get_string (client, PATH_GCONF_GNOME_VFS_SMB_WORKGROUP, NULL);
	if (current_workgroup == NULL) {
		current_workgroup = g_strdup ("workgroup");
	}

	smb_timestamp = 0;

	G_UNLOCK (network);
}


GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
	GnomeVFSURI *uri;
	GConfClient *gconf_client;

	gconf_client = gconf_client_get_default ();

	gconf_client_add_dir (gconf_client, PATH_GCONF_GNOME_VFS_SMB, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	current_workgroup = gconf_client_get_string (gconf_client, PATH_GCONF_GNOME_VFS_SMB_WORKGROUP, NULL);
	if (current_workgroup == NULL) {
		current_workgroup = g_strdup ("workgroup");
	}
	
	gconf_client_notify_add (gconf_client, PATH_GCONF_GNOME_VFS_SMB_WORKGROUP, notify_gconf_value_changed, NULL, NULL, NULL);

	g_object_unref (gconf_client);

	uri = gnome_vfs_uri_new ("smb://");
	have_smb = uri != NULL;
	if (uri != NULL) {
		gnome_vfs_uri_unref (uri);
	}

	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
