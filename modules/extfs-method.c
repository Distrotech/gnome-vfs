/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* extfs-method.c - Integrated support for various archiving methods via
   helper scripts.

   Copyright (C) 1999 Free Software Foundation

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

   Author: Ettore Perazzoli <ettore@comm2000.it>
   Based on the ideas from the extfs system implemented in the GNU Midnight
   Commander.  */

/* TODO: Metadata? */
/* TODO: Support archives on non-local file systems.  Although I am not
   that sure it's such a terrific idea anymore.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <libgnome/libgnome.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "gnome-vfs-module.h"
#include "module-shared.h"

#include "extfs-method.h"


#define EXTFS_COMMAND_DIR	PREFIX "/lib/vfs/extfs"


/* Our private handle struct.  */
struct _ExtfsHandle {
	GnomeVFSOpenMode open_mode;
	GnomeVFSHandle *vfs_handle;
	gchar *local_path;
};
typedef struct _ExtfsHandle ExtfsHandle;

#define VFS_HANDLE(method_handle) \
	((ExtfsHandle *) method_handle)->vfs_handle

/* List of current handles, for cleaning up in `vfs_module_shutdown()'.  */
static GList *handle_list;
G_LOCK_DEFINE_STATIC (handle_list);


struct _ExtfsDirectoryEntry {
	gchar *directory;
	GnomeVFSFileInfo *info;
};
typedef struct _ExtfsDirectoryEntry ExtfsDirectoryEntry;

struct _ExtfsDirectory {
	guint ref_count;
	GnomeVFSURI *uri;
	GList *entries;
};
typedef struct _ExtfsDirectory ExtfsDirectory;

/* Hash of directory lists.  */
/* Notice that, for the way the code currently works, this is useless.  But I
   plan to add some caching (i.e. keep directory lists for a while to make
   visiting easier) in the future, so this will help.  The main reason for not
   doing so now right is that we need some support for expiration in the GNOME
   VFS library core.  */
static GHashTable *uri_to_directory_hash;
G_LOCK_DEFINE_STATIC (uri_to_directory_hash);

/* Directory handle struct.  */
struct _ExtfsDirectoryHandle {
	ExtfsDirectory *directory;
	GList *prev_position;
	gchar *sub_uri;
	gchar **meta_keys;
	GnomeVFSFileInfoOptions info_options;
	const GnomeVFSDirectoryFilter *filter;
};
typedef struct _ExtfsDirectoryHandle ExtfsDirectoryHandle;


#define ERROR_IF_NOT_LOCAL(uri)					\
	if (strcmp ((uri)->parent->method_string, "file") != 0)	\
		return GNOME_VFS_ERROR_NOTSUPPORTED;

static GnomeVFSResult
extfs_handle_close (ExtfsHandle *handle)
{
	GnomeVFSResult close_result;

	close_result = gnome_vfs_close (handle->vfs_handle);

	/* Maybe we could use the VFS functions here.  */
	if (unlink (handle->local_path) != 0)
		g_warning ("Cannot unlink temporary file `%s': %s",
			   handle->local_path, g_strerror (errno));

	g_free (handle->local_path);
	g_free (handle);

	return close_result;
}

static gchar *
quote_file_name (const gchar *file_name)
{
	guint len;
	const gchar *p;
	gchar *q;
	gchar *new;

	len = 2;
	for (p = file_name; *p != 0; p++) {
		if (*p == '\'')
			len += 3;
		else
			len++;
	}

	new = g_malloc (len + 1);
	new[0] = '\'';

	for (p = file_name, q = new + 1; *p != 0; p++) {
		if (*p == '\'') {
			q[0] = '"';
			q[1] = '\'';
			q[2] = '"';
			q += 3;
		} else {
			*q = *p;
			q++;
		}
	}

	*q++ = '\'';
	*q = 0;

	return new;
}

