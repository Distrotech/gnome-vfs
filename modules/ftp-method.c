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
 * Fixes:
 *   Ian McKellar.
 *
 * (C) 1999 International GNOME Support.
 * (C) 1995, 1996, 1997, 1998, 1999 The Free Software Foundation
 * (C) 1997, 1998, 1999 Norbert Warmuth
 *
 * TODO:
 *   on connection unref, we need to move the connection to a
 *   timeout pool, and kill the connection after some time.
 *
 *   Make the global variables use a lock before we access them.
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
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>		/* struct hostent */
#include <sys/socket.h>		/* AF_INET */
#include <netinet/in.h>		/* struct in_addr */
#ifdef HAVE_SETSOCKOPT
#    include <netinet/ip.h>	/* IP options */
#endif
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <arpa/telnet.h>

#include <gnome.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"
#include "gnome-vfs-mime.h"
#include "gnome-vfs-module.h"
#include "gnome-vfs-module-shared.h"

#include "ftp-method.h"


#define DEFAULT_PORT 21
#define IS_LINEAR(mode) (! ((mode) & GNOME_VFS_OPEN_RANDOM))

#ifdef G_THREADS_ENABLED
#define MUTEX_LOCK(a)	if ((a) != NULL) g_mutex_lock (a)
#define MUTEX_UNLOCK(a)	if ((a) != NULL) g_mutex_unlock (a)
#else
#define MUTEX_LOCK(a)
#define MUTEX_UNLOCK(a)
#endif

/* #define logfile stdout	*/	/* FIXME tmp */
#define logfile NULL

/* Seconds until directory contents are considered invalid */
int   ftpfs_directory_timeout = 600;
int   ftpfs_first_cd_then_ls  = 1;
int   ftpfs_retry_seconds     = 0;
char *ftpfs_anonymous_passwd  = "nothing@";

static GHashTable *connections_hash = NULL;
G_LOCK_DEFINE_STATIC (connections_hash);

/* FIXME ugly kludgy smelly globals.  Should be removed.  */
/* static int code; FIXME this was evil, I hope I removed it correctly */
/*  static int got_sigpipe; */
/*  static char reply_str [80]; */

static void            ftpfs_dir_unref (ftpfs_dir_t *dir);
static GnomeVFSResult  login_server    (ftpfs_connection_t *conn);
static GnomeVFSResult  get_file_entry  (ftpfs_uri_t *uri, int flags,
					GnomeVFSOpenMode mode, gboolean exclusive,
					ftpfs_direntry_t **retval);
static char           *ftpfs_get_current_directory (ftpfs_connection_t *conn);

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

static gboolean
remove_entry (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	ftpfs_dir_unref (value);
	return TRUE;
}

