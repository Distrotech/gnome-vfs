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
#include <sys/param.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "gnome-vfs.h"
#include "gnome-vfs-private.h"

#include <netdb.h>		/* struct hostent */
#include <sys/socket.h>		/* AF_INET */
#include <netinet/in.h>		/* struct in_addr */
#ifdef HAVE_SETSOCKOPT
#    include <netinet/ip.h>	/* IP options */
#endif
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <arpa/telnet.h>
#include "ftp-method.h"
#include "util-url.h"

#define IS_LINEAR(mode) (! ((mode) & GNOME_VFS_OPEN_RANDOM))

static GHashTable *connections_hash;
static FILE *logfile;
static int code;

/* command wait_flag: */
#define NONE        0x00
#define WAIT_REPLY  0x01
#define WANT_STRING 0x02
static char reply_str [80];

static void
print_vfs_message (char *msg, ...)
{
	char *str;
	va_list args;
	
	va_start  (args,msg);
	str = g_strdup_vprintf (msg, args);
	va_end    (args);
	
	puts (str);
	g_free (str);
}

static ftpfs_connection_t *
ftpfs_connection_ref (ftpfs_connection_t *conn)
{
	conn->ref_count++;

	return conn;
}

static void
ftpfs_connection_unref (ftpfs_connection_t *conn)
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
	const ftpfs_connection_t *conn = key;
	
	return g_str_hash (conn->hostname) + g_str_hash (conn->username);
}