static gchar *
get_script_path (const GnomeVFSURI *uri)
{
	return g_strconcat (EXTFS_COMMAND_DIR, "/", uri->method_string, NULL);
}

static gchar *
get_dirname (const gchar *path)
{
	gchar *p;
	gchar *s;
	guint len;

	p = strrchr (path, G_DIR_SEPARATOR);
	if (p == NULL)
		return NULL;

	while (p != path && *p == G_DIR_SEPARATOR)
		p--;

	while (p != path && *path == G_DIR_SEPARATOR)
		path++;

	if (p == path)
		return NULL;

	len = p - path;
	s = g_malloc (len + 1);
	memcpy (s, p, len);
	s[len] = 0;

	return s;
}


/* URI -> directory hash table handling.  */

static void
free_directory_entries (GList *entries)
{
	GList *p;

	for (p = entries; p != NULL; p = p->next) {
		ExtfsDirectoryEntry *entry;

		entry = p->data;
		gnome_vfs_file_info_destroy (entry->info);
		g_free (entry->directory);
		g_free (entry);
	}

	g_list_free (entries);
}

static ExtfsDirectory *
extfs_directory_new (const GnomeVFSURI *uri,
		     GList *entries)
{
	ExtfsDirectory *new;
	ExtfsDirectory *existing;

	G_LOCK (uri_to_directory_hash);

	/* First check that a new directory has not been registered for this
           URI yet.  */
	existing = g_hash_table_lookup (uri_to_directory_hash, uri);
	if (existing != NULL) {
		free_directory_entries (entries);
		G_UNLOCK (uri_to_directory_hash);
		return existing;
	}

	new = g_new (ExtfsDirectory, 1);

	new->uri = gnome_vfs_uri_dup (uri);
	new->entries = entries;
	new->ref_count = 1;

	g_hash_table_insert (uri_to_directory_hash, new->uri, new);

	G_UNLOCK (uri_to_directory_hash);

	return new;
}

static void
extfs_directory_unref (ExtfsDirectory *dir)
{
	g_return_if_fail (dir->ref_count > 0);

	G_LOCK (uri_to_directory_hash);

	dir->ref_count--;
	if (dir->ref_count == 0) {
		g_hash_table_remove (uri_to_directory_hash, dir->uri);

		free_directory_entries (dir->entries);
		gnome_vfs_uri_unref (dir->uri);
		g_free (dir);
	}

	G_UNLOCK (uri_to_directory_hash);
}

static ExtfsDirectory *
extfs_directory_lookup (GnomeVFSURI *uri)
{
	ExtfsDirectory *directory;

	G_LOCK (uri_to_directory_hash);
	directory = g_hash_table_lookup (uri_to_directory_hash, uri);
	if (directory != NULL)
		directory->ref_count++;
	G_UNLOCK (uri_to_directory_hash);

	return directory;
}


