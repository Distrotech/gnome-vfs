/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* ssh-method.c - VFS Access to the GConf configuration database.

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

   Author: Ian McKellar <yakk@yakk.net> */

#include <config.h>

#include <errno.h>
#include <glib/gstrfuncs.h>
#include <libgnomevfs/gnome-vfs-cancellation.h>
#include <libgnomevfs/gnome-vfs-context.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-module.h>
#include <libgnomevfs/gnome-vfs-parse-ls.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
	GnomeVFSMethodHandle method_handle;
	GnomeVFSURI *uri;
	enum {
		SSH_FILE,
		SSH_LIST
	} type;
	GnomeVFSOpenMode open_mode;
	int read_fd;
	int write_fd;
	pid_t pid;
} SshHandle;

static GnomeVFSResult do_open           (GnomeVFSMethod *method,
				         GnomeVFSMethodHandle **method_handle,
				         GnomeVFSURI *uri,
				         GnomeVFSOpenMode mode,
				         GnomeVFSContext *context);
static GnomeVFSResult do_create         (GnomeVFSMethod *method,
				         GnomeVFSMethodHandle **method_handle,
				         GnomeVFSURI *uri,
				         GnomeVFSOpenMode mode,
				         gboolean exclusive,
				         guint perm,
				         GnomeVFSContext *context);
static GnomeVFSResult do_close          (GnomeVFSMethod *method,
				         GnomeVFSMethodHandle *method_handle,
				         GnomeVFSContext *context);
static GnomeVFSResult do_read		(GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read,
					 GnomeVFSContext *context);
static GnomeVFSResult do_write          (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
				         gconstpointer buffer,
				         GnomeVFSFileSize num_bytes,
				         GnomeVFSFileSize *bytes_written,
					 GnomeVFSContext *context);
static GnomeVFSResult do_open_directory (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfoOptions options,
					 GnomeVFSContext *context);
static GnomeVFSResult do_close_directory(GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 GnomeVFSContext *context);
static GnomeVFSResult do_read_directory (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSContext *context);
static GnomeVFSResult do_get_file_info  (GnomeVFSMethod *method,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 GnomeVFSContext *context);
static GnomeVFSResult do_make_directory (GnomeVFSMethod *method,
					 GnomeVFSURI *uri,
					 guint perm,
					 GnomeVFSContext *context);
static GnomeVFSResult do_remove_directory(GnomeVFSMethod *method,
					  GnomeVFSURI *uri,
					  GnomeVFSContext *context);
static GnomeVFSResult do_unlink         (GnomeVFSMethod *method,
					 GnomeVFSURI *uri,
					 GnomeVFSContext *context);
static GnomeVFSResult do_set_file_info  (GnomeVFSMethod *method,
					 GnomeVFSURI *uri,
					 const GnomeVFSFileInfo *info,
					 GnomeVFSSetFileInfoMask mask,
					 GnomeVFSContext *context);
#if 0
static GnomeVFSResult do_get_file_info_from_handle
                                        (GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options);
#endif
static gboolean       do_is_local       (GnomeVFSMethod *method,
					 const GnomeVFSURI *uri);

static GnomeVFSMethod method = {
	sizeof (GnomeVFSMethod),
        do_open,
        do_create, /* create */
        do_close,
        do_read, /* read */
        do_write, /* write */
        NULL, /* seek */
        NULL, /* tell */
        NULL, /* truncate */
        do_open_directory,
	do_close_directory,
        do_read_directory,
        do_get_file_info,
	NULL, /* get_file_info_from_handle */
        do_is_local,
	do_make_directory, /* make directory */
        do_remove_directory, /* remove directory */
	NULL, /* move */
	do_unlink, /* unlink */
	NULL, /* check_same_fs */
	do_set_file_info, /* set_file_info */
	NULL, /* truncate */
	NULL, /* find_directory */
	NULL /* create_symbolic_link */
};