static gint
equal_conn (gconstpointer a, gconstpointer b)
{
	const ftpfs_connection_t *ac;
	const ftpfs_connection_t *bc;

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
init_connections_hash (void)
{
	connections_hash = g_hash_table_new (hash_conn, equal_conn);
}

static ftpfs_connection_t *
lookup_conn (char *host, char *user, char *pass, int port)
{
	ftpfs_connection_t key;
	
	key.hostname = host;
	key.username = user;
	key.password = pass;
	key.port = port;

	return g_hash_table_lookup (connections_hash, &key);
}

static ftpfs_connection_t *
ftpfs_connection_new (char *hostname, char *username, char *password, char *path, int port)
{
	ftpfs_connection_t *conn;
			    
	conn = g_new (ftpfs_connection_t, 1);
	conn->ref_count = 1;
	conn->hostname = hostname;
	conn->username = username;
	conn->password = password;
	conn->port = port;

	conn->sock = 0;
	conn->remote_is_amiga = 0;
	conn->strict_rfc959_list_cmd = 0;

	if (!connections_hash)
		init_connections_hash ();

	g_hash_table_insert (connections_hash, conn, conn);
	
	return conn;
}

static ftpfs_uri_t *
ftpfs_parse_uri (GnomeVFSURI *uri)
{
	ftpfs_connection_t *conn;
	ftpfs_uri_t *ftpfs_uri;
	char *ftpfs_url;
	char *path, *host, *user, *pass;
	int  port;
	
	if (uri->text [0] != '/' || uri->text [1] != '/')
		return NULL;

	/* Skip the slashes */
	ftpfs_url = uri->text + 2;
	
	path = vfs_split_url (ftpfs_url, &host, &user, &port, &pass, 21, URL_DEFAULTANON);
	if (!path)
		return NULL;

	ftpfs_uri = g_new (ftpfs_uri_t, 1);
	ftpfs_uri->path = path;
	
	conn = lookup_conn (host, user, pass, port);
	if (conn){
		ftpfs_uri->conn = ftpfs_connection_ref (conn);

		g_free (host);
		g_free (user);
		g_free (pass);

		return ftpfs_uri;
	}

	ftpfs_uri->conn = ftpfs_connection_new (host, user, pass, path, port);

	return ftpfs_uri;
}

static void
ftpfs_uri_destroy (ftpfs_uri_t *uri)
{
	ftpfs_connection_unref (uri->conn);
	g_free (uri->path);
	g_free (uri);
}

static void
ftpfs_direntry_unref (ftpfs_direntry_t *fe)
{
	g_return_if_fail (fe != NULL);
	
	fe->ref_count--;
	if (fe->ref_count != 0)
		return;

	if (fe->name)
		g_free (fe->name);
	if (fe->linkname)
		g_free (fe->linkname);
	if (fe->local_filename){
		if (fe->local_is_temp) {
			if (!fe->local_stat.st_mtime)
				unlink (fe->local_filename);
			else {
				struct stat sb;
				
				/* Delete only if it hasn't changed */
				if (stat (fe->local_filename, &sb) >=0 && 
				    fe->local_stat.st_mtime == sb.st_mtime)
					unlink (fe->local_filename);
			}
		}
		g_free (fe->local_filename);
		fe->local_filename = NULL;
	}
	if (fe->remote_filename)
		g_free (fe->remote_filename);
	if (fe->l_stat)
		g_free (fe->l_stat);
	g_free(fe);
}

static int
get_line (int sock, char *buf, int buf_len, char term)
{
	int i, status;
	char c;
	
	for (i = 0; i < buf_len; i++, buf++) {
		if (read(sock, buf, sizeof(char)) <= 0)
			return 0;
		if (logfile){
			fwrite (buf, 1, 1, logfile);
			fflush (logfile);
		}
		if (*buf == term) {
			*buf = 0;
			return 1;
		}
	}
	*buf = 0;
	while ((status = read(sock, &c, sizeof(c))) > 0){
		if (logfile){
			fwrite (&c, 1, 1, logfile);
			fflush (logfile);
		}
		if (c == '\n')
			return 1;
	}
	return 0;
}


/* Returns a reply code, check /usr/include/arpa/ftp.h for possible values */
static int
get_reply (int sock, char *string_buf, int string_len)
{
	char answer [1024];
	int i;
	
	for (;;) {
		if (!get_line (sock, answer, sizeof (answer), '\n')){
			if (string_buf)
				*string_buf = 0;
			code = 421;
			return 4;
		}
		switch (sscanf(answer, "%d", &code)){
		case 0:
			if (string_buf) {
				strncpy (string_buf, answer, string_len - 1);
				*(string_buf + string_len - 1) = 0;
			}
			code = 500;
			return 5;
		case 1:
			if (answer[3] == '-') {
				while (1) {
					if (!get_line (sock, answer, sizeof(answer), '\n')){
						if (string_buf)
							*string_buf = 0;
						code = 421;
						return 4;
					}
					if ((sscanf (answer, "%d", &i) > 0) && 
					    (code == i) && (answer[3] == ' '))
						break;
				}
			}
			if (string_buf){
				strncpy (string_buf, answer, string_len - 1);
				*(string_buf + string_len - 1) = 0;
			}
			return code / 100;
		}
	}
}

static int
command (ftpfs_connection_t *conn, int wait_reply, char *fmt, ...)
{
	va_list ap;
	char *str, *fmt_str;
	int status;
	int sock = conn->sock;
	int got_sigpipe;
	
	va_start (ap, fmt);
	
	fmt_str = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	
	str = g_strconcat (fmt_str, "\r\n", NULL);
	g_free (fmt_str);

	if (logfile){
		if (strncmp (str, "PASS ", 5) == 0){
			char *tmp = "PASS <Password not logged>\r\n";
			
			fwrite (tmp, strlen (tmp), 1, logfile);
		} else
			fwrite (str, strlen (str), 1, logfile);
		
		fflush (logfile);
	}

	got_sigpipe = 0;
	status = write (sock, str, strlen (str));
	g_free (str);
    
	if (status < 0){
		code = 421;
		
		if (errno == EPIPE){
			got_sigpipe = 1;
		}
		return TRANSIENT;
	}
	if (wait_reply)
		return get_reply (
			sock, (wait_reply & WANT_STRING) ? reply_str : NULL,
			sizeof (reply_str)-1);
	return COMPLETE;
}

static void
abort_transfer (ftpfs_connection_t *conn, int dsock)
{
	static unsigned char ipbuf[3] = { IAC, IP, IAC };
	fd_set mask;
	char buf [1024];
	
	print_vfs_message (_("ftpfs: aborting transfer."));
	if (send (conn->sock, ipbuf, sizeof (ipbuf), MSG_OOB) != sizeof(ipbuf)) {
		print_vfs_message(_("ftpfs: abort error: %s"), g_strerror (errno));
		return;
	}
    
	if (command (conn, NONE, "%cABOR", DM) != COMPLETE){
		print_vfs_message (_("ftpfs: abort failed"));
		return;
	}

	if (dsock != -1) {
		FD_ZERO (&mask);
		FD_SET (dsock, &mask);
		if (select (dsock + 1, &mask, NULL, NULL, NULL) > 0)
			while (read (dsock, buf, sizeof(buf)) > 0)
				;
	}

	if ((get_reply (conn->sock, NULL, 0) == TRANSIENT) && (code == 426))
		get_reply (conn->sock, NULL, 0);
}

/* Setup Passive ftp connection, we use it for source routed connections */
static int
setup_passive (int my_socket, ftpfs_connection_t *conn, struct sockaddr_in *sa)
{
	int xa, xb, xc, xd, xe, xf;
	char n [6];
	char *c = reply_str;
	
	if (command (conn, WAIT_REPLY | WANT_STRING, "PASV") != COMPLETE)
		return 0;
	
	/* Parse remote parameters */
	for (c = reply_str + 4; (*c) && (!isdigit (*c)); c++)
		;
	if (!*c)
		return 0;
	if (!isdigit (*c))
		return 0;
	if (sscanf (c, "%d,%d,%d,%d,%d,%d", &xa, &xb, &xc, &xd, &xe, &xf) != 6)
		return 0;
	n [0] = (unsigned char) xa;
	n [1] = (unsigned char) xb;
	n [2] = (unsigned char) xc;
	n [3] = (unsigned char) xd;
	n [4] = (unsigned char) xe;
	n [5] = (unsigned char) xf;
	
	memcpy (&(sa->sin_addr.s_addr), (void *)n, 4);
	memcpy (&(sa->sin_port),        (void *)&n[4], 2);

	if (connect (my_socket, (struct sockaddr *) sa, sizeof (struct sockaddr_in)) < 0)
		return 0;

	return 1;
}

static int
initconn (ftpfs_connection_t *conn)
{
	struct sockaddr_in data_addr;
	int data, len = sizeof(data_addr);
	struct protoent *pe;
	
	getsockname (conn->sock, (struct sockaddr *) &data_addr, &len);
	data_addr.sin_port = 0;
	
	pe = getprotobyname("tcp");
	if (pe == NULL)
		return -1;
	
	data = socket (AF_INET, SOCK_STREAM, pe->p_proto);
	if (data < 0)
		return -1;

	if (conn->use_passive_connection){
		if ((conn->use_passive_connection = setup_passive (data, conn, &data_addr)))
			return data;

		conn->use_passive_connection = 0;
		print_vfs_message (_("ftpfs: could not setup passive mode"));
	}

	/* If passive setup fails, fallback to active connections */
	/* Active FTP connection */
	if (bind (data, (struct sockaddr *)&data_addr, len) < 0)
		goto error_return;

	getsockname(data, (struct sockaddr *) &data_addr, &len);
	if (listen (data, 1) < 0)
		goto error_return;
	{
		unsigned char *a = (unsigned char *)&data_addr.sin_addr;
		unsigned char *p = (unsigned char *)&data_addr.sin_port;
		
		if (command (conn, WAIT_REPLY, "PORT %d,%d,%d,%d,%d,%d", a[0], a[1], 
			     a[2], a[3], p[0], p[1]) != COMPLETE)
			goto error_return;
	}
	return data;

 error_return:
	close(data);
	return -1;
}


/* some defines only used by changetype */
/* These two are valid values for the second parameter */
#define TYPE_ASCII    0
#define TYPE_BINARY   1

/* This one is only used to initialize bucket->isbinary, don't use it as
   second parameter to changetype. */
#define TYPE_UNKNOWN -1 

static int
changetype (ftpfs_connection_t *conn, int binary)
{
	if (binary != conn->is_binary) {
		if (command (conn, WAIT_REPLY, "TYPE %c", binary ? 'I' : 'A') != COMPLETE)
			return -1;
		conn->is_binary = binary;
	}
	return binary;
}

/*
 * char *translate_path (struct ftpfs_connection_t *conn, char *remote_path)
 * Translate a Unix path, i.e. gnome-vfs's internal path representation (e.g. 
 * /somedir/somefile) to a path valid for the remote server. Every path 
 * transfered to the remote server has to be mangled by this function 
 * right prior to sending it.
 * Currently only Amiga ftp servers are handled in a special manner.
   
 * When the remote server is an amiga:
 * a) strip leading slash if necesarry
 * b) replace first occurance of ":/" with ":"
 * c) strip trailing "/."
 */

static char *
translate_path (ftpfs_connection_t *conn, char *remote_path)
{
	char *p;
	
	if (!conn->remote_is_amiga)
		return g_strdup (remote_path);
	else {
		char *ret;
		
		if (logfile) {
			fprintf (logfile, "ftpfs -- translate_path: %s\n", remote_path);
			fflush (logfile);
		}
		
		/*
		 * Don't change "/" into "", e.g. "CWD " would be
		 * invalid.
		 */
		if (*remote_path == '/' && remote_path[1] == '\0')
			return g_strdup ("."); 
		
		/* strip leading slash */
		if (*remote_path == '/')
			ret = g_strdup (remote_path + 1);
		else
			ret = g_strdup (remote_path);
		
		/* replace first occurance of ":/" with ":" */
		if ((p = strchr (ret, ':')) && *(p + 1) == '/')
			strcpy (p + 1, p + 2);
		
		/* strip trailing "/." */
		if ((p = strrchr (ret, '/')) && *(p + 1) == '.' && *(p + 2) == '\0')
			*p = '\0';
		return ret;
	}
}

static int
open_data_connection (ftpfs_connection_t *conn,
		      char *cmd, char *remote, 
		      int isbinary, int reget)
{
	struct sockaddr_in from;
	int s, j, data, fromlen = sizeof(from);
	
	if ((s = initconn (conn)) == -1)
		return -1;
	if (changetype (conn, isbinary) == -1)
		return -1;

	if (reget > 0){
		j = command (conn, WAIT_REPLY, "REST %d", reget);
		if (j != CONTINUE)
			return -1;
	}
	if (remote){
		j = command (conn, WAIT_REPLY, "%s %s", cmd, 
			     translate_path (conn, remote));
	} else
		j = command (conn, WAIT_REPLY, "%s", cmd);
	if (j != PRELIM)
		return -1;

	if (conn->use_passive_connection)
		data = s;
	else {
		data = accept (s, (struct sockaddr *)&from, &fromlen);
		if (data < 0) {
			close(s);
			return -1;
		}
		close(s);
	} 
	return data;
}

static GnomeVFSResult
linear_start (ftpfs_direntry_t *fe, int offset)
{
	fe->local_stat.st_mtime = 0;
	fe->data_sock = open_data_connection (
		fe->conn, "RETR", fe->remote_filename, TYPE_BINARY, offset);
	if (fe->data_sock == -1)
		return GNOME_VFS_ERROR_ACCESSDENIED;
	
	fe->linear_state = LS_LINEAR_OPEN;
	return GNOME_VFS_OK;
}

static void
linear_abort (ftpfs_direntry_t *fe)
{
	abort_transfer (fe->conn, fe->data_sock);
	fe->data_sock = -1;
}

static GnomeVFSResult
linear_read (ftpfs_direntry_t *fe, void *buf, int len)
{
	int n;
	GnomeVFSResult error = GNOME_VFS_OK;
	
	while ((n = read (fe->data_sock, buf, len)) < 0) {
		if (errno == EINTR)
			continue;
		break;
	}

	if (n < 0)
	    linear_abort (fe);

	if (!n) {
		if ((get_reply (fe->conn->sock, NULL, 0) != COMPLETE))
			error = GNOME_VFS_ERROR_IO;
		close (fe->data_sock);
		fe->data_sock = -1;
	}
	return error;
}

static void
linear_close (ftpfs_direntry_t *fe)
{
	if (fe->data_sock != -1)
		linear_abort (fe);
}

static ftpfs_dir_t *
retrieve_dir (ftpfs_uri_t *uri, char *remote_path, gboolean resolve_symlinks)
{
	g_warning ("FIXME");
	return NULL;
}

/* If you want reget, you'll have to open file with O_LINEAR */
static GnomeVFSResult
retrieve_file (ftpfs_direntry_t *fe)
{
	GnomeVFSResult ret;
	int total = 0;
	char buffer [8192];
	int local_handle, n;
	int stat_size = fe->s.st_size;
	
	if (fe->local_filename)
		return GNOME_VFS_OK;
	
	if (!(fe->local_filename = tempnam (NULL, "ftpfs")))
		return GNOME_VFS_ERROR_NOMEM;

	fe->local_is_temp = 1;
	
	local_handle = open (fe->local_filename, O_RDWR | O_CREAT | O_TRUNC | O_EXCL, 0600);
	if (local_handle == -1) 
		return GNOME_VFS_ERROR_NOSPACE;

	ret = linear_start (fe, 0);
	if (ret != GNOME_VFS_OK)
		goto error_3;

	while (1) {
		if ((n = linear_read (fe, buffer, sizeof (buffer))) < 0){
			ret = GNOME_VFS_ERROR_IO;
			goto error_1;
		}
		if (!n)
			break;
		
		total += n;
		print_vfs_message ("Getting file", fe->remote_filename, total, stat_size);
		
		while (write (local_handle, buffer, n) < 0) {
			if (errno == EINTR) {
#warning Here
				ret = GNOME_VFS_ERROR_IO;
				goto error_2;
			} else
				continue;
			goto error_1;
		}
	}

	linear_close (fe);
	close (local_handle);
	
	if (stat (fe->local_filename, &fe->local_stat) < 0)
		fe->local_stat.st_mtime = 0;
	
	return GNOME_VFS_OK;
 error_1:
 error_2:
	linear_close(fe);
 error_3:
	close (local_handle);
	unlink (fe->local_filename);
	g_free (fe->local_filename);
	fe->local_filename = NULL;
	return ret;
}

static GnomeVFSResult
get_file_entry (ftpfs_uri_t *uri, int flags,
		GnomeVFSOpenMode mode, gboolean exclusive,
		ftpfs_direntry_t **retval)
{
	ftpfs_direntry_t *fe;
	ftpfs_dir_t *dir;
	GList *l;
	char *filename = g_basename (uri->path);
	char *dirname = g_dirname (uri->path);
	struct stat sb;
	int handle;
	
	*retval = NULL;
	dir = retrieve_dir (uri, *dirname ? dirname : "/", flags & FTPFS_DO_RESOLVE_SYMLINK);
	g_free (dirname);
	if (dir == NULL)
		return GNOME_VFS_ERROR_IO;

	for (l = dir->file_list; l; l = l->next){
		mode_t fmode;

		fe = l->data;
		if (strcmp (filename, fe->name))
			continue;

		if (S_ISLNK (fe->s.st_mode) && (flags & FTPFS_DO_RESOLVE_SYMLINK)) {
			if (fe->l_stat == NULL)
				return GNOME_VFS_ERROR_NOTFOUND;

			if (S_ISLNK (fe->l_stat->st_mode))
				return GNOME_VFS_ERROR_LOOP;
		}

		if (!(flags & FTPFS_DO_OPEN))
			continue;
		
		fmode = S_ISLNK (fe->s.st_mode)
			? fe->l_stat->st_mode
			: fe->s.st_mode;
		
		if (S_ISDIR (fmode))
			return GNOME_VFS_ERROR_ISDIRECTORY;
		
		if (!S_ISREG (fmode))
			return GNOME_VFS_ERROR_ACCESSDENIED;

		if ((flags & FTPFS_DO_CREAT) && exclusive)
			return GNOME_VFS_ERROR_FILEEXISTS;
		
		if (fe->remote_filename == NULL){
			fe->remote_filename = g_strdup (filename);
			if (fe->remote_filename == NULL)
				return GNOME_VFS_ERROR_NOMEM;
		}
		if (fe->local_filename == NULL || !fe->local_stat.st_mtime || 
		    stat (fe->local_filename, &sb) < 0 || 
		    sb.st_mtime != fe->local_stat.st_mtime) {
			if (fe->local_filename) {
				g_free (fe->local_filename);
				fe->local_filename = NULL;
			}

			if (flags & FTPFS_DO_TRUNC) {
				fe->local_filename = tempnam (NULL, "ftpfs");
				if (fe->local_filename == NULL)
					return GNOME_VFS_ERROR_NOMEM;
				handle = open (fe->local_filename,
					       O_CREAT | O_TRUNC | O_RDWR | O_EXCL,
					       0600);

				if (handle < 0)
					return GNOME_VFS_ERROR_IO;
				close (handle);

				if (stat (fe->local_filename, &fe->local_stat) < 0)
					fe->local_stat.st_mtime = 0;
				} else {
					GnomeVFSResult v;
					
					if (IS_LINEAR (flags)) {
						fe->local_is_temp = 0;
						fe->local_filename = NULL;
						fe->linear_state = LS_LINEAR_CLOSED;
						*retval = fe;
						return GNOME_VFS_OK;
					}
					v = retrieve_file (fe);
					if (v != GNOME_VFS_OK)
						return v;
				}
			} else if (flags & FTPFS_DO_TRUNC) 
				truncate (fe->local_filename, 0);

		*retval = fe;
		return GNOME_VFS_OK;
	}

	/*
	 * Ok, the file does not exist, does the user want to create it?
	 */
	if (!((flags & FTPFS_DO_OPEN) && (flags & FTPFS_DO_CREAT)))
		return GNOME_VFS_ERROR_GENERIC;

	fe = g_new (ftpfs_direntry_t, 1);
	if (fe == NULL)
		return GNOME_VFS_ERROR_NOMEM;
	
	fe->freshly_created = 0;
	fe->ref_count = 1;
	fe->linkname = NULL;
	fe->l_stat = NULL;
	fe->conn = uri->conn;
	fe->name = g_strdup (filename);
	fe->remote_filename = g_strdup (filename);
	fe->local_filename = tempnam (NULL, "ftpfs");

	if (!fe->name || !fe->remote_filename || !fe->local_filename){
		ftpfs_direntry_unref (fe);
		return GNOME_VFS_ERROR_NOMEM;
	}

	handle = open (fe->local_filename,
		       O_CREAT | O_TRUNC | O_RDWR | O_EXCL,
		       0600);

	if (handle == -1) {
		ftpfs_direntry_unref (fe);
		return GNOME_VFS_ERROR_IO;
	}

	fstat (handle, &fe->s);
	close (handle);

	dir->file_list = g_list_prepend (dir->file_list, fe);
	fe->freshly_created = 1;
	*retval = fe;
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
ftpfs_open (GnomeVFSMethodHandle **method_handle,
	    GnomeVFSURI *uri,
	    GnomeVFSOpenMode mode)
{
	ftpfs_file_handle_t *fh;
	ftpfs_direntry_t *fe;
	ftpfs_uri_t *ftpfs_uri;
	GnomeVFSResult ret;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	ftpfs_uri = ftpfs_parse_uri (uri);
	if (!ftpfs_uri)
		return GNOME_VFS_ERROR_WRONGFORMAT;

	ret = get_file_entry (
		ftpfs_uri, FTPFS_DO_OPEN | FTPFS_DO_RESOLVE_SYMLINK,
		mode, FALSE, &fe);
	
	if (ret != GNOME_VFS_OK){
		ftpfs_uri_destroy (ftpfs_uri);
		return ret;
	}

	/* FIXME: When we have a GNOME_OPEN_MODE_LINEAR */
	fe->linear_state = 0;

	fh = g_new (ftpfs_file_handle_t, 1);
	fh->fe = fe;
	
	if (!fe->linear_state){
		int flags;

		if (mode & GNOME_VFS_OPEN_WRITE)
			flags = O_RDWR;
		if (mode & GNOME_VFS_OPEN_READ)
			flags = O_RDONLY;
		else
			flags = O_RDONLY;
		
		fh->local_handle = open (fe->local_filename, flags);
		if (fh->local_handle < 0){
			g_free (fh);
			return GNOME_VFS_ERROR_NOMEM;
		}
	} else
		fh->local_handle = -1;

#ifdef UPLOAD_ZERO_LENGTH_FILE        
	fh->has_changed = fe->freshly_created;
#else
	fh->has_changed = 0;
#endif
	ftpfs_connection_ref (fe->conn);

	*method_handle = (GnomeVFSMethodHandle *) fh;
	
	return GNOME_VFS_OK;
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