static GnomeVFSResult
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSCancellation *cancellation)
{
	GnomeVFSResult result;
	GnomeVFSProcessResult process_result;
	GnomeVFSHandle *temp_handle;
	ExtfsHandle *handle;
	gchar *script_path;
	const gchar *stored_name;
	gchar *args[6];
	gchar *temp_name;
	gboolean cleanup;
	gint process_exit_value;

	ERROR_IF_NOT_LOCAL (uri);

	/* TODO: Support write mode.  */
	if (mode & GNOME_VFS_OPEN_WRITE)
		return GNOME_VFS_ERROR_READONLYFS;

	if (uri->text == NULL)
		return GNOME_VFS_ERROR_INVALIDURI;

	if (uri->method_string == NULL)
		return GNOME_VFS_ERROR_INTERNAL;

	stored_name = uri->text;
	while (*stored_name == G_DIR_SEPARATOR)
		stored_name++;

	if (*stored_name == '\0')
		return GNOME_VFS_ERROR_INVALIDURI;

	result = gnome_vfs_create_temp ("extfs", &temp_name, &temp_handle);
	if (result != GNOME_VFS_OK)
		return result;

	handle = g_new (ExtfsHandle, 1);
	handle->vfs_handle = temp_handle;
	handle->open_mode = mode;
	handle->local_path = temp_name;

	script_path = get_script_path (uri);

	args[0] = uri->method_string;
	args[1] = "copyout";
	args[2] = uri->parent->text;
	args[3] = (gchar *) stored_name;
	args[4] = temp_name;
	args[5] = NULL;
	
	/* FIXME args */
	process_result = gnome_vfs_process_run_cancellable
		(script_path, args, GNOME_VFS_PROCESS_CLOSEFDS, cancellation,
		 &process_exit_value);

	switch (process_result) {
	case GNOME_VFS_PROCESS_RUN_OK:
		if (process_exit_value == 0) {
			result = GNOME_VFS_OK;
			cleanup = FALSE;
		} else {
			/* This is not very accurate, but it should be
			   enough.  */
			result = GNOME_VFS_ERROR_NOTFOUND;
			cleanup = TRUE;
		}
		break;
	case GNOME_VFS_PROCESS_RUN_CANCELLED:
		result = GNOME_VFS_ERROR_CANCELLED;
		cleanup = TRUE;
		break;
	case GNOME_VFS_PROCESS_RUN_SIGNALED:
		result = GNOME_VFS_ERROR_INTERRUPTED;
		cleanup = TRUE;
		break;
	case GNOME_VFS_PROCESS_RUN_STOPPED:
		result = GNOME_VFS_ERROR_INTERRUPTED;
		cleanup = TRUE;
		break;
	case GNOME_VFS_PROCESS_RUN_ERROR:
	default:
		/* If we get `GNOME_VFS_PROCESS_RUN_ERROR', it means that we
		   could not run the executable for some weird reason.*/
		result = GNOME_VFS_ERROR_INTERNAL;
		cleanup = TRUE;
		break;
	}

	if (cleanup) {
		extfs_handle_close (handle);
	} else {
		*method_handle = (GnomeVFSMethodHandle *) handle;
		G_LOCK (handle_list);
		handle_list = g_list_prepend (handle_list, handle);
		G_UNLOCK (handle_list);
	}

	g_free (script_path);
	return result;
}

static GnomeVFSResult
do_create (GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm,
	   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_READONLYFS;
}

static GnomeVFSResult
do_close (GnomeVFSMethodHandle *method_handle,
	  GnomeVFSCancellation *cancellation)
{
	ExtfsHandle *extfs_handle;
	GnomeVFSResult result;

	extfs_handle = (ExtfsHandle *) method_handle;
	result = extfs_handle_close (extfs_handle);

	if (result == GNOME_VFS_OK) {
		G_LOCK (handle_list);
		handle_list = g_list_remove (handle_list, extfs_handle);
		G_UNLOCK (handle_list);
	}

	return result;
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSCancellation *cancellation)
{
	return gnome_vfs_read_cancellable (VFS_HANDLE (method_handle),
					   buffer, num_bytes, bytes_read,
					   cancellation);
}

static GnomeVFSResult
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_READONLYFS;
}

static GnomeVFSResult
do_seek (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSSeekPosition whence,
	 GnomeVFSFileOffset offset,
	 GnomeVFSCancellation *cancellation)
{
	return gnome_vfs_seek_cancellable (VFS_HANDLE (method_handle),
					   whence, offset, cancellation);
}

static GnomeVFSResult
do_tell (GnomeVFSMethodHandle *method_handle,
	 GnomeVFSFileOffset *offset_return)
{
	return gnome_vfs_tell (VFS_HANDLE (method_handle), offset_return);
}

static GnomeVFSResult
do_truncate (GnomeVFSMethodHandle *method_handle,
	     GnomeVFSFileSize where,
	     GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}