/* FIXME: does this like FDs? */
static GnomeVFSResult
ssh_connect (SshHandle **handle_return,
	     GnomeVFSURI *uri, const char *command)
{
	int in[2];
	int out[2];
	pid_t pid;
	SshHandle *handle;

	if ( pipe (in) == -1 || 
	     pipe (out) == -1 ) {
		/* bugger */
		return gnome_vfs_result_from_errno ();
	}

	pid = fork ();

	if (pid == 0) {
		/* child */

		if ( dup2 (in[0], 0) == -1 ||
		     dup2 (out[1], 1) == -1 ) {
			/* bugger */
			_exit (errno); /* can we get the error back to the parent? */
		}

		/* fixme: handle other ports */
		execlp ("ssh", "ssh", "-oBatchmode yes", "-x", "-l", 
				gnome_vfs_uri_get_user_name (uri),
				gnome_vfs_uri_get_host_name (uri),
				command, NULL);

		/* we shouldn't get here */

		_exit (errno); /* can we get the error back to the parent? */
	} else if (pid == -1) {
		/* bugger */
		return gnome_vfs_result_from_errno ();
	} else {
		/* parent */
		/*
		waitpid (pid, &status, 0);
		status = WEXITSTATUS (status);

		if (status != 0) {
			return gnome_vfs_result_from_errno ();
		} */

	}

	handle = g_new0 (SshHandle, 1);
	handle->uri = uri;
	handle->read_fd = out[0];
	handle->write_fd = in[1];
	handle->pid = pid;

	gnome_vfs_uri_ref (handle->uri);

	*handle_return = handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
ssh_destroy (SshHandle *handle)
{
	close (handle->read_fd);
	close (handle->write_fd);
	gnome_vfs_uri_unref (handle->uri);
	kill (handle->pid, SIGINT);
	g_free (handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
ssh_read (SshHandle *handle,
	   gpointer buffer,
	   GnomeVFSFileSize num_bytes,
	   GnomeVFSFileSize *bytes_read)
{
	GnomeVFSFileSize my_read;

	my_read = (GnomeVFSFileSize) read (handle->read_fd, buffer, 
					   (size_t) num_bytes);

	if (my_read == -1) {
		return gnome_vfs_result_from_errno ();
	}

	*bytes_read = my_read;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
ssh_write (SshHandle *handle,
	   gconstpointer buffer,
	   GnomeVFSFileSize num_bytes,
	   GnomeVFSFileSize *bytes_written)
{
	GnomeVFSFileSize written;
	int count=0;

	do {
		errno = 0;
		written = (GnomeVFSFileSize) write (handle->write_fd, buffer, 
						    (size_t) num_bytes);
		if (written == -1 && errno == EINTR) {
			count++;
			usleep (10);
		}
	} while (written == -1 && errno == EINTR && count < 5);

	if (written == -1) {
		return gnome_vfs_result_from_errno ();
	}

	*bytes_written = written;

	return GNOME_VFS_OK;
}

#if 0
static char *ssh_escape (const char *string)
{
	char *new_str;
	int i,j;
	
	new_str = g_malloc0 (strlen (string)*2+3);

	new_str[0]='\'';

	for (i=0,j=1; string[i] != '\0'; i++, j++) {
		if (string[i] == '\'') {
			new_str[j] = '\\';
			j++;
		}
		new_str[j] = string[i];
	}

	return new_str;
}

static GnomeVFSResult
ssh_send (SshHandle *handle,
	  const char *string)
{
	GnomeVFSFileSize len, written;
	GnomeVFSResult result = GNOME_VFS_OK;
	
	len = strlen (string);

	while (len > 0 && result == GNOME_VFS_OK) {
		result = ssh_write (handle, string, len, &written);
		len -= written;
		string += written;
	}

	return result;
}
#endif

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	char *cmd;
	SshHandle *handle = NULL;
	char *name;

	/* FIXME: escape for shell */
	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);

	if (mode == GNOME_VFS_OPEN_READ) {
		/* FIXME: escape for shell */
		cmd = g_strdup_printf ("cat '%s'", name);
		result = ssh_connect (&handle, uri, cmd);
		g_free (cmd);

		if (result != GNOME_VFS_OK) {
			g_free (name);
			return result;
		}

	} else if (mode == GNOME_VFS_OPEN_WRITE) {
		g_free (name);
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	} else {
		g_free (name);
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	}
	
	handle->open_mode = mode;
	handle->type = SSH_FILE;
	*method_handle = (GnomeVFSMethodHandle *)handle;

	g_free (name);

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
	SshHandle *handle = NULL;
	char *cmd;
	GnomeVFSResult result;
	char *name;

	/* FIXME: escape for shell */
	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);

	if (mode != GNOME_VFS_OPEN_WRITE) {
		g_free (name);
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	}


	/* FIXME: escape for shell */
	cmd = g_strdup_printf ("cat > '%s'", name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);

	if (result != GNOME_VFS_OK) {
		g_free (name);
		return result;
	}

	/* FIXME: set perm */

	handle->open_mode = mode;
	handle->type = SSH_FILE;
	*method_handle = (GnomeVFSMethodHandle *)handle;

	g_free (name);
	return result;
}

static GnomeVFSResult   
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	return ssh_destroy ((SshHandle *)method_handle);
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	return ssh_read ((SshHandle *)method_handle, buffer, num_bytes,
			bytes_read);
}

