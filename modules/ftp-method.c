/*
 * ftp-method.c: Implementation of the FTP method for the GNOME-VFS
 *
 * Authors:
 *   Ching Hui
 *   Jakub Jelinek
 *   Miguel de Icaza
 *   Norbert Warmuth
 *   Pavel Machek
 *
 * GNOME VFS adaptation:
 *   Miguel de Icaza, International GNOME Support.
 *
 * (C) 1999 International GNOME Support.
 * (C) 1995, 1996, 1997, 1998, 1999 The Free Software Foundation
 * (C) 1997, 1998, 1999 Norbert Warmuth
 */
#include <config.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include "ftp-method.h"
#include "util-url.h"

static GHashTable *conn_hash;

typedef struct {
	int  ref_count;
	char *hostname;
	char *username;
	char *password;
	char *current_directory;

	int  port;
	/*
	 * our connection to the remote end
	 */
	int  sock;
	
	/*
	 * For AmiTCP systems
	 */
	unsigned int  remote_is_amiga:1;

	/*
	 * ftp server doesn't understand 
	 * "LIST -la <path>"; use "CWD <path>"/
	 * "LIST" instead
	 */
	unsigned int  strict_rfc959_list_cmd;
} ftp_connection_t;

typedef struct {
	ftp_connection_t *conn;
	char             *path;
} ftp_uri_t;

static ftp_connection_t *
ftp_connection_ref (ftp_connection_t *conn)
{
	conn->ref_count++;

	return conn;
}

static void
ftp_connection_unref (ftp_connection_t *conn)
{
	conn->ref_count--;

	if (conn->ref_count == 0){
		g_free (conn->hostname);
		g_free (conn->username);
		g_free (conn->password);
		g_free (conn->current_directory);

		g_free (conn);
	}
}

static guint
hash_conn (gconstpointer key)
{
	const ftp_connection_t *conn = key;
	
	return g_str_hash (conn->hostname) + g_str_hash (conn->username);
}

static gint
equal_conn (gconstpointer a, gconstpointer b)
{
	const ftp_connection_t *ac;
	const ftp_connection_t *bc;

	ac = a;
	bc = b;

	if (strcmp (ac->hostname, bc->hostname))
		return 0;

	if (strcmp (ac->username, bc->username))
		return 0;

	if (strcmp (ac->password, bc->password))
		return 0;

	if (ac->port != bc->port)
		return 0;

	return 1;
}

static void
init_conn_hash (void)
{
	conn_hash = g_hash_table_new (hash_conn, equal_conn);
}

static ftp_connection_t *
lookup_conn (char *host, char *user, char *pass, int port)
{
	ftp_connection_t key;
	
	key.hostname = host;
	key.username = user;
	key.password = pass;
	key.port = port;

	return g_hash_table_lookup (conn_hash, &key);
}

static ftp_connection_t *
ftp_connection_new (char *hostname, char *username, char *password, char *path, int port)
{
	ftp_connection_t *conn;
			    
	conn = g_new (ftp_connection_t, 1);
	conn->ref_count = 1;
	conn->hostname = hostname;
	conn->username = username;
	conn->password = password;
	conn->port = port;

	conn->sock = 0;
	conn->remote_is_amiga = 0;
	conn->strict_rfc959_list_cmd = 0;

	if (!conn_hash)
		init_conn_hash ();

	g_hash_table_insert (conn_hash, conn, conn);
	
	return conn;
}

static ftp_uri_t *
ftpfs_parse_uri (GnomeVFSURI *uri)
{
	ftp_connection_t *conn;
	ftp_uri_t *ftp_uri;
	char *ftp_url;
	char *path, *host, *user, *pass;
	int  port;
	
	if (uri->text [0] != '/' || uri->text [1] != '/')
		return NULL;

	/* Skip the slashes */
	ftp_url = uri->text + 2;
	
	path = vfs_split_url (ftp_url, &host, &user, &port, &pass, 21, URL_DEFAULTANON);
	if (!path)
		return NULL;

	ftp_uri = g_new (ftp_uri_t, 1);
	ftp_uri->path = path;
	
	conn = lookup_conn (host, user, pass, port);
	if (conn){
		ftp_uri->conn = ftp_connection_ref (conn);

		g_free (host);
		g_free (user);
		g_free (pass);

		return ftp_uri;
	}

	ftp_uri->conn = ftp_connection_new (host, user, pass, path, port);

	return ftp_uri;
}

static void
ftp_uri_destroy (ftp_uri_t *uri)
{
	ftp_connection_unref (uri->conn);

	g_free (uri);
}

static GnomeVFSResult
ftpfs_open (GnomeVFSMethodHandle **method_handle,
	    GnomeVFSURI *uri,
	    GnomeVFSOpenMode mode)
{
	ftp_uri_t *ftp_uri;
	
	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	ftp_uri = ftpfs_parse_uri (uri);
	if (!ftp_uri)
		return GNOME_VFS_ERROR_WRONGFORMAT;

	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_create (GnomeVFSMethodHandle **method_handle,
	      GnomeVFSURI *uri,
	      GnomeVFSOpenMode mode,
	      gboolean exclusive,
	      guint perm)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_close (GnomeVFSMethodHandle *method_handle)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_read (GnomeVFSMethodHandle *method_handle,
	    gpointer buffer,
	    GnomeVFSFileSize num_bytes,
	    GnomeVFSFileSize *bytes_read)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_write (GnomeVFSMethodHandle *method_handle,
	     gconstpointer buffer,
	     GnomeVFSFileSize num_bytes,
	     GnomeVFSFileSize *bytes_written)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_seek (GnomeVFSMethodHandle *method_handle,
	    GnomeVFSSeekPosition whence,
	    GnomeVFSFileOffset offset)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_tell (GnomeVFSMethodHandle *method_handle,
	    GnomeVFSSeekPosition whence,
	    GnomeVFSFileOffset *offset_return)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_truncate (GnomeVFSMethodHandle *method_handle,
		glong where)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_open_directory (GnomeVFSMethodHandle **method_handle,
		      GnomeVFSURI *uri,
		      GnomeVFSFileInfoOptions options,
		      const GList *meta_keys,
		      const GnomeVFSDirectoryFilter *filter)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_close_directory (GnomeVFSMethodHandle *method_handle)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
ftpfs_read_directory (GnomeVFSMethodHandle *method_handle,
		      GnomeVFSFileInfo *file_info)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}


static GnomeVFSResult
ftpfs_get_file_info (GnomeVFSURI *uri,
		     GnomeVFSFileInfo *file_info,
		     GnomeVFSFileInfoOptions options,
		     const GList *meta_keys)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}


static gboolean
ftpfs_is_local (const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);
	
	/* We are never a native file system */
	return FALSE;
}

static GnomeVFSResult
ftpfs_make_directory (GnomeVFSURI *uri, guint perm)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSMethod method = {
	ftpfs_open,
	ftpfs_create,
	ftpfs_close,
	ftpfs_read,
	ftpfs_write,
	ftpfs_seek,
	ftpfs_tell,
	ftpfs_truncate,
	ftpfs_open_directory,
	ftpfs_close_directory,
	ftpfs_read_directory,
	ftpfs_get_file_info,
	ftpfs_is_local,
	ftpfs_make_directory,
	NULL
};

GnomeVFSMethod *
init (void)
{
	return &method;
}