/* Directory reading.  */
static GnomeVFSResult
read_directory_list (FILE *p,
		     GList **list_return,
		     GnomeVFSFileInfoOptions info_options,
		     GnomeVFSCancellation *cancellation)
{
	GnomeVFSResult result;
	GList *list;
	gchar *line_buffer;
	size_t line_buffer_size = 0;

	list = NULL;
	line_buffer = NULL;
	line_buffer_size = 0;
	result = GNOME_VFS_OK;

	while (1) {
		GnomeVFSFileInfo *info;
		ExtfsDirectoryEntry *entry;
		ssize_t chars_read;
		struct stat statbuf;
		gchar *name;
		gchar *symlink_name;

		if (gnome_vfs_cancellation_check (cancellation)) {
			result = GNOME_VFS_ERROR_CANCELLED;
			break;
		}

		chars_read = getdelim (&line_buffer, &line_buffer_size, '\n', p);
		if (chars_read == -1)
			break;

		/* FIXME */
		fputs (line_buffer, stdout);

		line_buffer[chars_read] = '\0';

		if (! gnome_vfs_parse_ls_lga (line_buffer, &statbuf,
					      &name, &symlink_name))
			continue;

		info = gnome_vfs_file_info_new ();
		gnome_vfs_stat_to_file_info (info, &statbuf);

		info->name = g_strdup (g_basename (name));
		info->symlink_name = symlink_name;

		/* Notice that we always do stupid, fast MIME type checking.
                   Real checking based on contents would be too expensive.  */
		if (info_options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
			info->mime_type = g_strdup (gnome_mime_type
						    (info->name));

		entry = g_new (ExtfsDirectoryEntry, 1);
		entry->info = info;
		entry->directory = get_dirname (name);

		g_free (name);

		/* Order does not really matter here.  */
		list = g_list_prepend (list, entry);
	}

	*list_return = list;
	return result;
}

static GnomeVFSResult
do_open_directory (GnomeVFSMethodHandle **method_handle,
		   GnomeVFSURI *uri,
		   GnomeVFSFileInfoOptions info_options,
		   const GList *meta_keys,
		   const GnomeVFSDirectoryFilter *filter,
		   GnomeVFSCancellation *cancellation)
{
	ExtfsDirectoryHandle *handle;
	ExtfsDirectory *directory;
	struct stat statbuf;
	gchar *script_path;
	gchar *quoted_file_name;
	gchar *cmd;
	const gchar *p;
	FILE *pipe;

	ERROR_IF_NOT_LOCAL (uri);

	directory = extfs_directory_lookup (uri->parent);
	if (directory == NULL) {
		GList *list;
		GnomeVFSResult result;

		/* Check that the file exists first.  */
		if (stat (uri->parent->text, &statbuf) != 0)
			return GNOME_VFS_ERROR_NOTFOUND;

		quoted_file_name = quote_file_name (uri->parent->text);
		script_path = get_script_path (uri);
		cmd = g_strconcat (script_path, " list ", quoted_file_name, 
				   NULL);

		pipe = popen (cmd, "r");

		g_free (cmd);
		g_free (script_path);
		g_free (quoted_file_name);

		if (pipe == NULL)
			return GNOME_VFS_ERROR_NOTSUPPORTED;

		result = read_directory_list (pipe, &list, info_options,
					      cancellation);

		if (pclose (pipe) == 0 && result == GNOME_VFS_OK) {
			directory = extfs_directory_new (uri->parent, list);
		} else {
			free_directory_entries (list);
			if (result == GNOME_VFS_OK)
				return GNOME_VFS_ERROR_IO; /* FIXME? */
			else
				return result;
		}
	}

	handle = g_new (ExtfsDirectoryHandle, 1);
	handle->directory = directory;
	handle->prev_position = NULL;
	handle->meta_keys = meta_keys; /* FIXME currently unused FIXME strdup? */
	handle->info_options = info_options; /* FIXME currently unused */
	handle->filter = filter;

	/* Remove all leading slashes, as they don't matter for us.  */
	if (uri->text != NULL) {
		for (p = uri->text; *p == G_DIR_SEPARATOR; p++)
			;
		handle->sub_uri = g_strdup (p);
	} else {
		handle->sub_uri = NULL;
	}

	*method_handle = (GnomeVFSMethodHandle *) handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethodHandle *method_handle,
		    GnomeVFSCancellation *cancellation)
{
	ExtfsDirectoryHandle *handle;

	handle = (ExtfsDirectoryHandle *) method_handle;

	extfs_directory_unref (handle->directory);
	g_free (handle->sub_uri);
	g_free (handle);

	return GNOME_VFS_OK;
}

static gboolean
match (const ExtfsDirectoryHandle *handle,
       const ExtfsDirectoryEntry *entry)
{
	const gchar *p;
	guint len;

	if ((entry->directory != NULL && handle->sub_uri == NULL)
	    || (entry->directory == NULL && handle->sub_uri != NULL))
		return FALSE;

	/* First check that this is in the subdirectory we want.  */
	/* FIXME more canonicalization might be needed.  */

	if (entry->directory != NULL) {
		for (p = entry->directory; *p == G_DIR_SEPARATOR; p++)
			;
		len = strlen (handle->sub_uri);
		if (p[len] != G_DIR_SEPARATOR && p[len] != '\0')
			return FALSE;
		if (strncmp (p, handle->sub_uri, len) != 0)
			return FALSE;
	}

	if (! gnome_vfs_directory_filter_apply (handle->filter, entry->info))
		return FALSE;

	return TRUE;
}

static ExtfsDirectoryEntry *
find_next (ExtfsDirectoryHandle *handle)
{
	ExtfsDirectory *directory;
	GList *p;

	directory = handle->directory;
	p = handle->prev_position;

	if (p == NULL) {
		ExtfsDirectoryEntry *entry;

		p = directory->entries;
		entry = p->data;
		if (match (handle, entry)) {
			handle->prev_position = p;
			return entry;
		}
	}

	for (p = p->next; p != NULL; p = p->next) {
		ExtfsDirectoryEntry *entry;

		entry = p->data;
		if (match (handle, entry)) {
			handle->prev_position = p;
			return entry;
		}
	}

	return NULL;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo *file_info,
		   GnomeVFSCancellation *cancellation)
{
	ExtfsDirectoryHandle *handle;
	ExtfsDirectoryEntry *next;

	handle = (ExtfsDirectoryHandle *) method_handle;

	next = find_next (handle);
	if (next == NULL)
		return GNOME_VFS_ERROR_EOF;

	gnome_vfs_file_info_copy (file_info, next->info);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static gboolean
do_is_local (const GnomeVFSURI *uri)
{
	return FALSE;
}

static GnomeVFSResult
do_make_directory (GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSURI *uri,
		     GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_move (GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}


static GnomeVFSResult
do_unlink (GnomeVFSURI *uri,
	   GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSURI *a,
		  GnomeVFSURI *b,
		  gboolean *same_fs_return,
		  GnomeVFSCancellation *cancellation)
{
	return GNOME_VFS_ERROR_NOTSUPPORTED;
}


static GnomeVFSMethod method = {
	do_open,
	do_create,
	do_close,
	do_read,
	do_write,
	do_seek,
	do_tell,
	do_truncate,
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
	do_check_same_fs
};

GnomeVFSMethod *
vfs_module_init (void)
{
	handle_list = NULL;
	uri_to_directory_hash = g_hash_table_new (gnome_vfs_uri_hash,
						  gnome_vfs_uri_hequal);

	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
	GList *p;

	for (p = handle_list; p != NULL; p = p->next)
		extfs_handle_close ((ExtfsHandle *) p->data);
}