/* alternative impl:
 * dd bs=1 conv=notrunc count=5 seek=60 of=/tmp/foo-test
 */
static GnomeVFSResult   
do_write (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written,
	  GnomeVFSContext *context)
{
	return ssh_write ((SshHandle *)method_handle, buffer, num_bytes,
			bytes_written);
}

static GnomeVFSResult 
do_open_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle **method_handle,
                   GnomeVFSURI *uri,
                   GnomeVFSFileInfoOptions options,
		   GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	char *cmd = NULL;
	GnomeVFSResult result;
	char *name;

	/* FIXME: escape for shell */
	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);

	if (strlen (name) > 0) {
		cmd = g_strdup_printf ("ls -l '%s'", name);
	} else {
		cmd = g_strdup_printf ("ls -l '/'");
	}

	result = ssh_connect (&handle, uri, cmd);
	g_free (name);
	g_free (cmd);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	handle->open_mode = GNOME_VFS_OPEN_NONE;
	handle->type = SSH_LIST;
	*method_handle = (GnomeVFSMethodHandle *)handle;

	return result;
	return GNOME_VFS_OK;
}

static GnomeVFSResult 
do_close_directory (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext *context)
{
	return ssh_destroy ((SshHandle *)method_handle);
}

#define LINE_LENGTH 4096 /* max line length we'll grok */

static GnomeVFSResult 
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
                   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	char line[LINE_LENGTH];
	char c;
	int i=0;
	GnomeVFSFileSize j;
	struct stat st;
	char *tempfilename, *filename, *linkname;

	for (;;) {
		tempfilename = NULL;
		filename = NULL;
		linkname = NULL;
		i = 0;
		j = 0;

		while (i<LINE_LENGTH) {
			result = ssh_read ((SshHandle *)method_handle, &c, 1, &j);
			if (j == 0 || c == '\r' || c == '\n') {
				break;
			}

			if (result != GNOME_VFS_OK) {
				return result;
			}

			line[i] = c;
			i++;
		}

		line[i] = 0;
		if (i == 0) {
			return GNOME_VFS_ERROR_EOF;
		}

		if (!gnome_vfs_parse_ls_lga (line, &st, &tempfilename, &linkname)) {
			/* Maybe the file doesn't exist? */
			if (strstr (line, "No such file or directory"))
				return GNOME_VFS_ERROR_NOT_FOUND;
			continue; /* skip to next line */
		}

		/* Get rid of the path */
		if (strrchr (tempfilename, '/') != NULL) {
			filename = g_strdup (strrchr (tempfilename,'/') + 1);
		} else {
			filename = g_strdup (tempfilename);
		}
		g_free (tempfilename);

		gnome_vfs_stat_to_file_info (file_info, &st);
		file_info->name = filename;
		if (linkname) {
			file_info->symlink_name = linkname;
		}

		/* FIXME: support symlinks correctly */

		file_info->mime_type = g_strdup 
			(gnome_vfs_get_file_mime_type (filename, &st, FALSE));

		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
		file_info->valid_fields &= 
			~GNOME_VFS_FILE_INFO_FIELDS_BLOCK_COUNT;
		file_info->valid_fields &= 
			~GNOME_VFS_FILE_INFO_FIELDS_IO_BLOCK_SIZE;

		/* Break out.
		   We are in a loop so we get the first 'ls' line;
		   often it starts with 'total 2213' etc.
		*/
		break;
	}

	return result;
}

GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
                  GnomeVFSFileInfo *file_info,
                  GnomeVFSFileInfoOptions options,
		  GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	char *cmd = NULL;
	GnomeVFSResult result;
	char *name;

	/* FIXME: escape for shell */
	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);

	if (strlen (name) > 0) {
		cmd = g_strdup_printf ("ls -ld '%s' 2>&1", name);
	} else {
		cmd = g_strdup_printf ("ls -ld '/' 2>&1");
	}

	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	handle->open_mode = GNOME_VFS_OPEN_NONE;
	handle->type = SSH_LIST;

	result = do_read_directory (method, (GnomeVFSMethodHandle *)handle,
				    file_info, context);

	ssh_destroy (handle);

	return (result == GNOME_VFS_ERROR_EOF ? GNOME_VFS_OK : result);
}

static GnomeVFSResult
do_make_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	char *cmd = NULL;
	GnomeVFSResult result;
	char *name;

	/* FIXME: escape for shell */
	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);

	/* FIXME: escape for shell */
	cmd = g_strdup_printf ("mkdir '%s'", name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	ssh_destroy (handle);

	return result;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	char *cmd = NULL;
	GnomeVFSResult result;
	gchar *name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	/* FIXME: escape for shell */
	cmd = g_strdup_printf ("rm -rf '%s'", name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	ssh_destroy (handle);

	return result;
}

GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *contet)
{
	SshHandle *handle = NULL;
	char *cmd = NULL;
	GnomeVFSResult result;
	gchar *name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	/* FIXME: escape for shell */
	cmd = g_strdup_printf ("rm -rf '%s'", name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	ssh_destroy (handle);

	return result;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  const GnomeVFSFileInfo *info,
		  GnomeVFSSetFileInfoMask mask,
		  GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	char *cmd = NULL;
	GnomeVFSResult result=GNOME_VFS_OK;
	gchar *full_name;

	full_name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (full_name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	if (mask & GNOME_VFS_SET_FILE_INFO_NAME) {
		char *encoded_dir;
		char *dir;
		char *new_name;

		encoded_dir = gnome_vfs_uri_extract_dirname (uri);
		dir = gnome_vfs_unescape_string (encoded_dir, G_DIR_SEPARATOR_S);
		g_free (encoded_dir);
		g_assert (dir != NULL);

		/* FIXME bugzilla.eazel.com 645: This needs to return
		 * an error for incoming names with "/" characters in
		 * them, instead of moving the file.
		 */

		if (dir[strlen (dir) - 1] != '/') {
			new_name = g_strconcat (dir, "/", info->name, NULL);
		} else {
			new_name = g_strconcat (dir, info->name, NULL);
		}

		/* FIXME: escape for shell */
		cmd = g_strdup_printf ("mv '%s' '%s'", full_name,
				       new_name);
		result = ssh_connect (&handle, uri, cmd);
		g_free (cmd);
		g_free (dir);
		g_free (new_name);

		/* Read all info from remote host */
		while (1) {
			char c;
			GnomeVFSResult res;
			GnomeVFSFileSize j;
			res = ssh_read (handle, &c, 1, &j);
			if (j == 0 || res != GNOME_VFS_OK)
				break;
		}

		ssh_destroy (handle);

		if (result != GNOME_VFS_OK) {
			g_free (full_name);
			return result;
		}
	}

	g_free (full_name);
	return result;
}

#if 0
static GnomeVFSResult  
do_get_file_info_from_handle (GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options)
{
	return GNOME_VFS_ERROR_WRONG_FORMAT;	
}
#endif

gboolean 
do_is_local (GnomeVFSMethod *method, const GnomeVFSURI *uri)
{
        return FALSE;
}

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
        return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
