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
#include <signal.h>
#include <sys/signal.h>
#include <fcntl.h>

#define D(x)
/* #define D(x) g_print x */

#define LINE_LENGTH 4096	/* max line length we'll grok */
#define SLEEP_TIME 300000	/* time we'll wait for the process to finish */

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
	int error_fd;
	pid_t pid;
	GnomeVFSFileInfoOptions info_opts;
} SshHandle;

static GnomeVFSResult ssh_read_error	(int error_fd,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read);
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
static GnomeVFSResult do_move            (GnomeVFSMethod *method,
					  GnomeVFSURI *old_uri,
					  GnomeVFSURI *new_uri,
					  gboolean force_replace,
					  GnomeVFSContext *context);
static GnomeVFSResult do_unlink         (GnomeVFSMethod *method,
					 GnomeVFSURI *uri,
					 GnomeVFSContext *context);
static GnomeVFSResult do_check_same_fs	(GnomeVFSMethod  *method,
					 GnomeVFSURI     *a,
					 GnomeVFSURI     *b,
					 gboolean        *same_fs_return,
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
	do_move,
	do_unlink, /* unlink */
	do_check_same_fs,
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
	char ** argv;
	SshHandle *handle;
	char *command_line, *host_port;
	const gchar *username, *hostname;
	int argc;
	GError *gerror = NULL;
	GnomeVFSFileSize bytes_read;
	char buffer[LINE_LENGTH];
	GnomeVFSResult res;

	/* We do support ssh:/// as ssh://localhost/ */
	hostname = gnome_vfs_uri_get_host_name (uri);
	if (hostname == NULL) {
		hostname = "localhost";
	}

	username = gnome_vfs_uri_get_user_name(uri);
	if (username == NULL) {
		username = g_get_user_name();
	}

	host_port = g_strdup_printf("%d", gnome_vfs_uri_get_host_port(uri) ?
			gnome_vfs_uri_get_host_port(uri) : 22);

	command_line  = g_strconcat ("ssh -oBatchMode=yes -x -l ", 
				     username,
				     " -p ", host_port,
				     " ", hostname,
				     " ",
				     "\"LC_ALL=C; echo READY > /dev/stderr;",
				     command, "; echo DONE > /dev/stderr\"",
				     NULL);
	g_free (host_port);

	D(("ssh_connect () command: %s\n", command_line));

	g_shell_parse_argv (command_line, &argc, &argv, &gerror);

	g_free (command_line);
	if (gerror) {
		g_warning (gerror->message);
		return GNOME_VFS_ERROR_BAD_PARAMETERS;
	}

	handle = g_new0 (SshHandle, 1);
	handle->uri = uri;

	g_spawn_async_with_pipes (NULL, argv, NULL, 
				  G_SPAWN_SEARCH_PATH,
				  NULL, NULL,
				  &handle->pid, &handle->write_fd, &handle->read_fd,
				  &handle->error_fd, &gerror);
	g_strfreev (argv);

	if (gerror) {
		g_warning (gerror->message);
		g_free (handle);
		return GNOME_VFS_ERROR_GENERIC;
	}

	gnome_vfs_uri_ref (handle->uri);

	*handle_return = handle;

	/* You can add more error checking here */
	memset (buffer, '\0', LINE_LENGTH);
	res = ssh_read_error (handle->error_fd, &buffer,
			LINE_LENGTH, &bytes_read);

	if (bytes_read != 0 && res == GNOME_VFS_OK) {
		if (strncmp ("READY", buffer, strlen ("READY")) == 0) {
			res = GNOME_VFS_OK;
		} else if (strncmp ("Permission denied", buffer,
					strlen("Permission denied")) == 0) {
			res = GNOME_VFS_ERROR_LOGIN_FAILED;
		} else if (strncmp ("Host key verification failed", buffer,
					strlen("Host key verification failed"))
				== 0) {
			res = GNOME_VFS_ERROR_SERVICE_NOT_AVAILABLE;
		} else if (strstr (buffer, "Connection refused") != NULL) {
			res = GNOME_VFS_ERROR_ACCESS_DENIED;
		}
	}

	return res;
}