static void
ftpfs_connection_unref (ftpfs_connection_t *conn)
{
	MUTEX_LOCK (conn->access_mutex);

	conn->ref_count--;
	if (conn->ref_count != 0) {
		MUTEX_UNLOCK (conn->access_mutex);
		return;
	}

	MUTEX_UNLOCK (conn->access_mutex);

#ifdef G_THREADS_ENABLED
	if (conn->access_mutex != NULL)
		g_mutex_free (conn->access_mutex);
#endif

	g_free (conn->hostname);
	g_free (conn->username);
	g_free (conn->password);
	g_free (conn->current_directory);
	g_free (conn->home);
	
	g_hash_table_foreach_remove (conn->dcache, remove_entry, NULL);
	g_hash_table_destroy (conn->dcache);

	G_LOCK (connections_hash);
	g_hash_table_remove (connections_hash, conn);
	G_UNLOCK (connections_hash);

	g_free (conn);
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

static void
ftpfs_direntry_ref (ftpfs_direntry_t *fe)
{
	fe->ref_count++;
}

static ftpfs_direntry_t *
ftpfs_direntry_new ()
{
	ftpfs_direntry_t *fe;

	fe = g_new0 (ftpfs_direntry_t, 1);

	return fe;
}

static void
ftpfs_dir_unref (ftpfs_dir_t *dir)
{
	GList *l;

	dir->ref_count--;
	if (dir->ref_count != 0)
		return;
	
	for (l = dir->file_list; l; l = l->next){
		ftpfs_direntry_t *fe = l->data;

		ftpfs_direntry_unref (fe);
	}
	g_free (dir->remote_path);
	g_free (dir);
}

static void
ftpfs_dir_ref (ftpfs_dir_t *dir)
{
	g_return_if_fail (dir != NULL);
	g_return_if_fail (dir->ref_count > 0);
	
	dir->ref_count++;
}

static ftpfs_dir_t *
ftpfs_dir_new (const char *remote_path)
{
	ftpfs_dir_t *dir;

	dir = g_new (ftpfs_dir_t, 1);

	dir->timeout = time (NULL) + ftpfs_directory_timeout;
	dir->file_list = NULL;
	dir->remote_path = g_strdup (remote_path);
	dir->ref_count = 1;
	dir->symlink_status = FTPFS_NO_SYMLINKS;

	return dir;
}

static void
ftpfs_dir_insert (ftpfs_dir_t *dir, ftpfs_direntry_t *fe)
{
	dir->file_list = g_list_prepend (dir->file_list, fe);
	ftpfs_direntry_ref (fe);
}

static guint
hash_conn (gconstpointer key)
{
	const ftpfs_connection_t *conn = key;
	guint value;

	value = 0;

	if (conn->hostname != NULL)
		value += g_str_hash (conn->hostname);
	if (conn->username != NULL)
		value += g_str_hash (conn->username);

	return value;
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

	if (ac->username && bc->username){
		if (strcmp (ac->username, bc->username))
			return 0;
	} else
		if (ac->username != bc->username)
			return 0;

	if (ac->password && bc->password){
		if (strcmp (ac->password, bc->password))
			return 0;
	} else
		if (ac->password != bc->password)
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
	ftpfs_connection_t *retval;

	G_LOCK (connections_hash);

	if (connections_hash == NULL){
		init_connections_hash ();
		G_UNLOCK (connections_hash);
		return NULL;
	}
	
	key.hostname = host;
	key.username = user;
	key.password = pass;
	key.port = (port == 0 ? DEFAULT_PORT : port);

	retval = g_hash_table_lookup (connections_hash, &key);

	G_UNLOCK (connections_hash);

	return retval;
}

static GnomeVFSResult
ftpfs_open_socket (ftpfs_connection_t *conn)
{
	struct   sockaddr_in server_address;
	struct   hostent *hp;
	int      my_socket;
	char     *host = NULL;
	int      port = 0;
	int      free_host = 0;

	/* Use a proxy host? */
	host = conn->hostname;
	
	if (!host || !*host){
		print_vfs_message (_("ftpfs: Invalid host name."));
		return GNOME_VFS_ERROR_INVALIDHOSTNAME;
	}
	
	/* Hosts to connect to that start with a ! should use proxy */
	if (conn->use_proxy){
		/* FIXME: Missing code here. */
#if 0
		ftpfs_get_proxy_host_and_port (ftpfs_proxy_host, &host, &port);
#endif
		free_host = 1;
	} else
		port = conn->port;
	
	/* Get host address */
	memset (&server_address, 0, sizeof (server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = inet_addr (host);
	if (server_address.sin_addr.s_addr != -1)
		server_address.sin_family = AF_INET;
	else {
		hp = gethostbyname (host);
		if (hp == NULL){
			print_vfs_message (_("ftpfs: Invalid host address."));
			if (free_host)
				g_free (host);
			return gnome_vfs_result_from_h_errno ();
		}
		server_address.sin_family = hp->h_addrtype;
		
		/* We copy only 4 bytes, we can not trust hp->h_length, as it comes from the DNS */
		memcpy (&server_address.sin_addr, hp->h_addr, 4);
	}
	
	server_address.sin_port = htons (port);
	
	/* Connect */
	if ((my_socket = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		if (free_host)
			g_free (host);
		return gnome_vfs_result_from_errno ();
	}
	
	print_vfs_message (_("ftpfs: making connection to %s"), host);
	if (free_host)
		g_free (host);
	
	if (connect (my_socket, (struct sockaddr *) &server_address, sizeof (server_address)) < 0){
		if (errno == EINTR)
			print_vfs_message (_("ftpfs: connection interrupted by user"));
		else
			print_vfs_message (_("ftpfs: connection to server failed: %s"),
					   g_strerror (errno));
		close (my_socket);
		return gnome_vfs_result_from_errno ();
	}

	conn->sock = my_socket;
	return GNOME_VFS_OK;
}

/* some defines only used by changetype */
/* These two are valid values for the second parameter */
#define TYPE_ASCII    0
#define TYPE_BINARY   1

/* This one is only used to initialize bucket->isbinary, don't use it as
   second parameter to changetype. */
#define TYPE_UNKNOWN -1 

static void sig_pipe (int unused)
{
	/* Do nothing.  */
}

static void
net_init (void)
{
	static int inited;
	struct sigaction sa;

	if (inited)
		return;
	inited = 1;
	
	sa.sa_handler = sig_pipe;
	sa.sa_flags = 0;
	sigemptyset (&sa.sa_mask);
	sigaction (SIGPIPE, &sa, NULL);
}

static GnomeVFSResult
ftpfs_connection_connect (ftpfs_connection_t *conn)
{
	GnomeVFSResult result;
	int retry_seconds = 0;

	if (conn->sock != -1)
		return GNOME_VFS_OK;

	net_init ();
	do {
		conn->failed_on_login = 0;

		result = ftpfs_open_socket (conn);
		if (result != GNOME_VFS_OK)
			return result;
		
		result = login_server (conn);

		if (result == GNOME_VFS_OK) {
			/* Logged in, no need to retry the connection */
			break;
		} else {
			if (conn->failed_on_login){
				/* Close only the socket descriptor */
				close (conn->sock);
				conn->sock = -1;
			} else {
				return result;
			}

			if (ftpfs_retry_seconds) {
				int count_down;
				
				retry_seconds = ftpfs_retry_seconds;
				for (count_down = retry_seconds; count_down; count_down--){
					print_vfs_message (_("Waiting to retry... %d (Control-C to cancel)"), count_down);
					sleep (1);
				}
			}
		}
	} while (retry_seconds);

	if (conn->sock == -1)
		return GNOME_VFS_ERROR_LOGINFAILED;

	conn->home = ftpfs_get_current_directory (conn);
	if (!conn->home)
		conn->home = g_strdup ("/");

	return GNOME_VFS_OK;
}

static void
ftpfs_connection_dir_flush (ftpfs_connection_t *conn)
{
	g_hash_table_foreach_remove (conn->dcache, remove_entry, NULL);
}

static ftpfs_connection_t *
ftpfs_connection_new (const gchar *hostname,
		      const gchar *username,
		      const gchar *password,
		      guint port,
		      GnomeVFSResult *result_return)
{
	ftpfs_connection_t *conn;
			    
	conn = g_new0 (ftpfs_connection_t, 1);

#ifdef G_THREADS_ENABLED
	if (g_thread_supported ())
		conn->access_mutex = g_mutex_new ();
	else
		conn->access_mutex = NULL;
#endif
	
	conn->ref_count = 1;

	conn->hostname = g_strdup (hostname);
	conn->username = g_strdup (username);
	conn->password = g_strdup (password);
	conn->port = (port == 0 ? DEFAULT_PORT : port);
	conn->use_passive_connection = TRUE;

	conn->is_binary = TYPE_UNKNOWN;
	conn->sock = -1;
	conn->remote_is_amiga = 0;
	conn->strict_rfc959_list_cmd = 0;
	conn->dcache = g_hash_table_new (g_str_hash, g_str_equal);

	*result_return = ftpfs_connection_connect (conn);

	G_LOCK (connections_hash);

	if (connections_hash == NULL)
		init_connections_hash ();

	if (*result_return == GNOME_VFS_OK) {
	        g_hash_table_insert (connections_hash, conn, conn);
		G_UNLOCK (connections_hash);
		return conn;
	} else {
		ftpfs_connection_unref (conn);
		G_UNLOCK (connections_hash);
		return NULL;
	}
}

static void
ftpfs_uri_destroy (ftpfs_uri_t *uri)
{
	if (uri->conn){
		/*
		 * FIX THIS, we need to add some sort of timmeout to
		 * it, prolly a cache can keep a ref?
		 */
		/* ftpfs_connection_unref (uri->conn); */
	}
	g_free (uri->path);
	g_free (uri);
}

static ftpfs_uri_t *
ftpfs_uri_new (GnomeVFSURI *uri,
	       GnomeVFSResult *result_return)
{
	GnomeVFSToplevelURI *toplevel;
	ftpfs_connection_t *conn;
	ftpfs_uri_t *ftpfs_uri;
	int len;
	
	ftpfs_uri = g_new (ftpfs_uri_t, 1);
	ftpfs_uri->path = g_strdup (uri->text);
	len = strlen(ftpfs_uri->path);
	if(ftpfs_uri->path[len - 1] == '/') /* Strip trailing /'s */
	  ftpfs_uri->path[len - 1] = '\0';
	toplevel = (GnomeVFSToplevelURI *) uri;
	
	conn = lookup_conn (toplevel->host_name,
			    toplevel->user_name,
			    toplevel->password,
			    toplevel->host_port);

	if (conn != NULL) {
		ftpfs_uri->conn = ftpfs_connection_ref (conn);
		*result_return = GNOME_VFS_OK;
		return ftpfs_uri;
	}

	ftpfs_uri->conn = ftpfs_connection_new (toplevel->host_name,
						toplevel->user_name,
						toplevel->password,
						toplevel->host_port,
						result_return);
	if (ftpfs_uri->conn == NULL){
		ftpfs_uri_destroy (ftpfs_uri);
		return NULL;
	}
	
	return ftpfs_uri;
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


/* Returns a reply code, divide it by 100 and check /usr/include/arpa/ftp.h for
   possible values */
static int
get_reply (int sock, char *string_buf, int string_len)
{
	char answer [1024];
	int i;
	gint code;
	
	for (;;) {
		if (!get_line (sock, answer, sizeof (answer), '\n')){
			if (string_buf)
				*string_buf = 0;
			return 421;
		}
		switch (sscanf(answer, "%d", &code)){
		case 0:
			if (string_buf) {
				strncpy (string_buf, answer, string_len - 1);
				*(string_buf + string_len - 1) = 0;
			}
			return 500;
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
			return code;
		}
	}
}

static int
command (ftpfs_connection_t *conn, int wait_reply,
	 char *reply_string, int reply_string_len,
	 char *fmt, ...)
{
	GnomeVFSResult result;
	va_list ap;
	char *str, *fmt_str;
	int status;
	int sock = conn->sock;
	
	va_start (ap, fmt);
	
	fmt_str = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	
	str = g_strconcat (fmt_str, "\r\n", NULL);
	g_free (fmt_str);

	/* FIXME FIXME FIXME Use the return value.  */
	result = ftpfs_connection_connect (conn);

	if (logfile){
		if (strncmp (str, "PASS ", 5) == 0){
			char *tmp = "PASS <Password not logged>\r\n";
			
			fwrite (tmp, strlen (tmp), 1, logfile);
		} else {
			fwrite (str, strlen (str), 1, logfile);
		}

		fflush (logfile);
	}

	status = write (sock, str, strlen (str));
	g_free (str);
    
	if (status < 0)
		return 421;

	if (wait_reply)
		return get_reply (sock, reply_string, reply_string_len);

	return COMPLETE * 100;
}

static GnomeVFSResult
login_server (ftpfs_connection_t *conn)
{
	char *pass;
	char *op;
	char *name;			/* login user name */
	int  anon = 0;
	char reply_string[255];
	gint code;
	
	conn->is_binary = TYPE_UNKNOWN;
	if (conn->username == NULL
	    || strcmp (conn->username, "anonymous") == 0
	    || strcmp (conn->username, "ftp") == 0) {
		op = g_strdup (ftpfs_anonymous_passwd);
		anon = 1;
	} else {
		char *p;

		/* Non-anonymous login.  */

		if (!conn->password){
			p = g_strconcat (_(" FTP: Password required for "),
					 conn->username, " ", NULL);
			op = "FIXME";
			g_free (p);
			if (op == NULL)
				return GNOME_VFS_ERROR_LOGINFAILED;
			conn->password = g_strdup (op);
		} else {
			op = g_strdup (conn->password);
		}
	}
	
	if (!anon || logfile)
		pass = g_strdup (op);
	else
		pass = g_strconcat ("-", op, NULL);
	
	
	/* Proxy server accepts: username@host-we-want-to-connect*/
	if (conn->use_proxy){
		name = g_strconcat (conn->username, "@", 
				    (conn->hostname [0] == '!'
				     ? conn->hostname+1 : conn->hostname),
				    NULL);
	} else if (conn->username == NULL) {
		name = g_strdup ("anonymous");
	} else {
		name = g_strdup (conn->username);
	}

	code = get_reply (conn->sock, reply_string, sizeof (reply_string) - 1);

	if (code / 100 == COMPLETE) {
		g_strup (reply_string);
		conn->remote_is_amiga = strstr (reply_string, "AMIGA") != 0;
		if (logfile) {
			fprintf (logfile, "MC -- remote_is_amiga =  %d\n", conn->remote_is_amiga);
			fflush (logfile);
		}
		print_vfs_message (_("ftpfs: sending login name"));

		code = command (conn, TRUE, NULL, 0, "USER %s", name);

		switch (code / 100){
		case CONTINUE:
			print_vfs_message (_("ftpfs: sending user password"));
			if (command (conn, TRUE, NULL, 0, "PASS %s", pass) / 100
			    != COMPLETE)
				break;
			
		case COMPLETE:
			print_vfs_message (_("ftpfs: logged in"));
			g_free (name);
			return GNOME_VFS_OK;

		default:
			conn->failed_on_login = 1;
			conn->password = 0;
	    
			goto login_fail;
		}
	}

	print_vfs_message (_("ftpfs: Login incorrect for user %s "), conn->username);
 login_fail:
	g_free (name);
	return GNOME_VFS_ERROR_LOGINFAILED;
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
    
	if (command (conn, FALSE, NULL, 0, "%cABOR", DM) / 100 != COMPLETE){
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

	if (get_reply (conn->sock, NULL, 0) == 426)
		get_reply (conn->sock, NULL, 0);
}

/* Setup Passive ftp connection, we use it for source routed connections */
static int
setup_passive (int my_socket, ftpfs_connection_t *conn, struct sockaddr_in *sa)
{
	int xa, xb, xc, xd, xe, xf;
	char n [6];
	char reply_string[256];
	char *c;
	
	if (command (conn, TRUE, reply_string, sizeof (reply_string) - 1,
		     "PASV") / 100 != COMPLETE)
		return 0;
	
	/* Parse remote parameters */
	for (c = reply_string + 4; (*c) && (!isdigit (*c)); c++)
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
		
		if (command (conn, TRUE, NULL, 0, "PORT %d,%d,%d,%d,%d,%d",
			     a[0], a[1], a[2], a[3], p[0], p[1]) / 100
		    != COMPLETE)
			goto error_return;
	}
	return data;

 error_return:
	close(data);
	return -1;
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
translate_path (ftpfs_connection_t *conn, const char *remote_path)
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
changetype (ftpfs_connection_t *conn, int binary)
{
	if (binary != conn->is_binary) {
		if (command (conn, TRUE, NULL, 0, "TYPE %c", binary ? 'I' : 'A')
		    / 100 != COMPLETE)
			return -1;
		conn->is_binary = binary;
	}
	return binary;
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
		j = command (conn, TRUE, NULL, 0, "REST %d", reget) / 100;
		if (j != CONTINUE)
			return -1;
	}
	if (remote){
		j = command (conn, TRUE, NULL, 0, "%s %s", cmd, 
			     translate_path (conn, remote)) / 100;
	} else
		j = command (conn, TRUE, NULL, 0, "%s", cmd) / 100;
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
store_file (ftpfs_direntry_t *fe)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	int local_handle, sock, n, total;
#ifdef HAVE_STRUCT_LINGER
	struct linger li;
#else
	int flag_one = 1;
#endif
	char buffer [8192];
	struct stat s;
	
	local_handle = open (fe->local_filename, O_RDONLY);
	unlink (fe->local_filename);
	if (local_handle == -1)
		return GNOME_VFS_ERROR_IO;
		
	fstat (local_handle, &s);
	sock = open_data_connection (fe->conn, "STOR", fe->remote_filename, TYPE_BINARY, 0);
	if (sock < 0) {
		close(local_handle);
		return 0;
	}
#ifdef HAVE_STRUCT_LINGER
	li.l_onoff = 1;
	li.l_linger = 120;
	setsockopt (sock, SOL_SOCKET, SO_LINGER, (char *) &li, sizeof (li));
#else
	setsockopt(sock, SOL_SOCKET, SO_LINGER, &flag_one, sizeof (flag_one));
#endif
	total = 0;
	
	while (1) {
		while ((n = read (local_handle, buffer, sizeof (buffer))) < 0) {
			result = gnome_vfs_result_from_errno ();
			goto error_return;
		}
		if (n == 0)
			break;
		while (write (sock, buffer, n) < 0) {
			result = gnome_vfs_result_from_errno ();
			goto error_return;
		}
		total += n;
		print_vfs_message(_("ftpfs: storing file %d (%d)"), 
				  total, s.st_size);
	}
	close (sock);
	close (local_handle);

	if (get_reply (fe->conn->sock, NULL, 0) / 100 != COMPLETE)
		result = GNOME_VFS_ERROR_IO;

	return result;

 error_return:
	close (sock);
	close (local_handle);
	get_reply (fe->conn->sock, NULL, 0);
	return result;
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
linear_read (ftpfs_direntry_t *fe, void *buf, int len, GnomeVFSFileSize *bytes_read)
{
	int n;
	GnomeVFSResult error = GNOME_VFS_OK;

	*bytes_read = 0;
	while ((n = read (fe->data_sock, buf, len)) < 0) {
		if (errno == EINTR)
			continue;
		break;
	}

	if (n < 0) {
		linear_abort (fe);
		return GNOME_VFS_ERROR_IO;
	}
	
	if (!n) {
		close (fe->data_sock);
		fe->data_sock = -1;

		if ((get_reply (fe->conn->sock, NULL, 0) / 100 != COMPLETE))
			error = GNOME_VFS_ERROR_IO;
	}

	if (n > 0)
		*bytes_read = n;

	return error;
}

static void
linear_close (ftpfs_direntry_t *fe)
{
	if (fe->data_sock != -1)
		linear_abort (fe);
}

enum {
	OPT_FLUSH = 1,
	OPT_IGNORE_ERROR = 2
};

static GnomeVFSResult
send_ftp_command (GnomeVFSURI *uri, char *cmd, int flags)
{
	char res [120];
	GnomeVFSResult ret;
	ftpfs_uri_t *ftpfs_uri;
/*	int flush_directory_cache = (flags & OPT_FLUSH); */
	int r;
	
	ftpfs_uri = ftpfs_uri_new (uri, &ret);
	if (!ftpfs_uri)
		return GNOME_VFS_ERROR_INVALIDURI;
	
	r = command (ftpfs_uri->conn, TRUE, res, sizeof (res)-1, cmd, ftpfs_uri->path);
	if (flags & OPT_IGNORE_ERROR)
		r = COMPLETE;
	if (r != COMPLETE){
		ftpfs_uri_destroy (ftpfs_uri);
		return GNOME_VFS_ERROR_ACCESSDENIED;
	}
	return GNOME_VFS_OK;
}

static int
is_same_dir (const char *path, const ftpfs_connection_t *conn)
{
	if (!conn->current_directory);
		return 0;
	if (strcmp (path, conn->current_directory) == 0)
		return 1;
	return 0;
}

static int
ftpfs_chdir_internal (ftpfs_connection_t *conn, const char *remote_path)
{
	int r;
	char *p;

	/* if remote_path == "", assume remote_path == "/". This is certainly
	 * Netscape's interpretation. */
	if(*remote_path == '\0') remote_path = "/";
	
	if (!conn->cwd_defered && is_same_dir (remote_path, conn))
		return COMPLETE;
	
	p = translate_path (conn, remote_path);
	r = command (conn, 1, NULL, 0, "CWD %s", p) / 100;
	g_free (p);
	
	if (r == COMPLETE){
		if (conn->current_directory)
			g_free (conn->current_directory);
		conn->current_directory = g_strdup (remote_path);
		conn->cwd_defered = 0;
	}
	return r;
}

static void
resolve_symlink_without_ls_options (ftpfs_connection_t *conn, ftpfs_dir_t *dir)
{
	ftpfs_direntry_t *fe, *fel;
	GList *l;
	char tmp [300];
	
	dir->symlink_status = FTPFS_RESOLVING_SYMLINKS;
	
	for (l = dir->file_list; l != NULL; l = l->next) {
		fel = l->data;

		if (!S_ISLNK (fel->s.st_mode))
			continue;
		
		if (fel->linkname[0] == '/') {
			if (strlen (fel->linkname) >= sizeof (tmp)-1)
				continue;
			strcpy (tmp, fel->linkname);
		} else {
			if ((strlen (dir->remote_path) + strlen (fel->linkname)) >= sizeof (tmp)-1)
				continue;
			strcpy (tmp, dir->remote_path);
			if (tmp[1] != '\0')
				strcat (tmp, "/");
			strcat (tmp + 1, fel->linkname);
		}
		for ( ;; ) {
			ftpfs_uri_t uri;

			gnome_vfs_canonicalize_pathname (tmp);
			uri.path = tmp;
			uri.conn = conn;
			if (get_file_entry (&uri, 0, 0, 0, &fe) != GNOME_VFS_OK)
				break;
			
			if (S_ISLNK (fe->s.st_mode) && fe->l_stat == 0) {
				/* Symlink points to link which isn't resolved, yet. */
				if (fe->linkname[0] == '/') {
					if (strlen (fe->linkname) >= sizeof (tmp)-1)
						break;
					strcpy (tmp, fe->linkname);
				} else {
					/* at this point tmp looks always like this
					   /directory/filename, i.e. no need to check
					   strrchr's return value */
					*(strrchr (tmp, '/') + 1) = '\0'; /* dirname */
					if ((strlen (tmp) + strlen (fe->linkname)) >= sizeof (tmp)-1)
						break;
					strcat (tmp, fe->linkname);
				}
				continue;
			} else {
				fel->l_stat = g_new (struct stat, 1);
				if (S_ISLNK (fe->s.st_mode))
					*fel->l_stat = *fe->l_stat;
				else
					*fel->l_stat = fe->s;
				(*fel->l_stat).st_ino = conn->__inode_counter++;
			}
			break;
		}
	}
	dir->symlink_status = FTPFS_RESOLVED_SYMLINKS;
}

static void
resolve_symlink_with_ls_options(ftpfs_connection_t *conn, ftpfs_dir_t *dir)
{
	char  buffer[2048] = "", *filename;
	int sock;
	FILE *fp;
	struct stat s;
	GList *l;
	ftpfs_direntry_t *fe;
	int switch_method = 0;
	
	dir->symlink_status = FTPFS_RESOLVED_SYMLINKS;
	if (strchr (dir->remote_path, ' ')) {
		if (ftpfs_chdir_internal (conn, dir->remote_path) != COMPLETE) {
			print_vfs_message(_("ftpfs: CWD failed."));
			return;
		}
		sock = open_data_connection (conn, "LIST -lLa", ".", TYPE_ASCII, 0);
	}
	else
		sock = open_data_connection (conn, "LIST -lLa", 
					     dir->remote_path, TYPE_ASCII, 0);
	
	if (sock == -1) {
		print_vfs_message(_("ftpfs: couldn't resolve symlink"));
		return;
	}
	
	fp = fdopen(sock, "r");
	if (fp == NULL) {
		close(sock);
		print_vfs_message(_("ftpfs: couldn't resolve symlink"));
		return;
	}
	
	for (l = dir->file_list; l; l = l->next) {
		fe = l->data;

		if (!S_ISLNK (fe->s.st_mode))
			continue;
		
		while (1) {
			if (fgets (buffer, sizeof (buffer), fp) == NULL)
				goto done;
			if (logfile){
				fputs (buffer, logfile);
				fflush (logfile);
			}
			if (gnome_vfs_parse_ls_lga (buffer, &s, &filename, NULL)) {
				int r = strcmp(fe->name, filename);
				g_free(filename);
				if (r == 0) {
					if (S_ISLNK (s.st_mode)) {
						/* This server doesn't understand LIST -lLa */
						switch_method = 1;
						goto done;
					}
					fe->l_stat = g_new (struct stat, 1);
					if (fe->l_stat == NULL)
						goto done;
					*fe->l_stat = s;
					(*fe->l_stat).st_ino = conn->__inode_counter++;
					break;
				}
				if (r < 0)
					break;
			}
		}
	}
 done:
	while (fgets(buffer, sizeof(buffer), fp) != NULL)
		;
	fclose(fp);
	get_reply (conn->sock, NULL, 0);
	if (switch_method) {
		conn->strict_rfc959_list_cmd = 1;
		resolve_symlink_without_ls_options (conn, dir);
	}
}

static void
resolve_symlink(ftpfs_connection_t *conn, ftpfs_dir_t *dir)
{
    print_vfs_message(_("Resolving symlink..."));

    if (conn->strict_rfc959_list_cmd) 
	resolve_symlink_without_ls_options (conn, dir);
    else
        resolve_symlink_with_ls_options (conn, dir);
}

/* Return true if path is the same directoy as the one we are on now */

/*
 * Inserts an entry for "." (and "..") into the linked list. Ignore any 
 * errors because "." isn't important (as fas as you don't try to save a
 * file in the root dir of the ftp server).
 * Actually the dot is needed when stating the root directory, e.g.
 * mc_stat ("/ftp#localhost", &buf). Down the call tree _get_file_entry
 * gets called with filename = "/" which will be transformed into "."
 * before searching for a fileentry. Whithout "." in the linked list
 * this search fails.  -- Norbert.
 */
static void
insert_dots (ftpfs_dir_t *dir, ftpfs_connection_t *conn)
{
	ftpfs_direntry_t *fe;
	int i;
	char buffer[][58] = { 
		"drwxrwxrwx   1 0        0            1024 Jan  1  1970 .",
		"drwxrwxrwx   1 0        0            1024 Jan  1  1970 .."
	};
	
	for (i = 0; i < 2; i++ ) {
		fe = ftpfs_direntry_new ();
		if (fe == NULL)
			return;
		if (gnome_vfs_parse_ls_lga (buffer[i], &fe->s, &fe->name, &fe->linkname)) {
			fe->freshly_created = 0;
			fe->ref_count = 1;
			fe->local_filename = fe->remote_filename = NULL;
			fe->l_stat = NULL;
			fe->conn = conn;
			(fe->s).st_ino = conn->__inode_counter++;

			ftpfs_dir_insert (dir, fe);
		} else
			g_free (fe);
	}
}

static char *
ftpfs_get_current_directory (ftpfs_connection_t *conn)
{
	char buf[4096], *bufp, *bufq;
	
	if (!(command (conn, FALSE, NULL, 0, "PWD") / 100 == COMPLETE &&
	      get_reply(conn->sock, buf, sizeof(buf)) / 100 == COMPLETE))
		return NULL;
	
	bufp = NULL;
	for (bufq = buf; *bufq; bufq++){
		if (*bufq != '"')
			continue;
		
		if (!bufp) {
			bufp = bufq + 1;
			continue;
		}
		
		*bufq = 0;
		if (!*bufp)
			return NULL;
		
		if (*(bufq - 1) != '/') {
			*bufq++ = '/';
			*bufq = 0;
		}
		if (*bufp == '/')
			return g_strdup (bufp);
		else {
			/*
			 * If the remote server is an Amiga a leading slash
			 * might be missing. MC needs it because it is used
			 * as seperator between hostname and path internally.
			 */
			return g_strconcat( "/", bufp, 0);
		}
	}
	return NULL;
}

static ftpfs_dir_t *
retrieve_dir (ftpfs_connection_t *conn, char *remote_path, gboolean resolve_symlinks)
{
	int sock, has_symlinks;
	ftpfs_direntry_t *fe;
	ftpfs_dir_t *dcache;
	char buffer[8192];
	int got_intr = 0;
	int dot_found = 0;
	int has_spaces = (strchr (remote_path, ' ') != NULL);

	dcache = g_hash_table_lookup (conn->dcache, remote_path);
	if (dcache){
		time_t now = time (NULL);
		
		if (now < dcache->timeout ||
		    dcache->symlink_status == FTPFS_RESOLVING_SYMLINKS) {
			if (resolve_symlinks && dcache->symlink_status == FTPFS_UNRESOLVED_SYMLINKS)
				resolve_symlink (conn, dcache);
			return dcache;
		} else {
			gpointer key;

			g_hash_table_lookup_extended (conn->dcache, remote_path, &key, NULL);
			g_hash_table_remove (conn->dcache, remote_path);
			g_free (key);
			
			ftpfs_dir_unref (dcache);
		}
	}

	
	has_symlinks = 0;
	if (conn->strict_rfc959_list_cmd)
		print_vfs_message(
			_("ftpfs: Reading FTP directory %s... (don't use UNIX ls options)"),
			remote_path);
	else
		print_vfs_message(
			_("ftpfs: Reading FTP directory %s..."),
			remote_path);

	if (has_spaces || conn->strict_rfc959_list_cmd || ftpfs_first_cd_then_ls) {
		if (ftpfs_chdir_internal (conn, remote_path) != COMPLETE) {
			print_vfs_message(_("ftpfs: CWD failed."));
			return NULL;
		}
	}

	dcache = ftpfs_dir_new (remote_path);

	if (conn->strict_rfc959_list_cmd == 1) 
		sock = open_data_connection (conn, "LIST", 0, TYPE_ASCII, 0);
	else if (has_spaces || ftpfs_first_cd_then_ls)
		sock = open_data_connection (conn, "LIST -la", ".", TYPE_ASCII, 0);
	else {
		/*
		 * Trailing "/." is necessary if remote_path is a symlink
		 * but don't generate "//."
		 */
		char *path = g_strconcat (remote_path, 
					  remote_path[1] == '\0' ? "" : "/", 
					  ".", (char *) 0);

		sock = open_data_connection (conn, "LIST -la", path, TYPE_ASCII, 0);
		g_free (path);
	}

	if (sock == -1)
		goto fallback;

	while ((got_intr = get_line (sock, buffer, sizeof (buffer), '\n')) != EINTR){
		int eof = got_intr == 0;

		if (logfile) {
			fputs (buffer, logfile);
			fputs ("\n", logfile);
			fflush (logfile);
		}

		if (buffer [0] == 0 && eof)
			break;
		
		fe = ftpfs_direntry_new ();

		if (gnome_vfs_parse_ls_lga (buffer, &fe->s, &fe->name, &fe->linkname)) {
			if (strcmp (fe->name, ".") == 0)
				dot_found = 1;
			fe->ref_count = 1;
			fe->local_filename = fe->remote_filename = NULL;
			fe->l_stat = NULL;
			fe->conn = conn;
			(fe->s).st_ino = conn->__inode_counter++;
			if (S_ISLNK (fe->s.st_mode))
				has_symlinks = 1;

			ftpfs_dir_insert (dcache, fe);
		} else
			g_free(fe);

		if (eof)
			break;
	}
	
	if (got_intr){
		print_vfs_message(_("ftpfs: reading FTP directory interrupt by user"));
		abort_transfer (conn, sock);
		close (sock);
		goto error_3;
	}
	close (sock);

	if ((get_reply (conn->sock, NULL, 0) / 100 != COMPLETE)
	    || dcache->file_list == NULL)
		goto fallback;

ok:
	if (!dot_found)
		insert_dots (dcache, conn);

	g_hash_table_insert (conn->dcache, g_strdup (remote_path), dcache);

	
	if (has_symlinks) {
		if (resolve_symlinks)
			resolve_symlink (conn, dcache);
		else
			dcache->symlink_status = FTPFS_UNRESOLVED_SYMLINKS;
	}
	print_vfs_message(_("ftpfs: got listing"));
	return dcache;
	close (sock);
	get_reply (conn->sock, NULL, 0);

error_3:
	ftpfs_dir_unref (dcache);
	return NULL;

fallback:
        /*
	 * It's our first attempt to get a directory listing from this
	 * server (UNIX style LIST command)
	 */
	if (conn->__inode_counter == 0 && (!conn->strict_rfc959_list_cmd)) {
		conn->strict_rfc959_list_cmd = 1;
		ftpfs_dir_unref (dcache);

		return retrieve_dir (conn, remote_path, resolve_symlinks);
	}

	/*
	 * Ok, maybe the directory exists but the remote server doesn't
	 * list "." and "..". 
	 * Check whether the directory exists:
	 *
	 * CWD has been already performed, i.e. 
	 * we know that remote_path exists
	 */
	if (has_spaces || conn->strict_rfc959_list_cmd || ftpfs_first_cd_then_ls)
		goto ok;
	else {
		if (conn->remote_is_amiga) {
			/*
			 * The Amiga ftp server needs extra processing because it
			 * always gets relative pathes instead of absolute pathes
			 * like anyone else
			 */
			char *p = ftpfs_get_current_directory (conn);
	
			if (ftpfs_chdir_internal (conn, remote_path) == COMPLETE) {
				ftpfs_chdir_internal (conn, p);
				g_free (p);
				goto ok;
			}
		} else {
			if (ftpfs_chdir_internal (conn, remote_path) == COMPLETE)
				goto ok;
		}
	}
	
	ftpfs_dir_unref (dcache);
	print_vfs_message(_("ftpfs: failed; nowhere to fallback to"));
	return NULL;
}

/* If you want reget, you'll have to open file with O_LINEAR */
static GnomeVFSResult
retrieve_file (ftpfs_direntry_t *fe)
{
	GnomeVFSResult ret;
	GnomeVFSFileSize n;
	int total = 0;
	char buffer [8192];
	int local_handle;
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
		GnomeVFSResult s;
		
		s = linear_read (fe, buffer, sizeof (buffer), &n);
		
		if (s != GNOME_VFS_OK){
			ret = GNOME_VFS_ERROR_IO;
			goto error_1;
		}
		if (!n)
			break;
		
		total += n;
		print_vfs_message ("Getting file", fe->remote_filename, total, stat_size);
		
		while (write (local_handle, buffer, n) < 0) {
			if (errno == EINTR) {
			  /* FIXME: #warning Here */
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
	dir = retrieve_dir (uri->conn, *dirname ? dirname : "/", (flags & FTPFS_DO_RESOLVE_SYMLINK) != 0);
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
					
					if (IS_LINEAR (mode)) {
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
		return GNOME_VFS_ERROR_NOTFOUND;

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
ftpfs_open (GnomeVFSMethod *method,
	    GnomeVFSMethodHandle **method_handle,
	    GnomeVFSURI *uri,
	    GnomeVFSOpenMode mode,
	    GnomeVFSContext *context)
{
	ftpfs_file_handle_t *fh;
	ftpfs_direntry_t *fe;
	ftpfs_uri_t *ftpfs_uri;
	GnomeVFSResult ret;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	ftpfs_uri = ftpfs_uri_new (uri, &ret);
	if (!ftpfs_uri)
		return ret;

	/* FIXME: Perhaps we want to set linear *only* iff READ is specified (see write method) */
	
	ret = get_file_entry (
		ftpfs_uri, FTPFS_DO_OPEN | FTPFS_DO_RESOLVE_SYMLINK,
		mode, FALSE, &fe);
	
	if (ret != GNOME_VFS_OK){
		ftpfs_uri_destroy (ftpfs_uri);
		return ret;
	}

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
ftpfs_create (GnomeVFSMethod *method,
	      GnomeVFSMethodHandle **method_handle,
	      GnomeVFSURI *uri,
	      GnomeVFSOpenMode mode,
	      gboolean exclusive,
	      guint perm,
	      GnomeVFSContext *context)
{
	ftpfs_file_handle_t *fh;
	ftpfs_direntry_t *fe;
	ftpfs_uri_t *ftpfs_uri;
	GnomeVFSResult ret;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	ftpfs_uri = ftpfs_uri_new (uri, &ret);
	if (!ftpfs_uri)
		return GNOME_VFS_ERROR_INVALIDURI;

	/* FIXME: Perhaps we want to set linear *only* iff READ is specified (see write method) */
	
	ret = get_file_entry (
		ftpfs_uri, FTPFS_DO_TRUNC | FTPFS_DO_RESOLVE_SYMLINK | FTPFS_DO_CREAT,
		mode, exclusive, &fe);
	
	if (ret != GNOME_VFS_OK){
		ftpfs_uri_destroy (ftpfs_uri);
		return ret;
	}

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
		flags |= O_CREAT;

		flags = O_EXCL;
		
		fh->local_handle = open (fe->local_filename, flags, perm);
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
ftpfs_close (GnomeVFSMethod *method,
	     GnomeVFSMethodHandle *method_handle,
	     GnomeVFSContext *context)
{
	ftpfs_file_handle_t *fp;
	GnomeVFSResult result = GNOME_VFS_OK;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);
	fp = (ftpfs_file_handle_t *) method_handle;
	
	if (fp->has_changed) {
		result = store_file (fp->fe);
		ftpfs_connection_dir_flush (fp->fe->conn);
	}

	if (fp->fe->linear_state == LS_LINEAR_OPEN)
		linear_close (fp->fe);

	if (fp->local_handle >= 0)
		close (fp->local_handle);

	ftpfs_connection_unref (fp->fe->conn);
	ftpfs_direntry_unref (fp->fe);
	g_free(fp);

	return result;
}

static GnomeVFSResult
ftpfs_read (GnomeVFSMethod *method,
	    GnomeVFSMethodHandle *method_handle,
	    gpointer buffer,
	    GnomeVFSFileSize count,
	    GnomeVFSFileSize *bytes_read,
	    GnomeVFSContext *context)
{
	ftpfs_file_handle_t *fp;
	int n;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);
	
	fp = (ftpfs_file_handle_t *) method_handle;
	if (fp->fe->linear_state == LS_LINEAR_CLOSED) {
		GnomeVFSResult r;
		
		print_vfs_message (_("Starting linear transfer..."));
		r = linear_start (fp->fe, 0);
		if (r != GNOME_VFS_OK)
			return r;
	}

	if (fp->fe->linear_state == LS_LINEAR_CLOSED)
		g_error ("linear_start() did not set linear_state!");

	if (fp->fe->linear_state == LS_LINEAR_OPEN)
		return linear_read (fp->fe, buffer, count, bytes_read);
        
	n = read (fp->local_handle, buffer, count);
	if (n < 0){
		*bytes_read = 0;
		return GNOME_VFS_ERROR_IO;
	} else
		*bytes_read = n;
	return GNOME_VFS_OK;
}

static GnomeVFSResult
ftpfs_write (GnomeVFSMethod *method,
	     GnomeVFSMethodHandle *method_handle,
	     gconstpointer buffer,
	     GnomeVFSFileSize num_bytes,
	     GnomeVFSFileSize *bytes_written,
	     GnomeVFSContext *context)
{
	GnomeVFSResult result = GNOME_VFS_OK;
	ftpfs_file_handle_t *fp;
	int n;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);
	
	fp = (ftpfs_file_handle_t *) method_handle;
	*bytes_written = 0;
	
	if (fp->fe->linear_state){
		g_warning ("Write attempted on linear file");
		return GNOME_VFS_ERROR_IO;
	}
	n = write (fp->local_handle, buffer, num_bytes);
	if (n < 0){
		result = gnome_vfs_result_from_errno ();
	} else
		*bytes_written = n;
	fp->has_changed = 1;
	return result;
}

static GnomeVFSResult
ftpfs_seek (GnomeVFSMethod *method,
	    GnomeVFSMethodHandle *method_handle,
	    GnomeVFSSeekPosition whence,
	    GnomeVFSFileOffset offset,
	    GnomeVFSContext *context)
{
	ftpfs_file_handle_t *fp;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);
	fp = (ftpfs_file_handle_t *) method_handle;
	
	
	if (fp->fe->linear_state == LS_LINEAR_OPEN){
		g_warning ("Seek is not possible on non-random access files");
		return GNOME_VFS_ERROR_IO;
	}

	if (fp->fe->linear_state == LS_LINEAR_CLOSED){
		print_vfs_message (_("Preparing reget..."));
		if (whence != SEEK_SET){
			g_warning ("Seek is not possible on non-random access files");
			return GNOME_VFS_ERROR_IO;
		}
		
		return linear_start (fp->fe, offset);
	}

	if (lseek (fp->local_handle, offset, whence) == -1)
		return gnome_vfs_result_from_errno ();
	else
		return GNOME_VFS_OK;
}

static GnomeVFSResult
ftpfs_tell (GnomeVFSMethod *method,
	    GnomeVFSMethodHandle *method_handle,
	    GnomeVFSFileOffset *offset_return)
{
	g_error ("unimplemented routine reached (tell)");
	return GNOME_VFS_ERROR_INTERNAL;
}

static GnomeVFSResult
ftpfs_open_directory (GnomeVFSMethod *method,
		      GnomeVFSMethodHandle **method_handle,
		      GnomeVFSURI *uri,
		      GnomeVFSFileInfoOptions options,
		      const GList *meta_keys,
		      const GnomeVFSDirectoryFilter *filter,
		      GnomeVFSContext *context)
{
	GnomeVFSResult ret;
	ftpfs_dirent_t *dent;
		
	dent = g_new (ftpfs_dirent_t, 1);
	if (!dent)
		return GNOME_VFS_ERROR_NOMEM;

	dent->uri = ftpfs_uri_new (uri, &ret);
	if (!dent->uri){
		g_free (dent);
		return GNOME_VFS_ERROR_INVALIDURI;
	}

	dent->dir = retrieve_dir (dent->uri->conn, dent->uri->path, TRUE);
	if(dent->dir == NULL) {
		g_free(dent);
		return GNOME_VFS_ERROR_GENERIC;
	}
	ftpfs_dir_ref (dent->dir);
	dent->pos = NULL;
	dent->options = options;
	dent->meta_keys = meta_keys;
	dent->filter = filter;
	dent->pos = dent->dir->file_list;
	
	*method_handle = (GnomeVFSMethodHandle *) dent;
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
ftpfs_close_directory (GnomeVFSMethod *method,
		       GnomeVFSMethodHandle *method_handle,
		       GnomeVFSContext *context)
{
	ftpfs_dirent_t *dent = (ftpfs_dirent_t *) method_handle;

	ftpfs_dir_unref (dent->dir);
	ftpfs_uri_destroy (dent->uri);
	g_free (dent);
	
	return GNOME_VFS_OK;
}

static GnomeVFSResult
_ftpfs_read_directory (GnomeVFSMethodHandle *method_handle,
		       GnomeVFSFileInfo *info,
		       gboolean *skip)
{
	const GnomeVFSDirectoryFilter *filter;
	GnomeVFSDirectoryFilterNeeds filter_needs;
	gboolean filter_called;
	ftpfs_dirent_t *dent = (ftpfs_dirent_t *) method_handle;
	ftpfs_direntry_t *dentry;
	struct stat s;
	
	if (dent->pos == NULL)
		return GNOME_VFS_ERROR_EOF;
	
	/* This makes sure we do try to filter the file more than
           once.  */
	filter_called = FALSE;

	filter = dent->filter;
	if (filter != NULL)
		filter_needs = gnome_vfs_directory_filter_get_needs (filter);
	else
		filter_needs = GNOME_VFS_DIRECTORY_FILTER_NEEDS_NOTHING;
	    
	dentry = (ftpfs_direntry_t *) dent->pos->data;
	dent->pos = dent->pos->next;

	info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;
	info->name = g_strdup (dentry->name);

	if ((filter == NULL) &&
	    (!filter_called) &&
	    (filter_needs & (GNOME_VFS_DIRECTORY_FILTER_NEEDS_TYPE |
			     GNOME_VFS_DIRECTORY_FILTER_NEEDS_STAT |
			     GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE |
			     GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA))){
		if (!gnome_vfs_directory_filter_apply (filter, info)){
			*skip = TRUE;
			return GNOME_VFS_OK;
		}

		filter_called = TRUE;
	}

	/*
	 * Stat information
	 */
	s = dentry->s;
	if (dentry->l_stat){
		GNOME_VFS_FILE_INFO_SET_SYMLINK (info, TRUE);
		info->symlink_name = g_strdup (dentry->linkname);
		
		if (dent->options & GNOME_VFS_FILE_INFO_FOLLOWLINKS)
			s = dentry->s;
		info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME;
		
	} 
	gnome_vfs_stat_to_file_info (info, &s);
	GNOME_VFS_FILE_INFO_SET_LOCAL (info, FALSE);

	/*
	 *
	 */
	if (filter != NULL
	    && ! filter_called
	    && ! (filter_needs
		  & (GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE
		     | GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA))) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	if (dent->options & GNOME_VFS_FILE_INFO_GETMIMETYPE){
		const char *mime_name;
		const char *mime_type;
		
		if ((dent->options & GNOME_VFS_FILE_INFO_FOLLOWLINKS)
		    && info->type != GNOME_VFS_FILE_TYPE_BROKENSYMLINK
		    && info->symlink_name != NULL)
			mime_name = info->symlink_name;
		else
			mime_name = info->name;
		
		mime_type = gnome_vfs_mime_type_or_default (mime_name, NULL);

		if (mime_type == NULL)
			mime_type = gnome_vfs_mime_type_from_mode (s.st_mode);
		info->mime_type = g_strdup (mime_type);
		info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}

	if (filter != NULL
	    && ! filter_called
	    && ! (filter_needs & GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA)) {
		if (! gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	gnome_vfs_set_meta_for_list (info, info->name, dent->meta_keys);

	
	if (filter != NULL && ! filter_called) {
		if (!gnome_vfs_directory_filter_apply (filter, info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	*skip = FALSE;
	return GNOME_VFS_OK;
}

static GnomeVFSResult
ftpfs_read_directory (GnomeVFSMethod *method,
		      GnomeVFSMethodHandle *method_handle,
		      GnomeVFSFileInfo *file_info,
		      GnomeVFSContext *context)
{
	GnomeVFSResult result;
	gboolean skip;

	do {
		result = _ftpfs_read_directory (
			method_handle, file_info, &skip);
		if (result != GNOME_VFS_OK)
			break;
		if (skip)
			gnome_vfs_file_info_clear (file_info);
	} while (skip);

	return result;
}

static void
fill_file_info (const char *filename,
		ftpfs_direntry_t *dentry,
		GnomeVFSFileInfo *file_info,
		GnomeVFSFileInfoOptions options,
		const GList *meta_keys)
{
	struct stat s;

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;

	/*
	 * Stat
	 */
	s = dentry->s;
	if (dentry->l_stat){
		GNOME_VFS_FILE_INFO_SET_SYMLINK (file_info, TRUE);
		file_info->symlink_name = g_strdup (dentry->linkname);
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME;		
	} 
	gnome_vfs_stat_to_file_info (file_info, &s);
	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, FALSE);
	
	file_info->name = g_strdup (filename);
	
	/*
	 * Mime type
	 */
	if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE){
		const char *mime_name = NULL;
		const char *mime_type = NULL;
		
		if ((options & GNOME_VFS_FILE_INFO_FOLLOWLINKS)
		    && file_info->type != GNOME_VFS_FILE_TYPE_BROKENSYMLINK
		    && file_info->symlink_name != NULL)
			mime_name = file_info->symlink_name;
		else
			mime_name = file_info->name;
		
		mime_type = gnome_vfs_mime_type_or_default (mime_name, NULL);
		
		if (mime_type == NULL)
			mime_type = gnome_vfs_mime_type_from_mode (s.st_mode);

		file_info->mime_type = g_strdup(mime_type);
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}
	gnome_vfs_set_meta_for_list (file_info, file_info->name, meta_keys);
}

static GnomeVFSResult
ftpfs_get_file_info (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSFileInfo *file_info,
		     GnomeVFSFileInfoOptions options,
		     const GList *meta_keys,
		     GnomeVFSContext *context)
{
	GnomeVFSResult ret;
	ftpfs_uri_t *ftpfs_uri;
	ftpfs_dir_t *dir;
	GList *l;
	char *dirname, *filename, *real_filename = NULL;

	ftpfs_uri = ftpfs_uri_new (uri, &ret);
	if (!ftpfs_uri)
		return GNOME_VFS_ERROR_INVALIDURI;

	dirname = (*ftpfs_uri->path)?g_dirname(ftpfs_uri->path):NULL;
	filename = g_basename (ftpfs_uri->path);
	if (!dirname || !*dirname){
		g_free (dirname);
		dirname = g_strdup ("/");
		g_free(filename);
		filename = g_strdup(".");
		real_filename = "/";
	}

	dir = retrieve_dir (ftpfs_uri->conn, dirname, TRUE);
	g_free (dirname);

	if (dir == NULL){
		ftpfs_uri_destroy (ftpfs_uri);
		return GNOME_VFS_ERROR_NOTFOUND;
	}

	for (l = dir->file_list; l; l = l->next){
		ftpfs_direntry_t *fe;
		
		fe = l->data;

		if (strcmp (fe->name, filename))
			continue;

		if (S_ISLNK (fe->s.st_mode)) {
			if (fe->l_stat == NULL){
				ftpfs_uri_destroy (ftpfs_uri);
				return GNOME_VFS_ERROR_NOTFOUND;
			}

			if (S_ISLNK (fe->l_stat->st_mode)){
				ftpfs_uri_destroy (ftpfs_uri);
				return GNOME_VFS_ERROR_LOOP;
			}
		}

		fill_file_info (real_filename?real_filename:filename, fe, file_info, options, meta_keys);
		ftpfs_uri_destroy (ftpfs_uri);

		return GNOME_VFS_OK;
	}
	return GNOME_VFS_ERROR_NOTFOUND;
}

static GnomeVFSResult
ftpfs_get_file_info_from_handle (GnomeVFSMethod *method,
				 GnomeVFSMethodHandle *method_handle,
				 GnomeVFSFileInfo *file_info,
				 GnomeVFSFileInfoOptions options,
				 const GList *meta_keys,
				 GnomeVFSContext *context)
{
	ftpfs_file_handle_t *fh;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);
	fh = (ftpfs_file_handle_t *) method_handle;

	fill_file_info (fh->fe->name, fh->fe, file_info, options, meta_keys);
	
	return GNOME_VFS_OK;
}

static gboolean
ftpfs_is_local (GnomeVFSMethod *method,
		const GnomeVFSURI *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);
	
	/* We are never a native file system */
	return FALSE;
}

static GnomeVFSResult
ftpfs_make_directory (GnomeVFSMethod *method,
		      GnomeVFSURI *uri,
		      guint perm,
		      GnomeVFSContext *context)
{
	return send_ftp_command (uri, "MKD %s", OPT_FLUSH);
}

static GnomeVFSResult
ftpfs_remove_directory (GnomeVFSMethod *method,
			GnomeVFSURI *uri,
			GnomeVFSContext *context)
{
	return send_ftp_command (uri, "RMD %s", OPT_FLUSH);

}

static GnomeVFSMethod method = {
	ftpfs_open,
	ftpfs_create,
	ftpfs_close,
	ftpfs_read,
	ftpfs_write,
	ftpfs_seek,
	ftpfs_tell,
	NULL, /* truncate_handle */
	ftpfs_open_directory,
	ftpfs_close_directory,
	ftpfs_read_directory,
	ftpfs_get_file_info,
	ftpfs_get_file_info_from_handle,
	ftpfs_is_local,
	ftpfs_make_directory,
	ftpfs_remove_directory,
	NULL,
	NULL,
	NULL /* truncate */,
	NULL /* find_directory */
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