static GnomeVFSResult
ssh_destroy (SshHandle *handle)
{
	close (handle->read_fd);
	close (handle->write_fd);
	close (handle->error_fd);
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
ssh_read_error (int error_fd,
		gpointer buffer,
		GnomeVFSFileSize num_bytes,
		GnomeVFSFileSize *bytes_read)
{
	GnomeVFSFileSize my_read;

	my_read = (GnomeVFSFileSize) read (error_fd, buffer,
			(size_t) num_bytes);

	if (my_read == -1) {
		return gnome_vfs_result_from_errno ();
	}

	*bytes_read = my_read;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
ssh_check_for_done (SshHandle *handle)
{
	char buffer[LINE_LENGTH];
	GnomeVFSResult res;
	GnomeVFSFileSize bytes_read;

	memset (buffer, '\0', LINE_LENGTH);
	res = ssh_read_error (handle->error_fd, &buffer,
			LINE_LENGTH, &bytes_read);

	if (bytes_read != 0 && res == GNOME_VFS_OK) {
		if (strncmp ("DONE", buffer, strlen ("DONE")) == 0) {
			return res;
		}
	}

	return GNOME_VFS_ERROR_GENERIC;
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

static GnomeVFSResult
ssh_wait_and_destroy (SshHandle *handle, GnomeVFSContext *context)
{
	int i;

	/* That's 30 seconds */
	for (i = 0; i < 100 && kill (handle->pid, 0) != -1; i++) {
		if (gnome_vfs_context_check_cancellation (context) == TRUE) {
			break;
		}
		usleep (SLEEP_TIME);
	}

	return ssh_destroy (handle);
}


#if 0
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

	if (mode == GNOME_VFS_OPEN_READ) {
		char *name;
		char *quoted_name;
		name = gnome_vfs_unescape_string (uri->text, 
						  G_DIR_SEPARATOR_S);
		quoted_name = g_shell_quote (name);
		g_free (name);

		cmd = g_strdup_printf ("cat %s", quoted_name);
		result = ssh_connect (&handle, uri, cmd);
		g_free (cmd);
		g_free (quoted_name);
		if (result != GNOME_VFS_OK) {
			return result;
		}
	} else {
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	}

	handle->open_mode = mode;
	handle->type = SSH_FILE;
	*method_handle = (GnomeVFSMethodHandle *)handle;

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
	SshHandle *handle = NULL;
	char *cmd;
	GnomeVFSResult result;
	char *name;
	char *quoted_name;

	if (mode != GNOME_VFS_OPEN_WRITE) {
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	}

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	quoted_name = g_shell_quote (name);

	cmd = g_strdup_printf ("cat > %s", quoted_name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);
	g_free (quoted_name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	/* FIXME: set perm */

	handle->open_mode = mode;
	handle->type = SSH_FILE;
	*method_handle = (GnomeVFSMethodHandle *)handle;

	return result;
}

static GnomeVFSResult   
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	return ssh_wait_and_destroy ((SshHandle *)method_handle, context);
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	GnomeVFSResult result, res_secondary;

	result =  ssh_read ((SshHandle *)method_handle, buffer, num_bytes,
			bytes_read);
	if (result != GNOME_VFS_OK) {
		return result;
	}

	if (*bytes_read == 0) {	
		res_secondary = ssh_check_for_done ((SshHandle *)method_handle);
		if (res_secondary != GNOME_VFS_OK) {
			result = res_secondary;
		} else {
			result = GNOME_VFS_ERROR_EOF;
		}
	}
	return result;
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
	char *quoted_name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (name == NULL) {
		return GNOME_VFS_ERROR_NOT_FOUND;
	}

	quoted_name = g_shell_quote (name);

	if (*name != '\0') {
		cmd = g_strdup_printf ("ls -l %s", quoted_name);
	} else {
		cmd = g_strdup_printf ("ls -l '/'");
	}

	result = ssh_connect (&handle, uri, cmd);
	g_free (quoted_name);
	g_free (name);
	g_free (cmd);

	if (result != GNOME_VFS_OK) {
		return result;
	}
	handle->info_opts = options;
	handle->open_mode = GNOME_VFS_OPEN_NONE;
	handle->type = SSH_LIST;
	*method_handle = (GnomeVFSMethodHandle *)handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult 
do_close_directory (GnomeVFSMethod *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext *context)
{
	return ssh_destroy ((SshHandle *)method_handle);
}

static void
get_access_info (GnomeVFSURI *uri, GnomeVFSFileInfo *file_info)
{
     gint i;
     gchar *name;
     gchar *quoted_name;
     struct param {
             char c;
             GnomeVFSFilePermissions perm;
     };
     struct param params[2] = {{'r', GNOME_VFS_PERM_ACCESS_READABLE},
                               {'w', GNOME_VFS_PERM_ACCESS_WRITABLE}};

     name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);


     if ( *name == '\0' ) {
             quoted_name = g_shell_quote ("/");
     } else {
             quoted_name = g_shell_quote (name);
     }
     g_free (name);

     for (i = 0; i<2; i++) {
             gchar c;
             gchar *cmd;
             SshHandle *handle;
             GnomeVFSFileSize bytes_read;
             GnomeVFSResult result;

             cmd = g_strdup_printf ("test -%c %s && echo $?", 
                                    params[i].c, quoted_name);
             result = ssh_connect (&handle, uri, cmd);
             g_free (cmd);
             
             if (result != GNOME_VFS_OK) {
                     g_free(quoted_name);
                     return;
             }               
             
             result = ssh_read (handle, &c, 1, &bytes_read);
             if ((bytes_read > 0) && (c == '0')) {
                     file_info->permissions |= params[i].perm;
             } else {
                     file_info->permissions &= ~params[i].perm;
             }
                     
             ssh_destroy (handle);
     }

     file_info->permissions &= ~GNOME_VFS_PERM_ACCESS_EXECUTABLE;
     file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_ACCESS;

     g_free(quoted_name);
}

static GnomeVFSResult 
do_read_directory (GnomeVFSMethod *method,
		   GnomeVFSMethodHandle *method_handle,
                   GnomeVFSFileInfo *file_info,
		   GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK, res_secondary;
	char line[LINE_LENGTH+1];
	char c;
	int i=0;
	GnomeVFSFileSize bytes_read;
	struct stat st;
	char *tempfilename, *filename, *linkname;
	SshHandle *handle = (SshHandle *)method_handle;
	const char *mime_type;
	char *target_name, *parent;

	for (;;) {
		tempfilename = NULL;
		filename = NULL;
		linkname = NULL;
		i = 0;
		bytes_read = 0;

		while (i<LINE_LENGTH) {
			result = ssh_read (handle, &c,
					   sizeof(char), &bytes_read);
			if (bytes_read == 0 || c == '\r' || c == '\n') {
				break;
			}

			if (result != GNOME_VFS_OK) {
				res_secondary = ssh_check_for_done (handle);
				if (res_secondary != GNOME_VFS_OK) {
					result = res_secondary;
				} else {
					return result;
				}
			}

			line[i] = c;
			i++;
		}
		/* Here we can have i == LINE_LENGTH which explains 
		 * why the size of line is LINE_LENGTH+1
		 */
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
			GNOME_VFS_FILE_INFO_SET_SYMLINK (file_info, TRUE);
			file_info->valid_fields
				|= GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME;
			file_info->symlink_name = linkname;
			D(("symlink: %s\n", file_info->symlink_name));
		}

		/* FIXME: support symlinks to directories correctly */

		if (handle->info_opts & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
			if (file_info->type == GNOME_VFS_FILE_TYPE_SYMBOLIC_LINK) {
				if ((handle->info_opts & GNOME_VFS_FILE_INFO_FOLLOW_LINKS) == 0) {
					mime_type = "x-special/symlink";
				} else {
					parent = gnome_vfs_uri_to_string (handle->uri, 0);
					target_name = gnome_vfs_make_uri_full_from_relative (parent, file_info->symlink_name);
					mime_type = gnome_vfs_get_file_mime_type (target_name, NULL, FALSE);
					D(("Looking up mime-type for '%s'\n\n", target_name));
					g_free (target_name);
					g_free (parent);
				}
			} else {
				mime_type = (gnome_vfs_get_file_mime_type
					 (filename, &st, FALSE));
			}

			file_info->mime_type = g_strdup (mime_type);
			file_info->valid_fields 
				|= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
		}

		file_info->valid_fields &= 
			~GNOME_VFS_FILE_INFO_FIELDS_BLOCK_COUNT;
		file_info->valid_fields &= 
			~GNOME_VFS_FILE_INFO_FIELDS_IO_BLOCK_SIZE;
		if (handle->info_opts & GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS) {
			get_access_info (handle->uri, file_info);
		}

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
	char *quoted_name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	quoted_name = g_shell_quote (name);

	if (*name != '\0') {
		cmd = g_strdup_printf ("ls -ld %s 2>&1", quoted_name);
	} else {
		cmd = g_strdup_printf ("ls -ld '/' 2>&1");
	}

	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);
	g_free (quoted_name);

	if (result != GNOME_VFS_OK) {
		return result;
	}
	handle->info_opts = options;
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
	char *quoted_name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	quoted_name = g_shell_quote (name);

	cmd = g_strdup_printf ("mkdir %s", quoted_name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);
	g_free (quoted_name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	ssh_wait_and_destroy (handle, context);

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
	gchar *quoted_name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;
	quoted_name = g_shell_quote (name);

	cmd = g_strdup_printf ("rm -rf %s", quoted_name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);
	g_free (quoted_name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	ssh_wait_and_destroy (handle, context);

	return result;
}

static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	GnomeVFSResult res;
	gboolean same_fs;
	char *old_name, *new_name;
	char *quoted_old_name, *quoted_new_name;
	char *cmd;

	res = do_check_same_fs (method, old_uri, new_uri, &same_fs, context);
	if (res != GNOME_VFS_OK) {
		return res;
	}

	old_name = gnome_vfs_unescape_string (old_uri->text, G_DIR_SEPARATOR_S);
	new_name = gnome_vfs_unescape_string (new_uri->text, G_DIR_SEPARATOR_S);

	quoted_old_name = g_shell_quote (old_name);
	quoted_new_name = g_shell_quote (new_name);

	cmd = g_strdup_printf ("mv %s %s", quoted_old_name, quoted_new_name);
	res = ssh_connect (&handle, old_uri, cmd);
	g_free (cmd);
	g_free (old_name);
	g_free (new_name);
	g_free (quoted_old_name);
	g_free (quoted_new_name);

	if (res != GNOME_VFS_OK) {
		return res;
	}

	ssh_wait_and_destroy (handle, context);

	return res;
}

GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	SshHandle *handle = NULL;
	char *cmd;
	GnomeVFSResult result;
	gchar *name;
	gchar *quoted_name;

	name = gnome_vfs_unescape_string (uri->text, G_DIR_SEPARATOR_S);
	if (name == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;
	quoted_name = g_shell_quote (name);

	cmd = g_strdup_printf ("rm -rf %s", quoted_name);
	result = ssh_connect (&handle, uri, cmd);
	g_free (cmd);
	g_free (name);
	g_free (quoted_name);

	if (result != GNOME_VFS_OK) {
		return result;
	}

	ssh_wait_and_destroy (handle, context);

	return result;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod  *method,
		GnomeVFSURI     *a,
		GnomeVFSURI     *b,
		gboolean        *same_fs_return,
		GnomeVFSContext *context)
{
	const gchar *a_host_name, *b_host_name;
	const gchar *a_user_name, *b_user_name;

	g_return_val_if_fail (a != NULL, GNOME_VFS_ERROR_INTERNAL);
	g_return_val_if_fail (b != NULL, GNOME_VFS_ERROR_INTERNAL);

	a_host_name = gnome_vfs_uri_get_host_name (a);
	b_host_name = gnome_vfs_uri_get_host_name (b);
	a_user_name = gnome_vfs_uri_get_user_name (a);
	b_user_name = gnome_vfs_uri_get_user_name (b);

	g_return_val_if_fail (a_host_name != NULL, GNOME_VFS_ERROR_INVALID_URI);        g_return_val_if_fail (b_host_name != NULL, GNOME_VFS_ERROR_INVALID_URI);                                                                                
	if (a_user_name == NULL) {
		a_user_name = g_get_user_name ();
	}
	if (b_user_name == NULL) {
		b_user_name = g_get_user_name ();
	}

	if (same_fs_return != NULL) {
		*same_fs_return =
			((!strcmp (a_host_name, b_host_name))
			 && (!strcmp (a_user_name, b_user_name)));
	}

	return GNOME_VFS_OK;
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
		char *quoted_full_name;
		char *quoted_new_name;

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
		quoted_new_name = g_shell_quote (new_name);
		quoted_full_name = g_shell_quote (full_name);
		cmd = g_strdup_printf ("mv %s %s", quoted_full_name,
				       quoted_new_name);
		result = ssh_connect (&handle, uri, cmd);
		g_free (cmd);
		g_free (dir);
		g_free (new_name);
		g_free (quoted_new_name);
		g_free (quoted_full_name);
		g_free (full_name);

		if (result != GNOME_VFS_OK) {
			return result;
		}

		/* Read all info from remote host */
		while (1) {
			char c;
			GnomeVFSResult res;
			GnomeVFSFileSize bytes_read;
			res = ssh_read (handle, &c, 1, &bytes_read);
			if (bytes_read == 0 || res != GNOME_VFS_OK)
				break;
		}

		ssh_wait_and_destroy (handle, context);
	}

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
