/* newftp-method.c - VFS modules for FTP

   Copyright (C) 2000 Ian McKellar

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

/* see RFC 959 for protocol details */

/* SOME INVALID ASSUMPTIONS I HAVE MADE:
 * All FTP servers return UNIX ls style responses to LIST,
 * All FTP servers deal with LIST <full path>
 */

/* TODO 
 * Make koobera.math.uic.edu work
 * Make NetPresenz work (eg: uniserver.uwa.edu.au)
 * FtpUri / FtpConnectionUri refcounting or something.
 * Create Bugzilla entry relating to cacheing
 * Fix do_get_file_info_from_handle
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h> /* for atoi */
#include <stdio.h> /* for sscanf */
#include <ctype.h> /* for isspace */

#include <gtk/gtk.h>

#include <string.h>

#include "gnome-vfs-module.h"
#include "gnome-vfs-module-shared.h"
#include "gnome-vfs-mime.h"

#include "newftp-method.h"

/* maximum size of response we're expecting to get */
#define MAX_RESPONSE_SIZE 4096 

/* macros for the checking of FTP response codes */

#define IS_100(X) ((X)>=100 && (X)<200)
#define IS_200(X) ((X)>=200 && (X)<300)
#define IS_300(X) ((X)>=300 && (X)<400)
#define IS_400(X) ((X)>=400 && (X)<500)
#define IS_500(X) ((X)>=500 && (X)<600)

static GnomeVFSResult do_open	   (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode,
					 GnomeVFSContext *context);
/*
static GnomeVFSResult do_create	 (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode,
					 gboolean exclusive,
					 guint perm,
					 GnomeVFSContext *context);
static GnomeVFSResult do_close	  (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 GnomeVFSContext *context);
*/

/*
static GnomeVFSResult do_read(GnomeVFSMethod *method,
					GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read, GnomeVFSContext *context);
static GnomeVFSResult do_write	  (GnomeVFSMethod *method,GnomeVFSMethodHandle *method_handle,
					 gconstpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_written, GnomeVFSContext *context);

					 */
/*
static GnomeVFSResult do_open_directory (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 const GnomeVFSDirectoryFilter *filter,
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
					 const GList *meta_keys,
					 GnomeVFSContext *context);
static GnomeVFSResult do_get_file_info_from_handle
					(GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);
*/
static gboolean       do_is_local       (GnomeVFSMethod *method,
					 const GnomeVFSURI *uri);

guint ftp_connection_uri_hash(gconstpointer c);
gint ftp_connection_uri_equal(gconstpointer c, gconstpointer d);


gchar *anon_user = "anonymous";
gchar *anon_pass = "nobody@gnome.org";
gint control_port = 21;


/* A GHashTable of GLists of FtpConnections */
static GHashTable *spare_connections = NULL;
G_LOCK_DEFINE_STATIC(spare_connections);

#define ftp_debug(c,g) FTP_DEBUG((c),(g),__FILE__, __LINE__, __PRETTY_FUNCTION__)

static void FTP_DEBUG(FtpConnection *conn, gchar *text, gchar *file, gint line, gchar *func) {
	if(conn) {
		g_print("%s:%d (%s) [ftp conn=%p]\n %s\n", file, line, func, conn, text);
	} else {
		g_print("%s:%d (%s) [ftp]\n %s\n", file, line, func, text);
	}

	g_free(text);
}

static GnomeVFSResult ftp_response_to_vfs_result(gint response) {
	switch(response) {
		case 421: 
		case 426: 
			return GNOME_VFS_ERROR_CANCELLED;
		case 425:
			return GNOME_VFS_ERROR_ACCESSDENIED;
		case 530:
		case 331:
		case 332:
		case 532:
			return GNOME_VFS_ERROR_LOGINFAILED;
		case 450:
		case 550:
		case 451:
		case 551:
			return GNOME_VFS_ERROR_NOTFOUND;
		case 452:
		case 552:
			return GNOME_VFS_ERROR_NOSPACE;
		case 553:
			return GNOME_VFS_ERROR_BADFILE;
	}

	/* FIXME - is this the correct interpretation of this error? */
	/*if(IS_100(response)) return GNOME_VFS_ERROR_INPROGRESS;*/
	if(IS_100(response)) return GNOME_VFS_OK;
	if(IS_200(response)) return GNOME_VFS_OK;
	/* FIXME - is this the correct interpretation of this error? */
	/*if(IS_300(response)) return GNOME_VFS_ERROR_INPROGRESS;*/
	if(IS_300(response)) return GNOME_VFS_OK;
	if(IS_400(response)) return GNOME_VFS_ERROR_GENERIC;
	if(IS_500(response)) return GNOME_VFS_ERROR_INTERNAL;

	return GNOME_VFS_ERROR_GENERIC;

}

static GnomeVFSResult read_response_line(FtpConnection *conn, gchar **line) {
	GnomeVFSFileSize bytes = MAX_RESPONSE_SIZE, bytes_read;
	gchar *ptr, *buf = g_malloc(MAX_RESPONSE_SIZE+1);
	gint line_length;
	GnomeVFSResult result = GNOME_VFS_OK;

	while(!strstr(conn->response_buffer->str, "\r\n")) {
		/* we don't have a full line. Lets read some... */
		//ftp_debug(conn,g_strdup_printf("response `%s' is incomplete", conn->response_buffer->str));
		bytes_read = 0;
		result = gnome_vfs_iobuf_read(conn->iobuf, buf,
				bytes, &bytes_read);
		buf[bytes_read] = '\0';
		//ftp_debug(conn,g_strdup_printf("read `%s'", buf));
		conn->response_buffer = g_string_append(conn->response_buffer, buf);
		if(result != GNOME_VFS_OK) {
			g_warning("Error `%s' during read\n", gnome_vfs_result_to_string(result));
			g_free(buf);
			return result;
		}
	}

	g_free(buf);

  ptr = strstr(conn->response_buffer->str, "\r\n");
  line_length = ptr - conn->response_buffer->str;

	*line = g_strndup(conn->response_buffer->str, line_length);

	g_string_erase(conn->response_buffer, 0 , line_length + 2);

	return result;
}

static GnomeVFSResult get_response(FtpConnection *conn) {
	/* all that should be pending is a response to the last command */
	GnomeVFSResult result;

	
	//ftp_debug(conn,g_strdup_printf("get_response(%p)",  conn));


	while(TRUE) {
		gchar *line = NULL;
		result = read_response_line(conn, &line);

		if(result != GNOME_VFS_OK) {
			if(line) g_free(line);
			g_warning("Error reading response line.");
			return result;
		}

#ifdef FTP_RESPONSE_DEBUG
		g_print("FTP: %s\n", line);
#endif

		/* response needs to be at least: "### x"  - I think*/
		if( isdigit(line[0]) &&
				isdigit(line[1]) &&
				isdigit(line[2]) &&
				isspace(line[3])) {
			
			conn->response_code = (line[0]-'0')*100 + (line[1]-'0')*10 + (line[2]-'0');

			if(conn->response_message) g_free(conn->response_message);
			conn->response_message = g_strdup(line+4);

			ftp_debug(conn,g_strdup_printf("got response %d (%s)", conn->response_code, conn->response_message));

			if(line) g_free(line);

			return ftp_response_to_vfs_result(conn->response_code);

		}

		/* hmm - not a valid line - lets ignore it :-) */
		if(line) g_free(line);

	}

	return GNOME_VFS_OK; /* should never be reached */

}

static GnomeVFSResult do_control_write(FtpConnection *conn, gchar *command) {
	gchar *actual_command = g_strdup_printf("%s\r\n", command);
	GnomeVFSFileSize bytes = strlen(actual_command), bytes_written;
	GnomeVFSResult result = gnome_vfs_iobuf_write(conn->iobuf,
			actual_command, bytes, &bytes_written);
	ftp_debug(conn,g_strdup_printf("sent \"%s\\r\\n\"", command));
	gnome_vfs_iobuf_flush(conn->iobuf);

	if(result != GNOME_VFS_OK) {
		/* FIXME - return something useful */
		g_free(actual_command);
		return result; /* FIXME */
	}

	if(bytes != bytes_written) {
		/* FIXME - return something useful */
		g_free(actual_command);
		return result; /* FIXME */
	}

	g_free(actual_command);

	return result;
}

static GnomeVFSResult do_basic_command(FtpConnection *conn, gchar *command) {
	GnomeVFSResult result = do_control_write(conn, command);

	if(result != GNOME_VFS_OK) {
		/* FIXME - return something useful */
		return result;
	}

	return get_response(conn);
}

static GnomeVFSResult do_transfer_command(FtpConnection *conn, gchar *command) {
	char *host = NULL;
	gint port;
	GnomeVFSResult result;
	
	/* FIXME - support other than PASV */

	/* send PASV */
	do_basic_command(conn, "PASV");

	/* parse response */
	{
		gint a1, a2, a3, a4, p1, p2;
		gchar *ptr, *response = g_strdup(conn->response_message);
		ptr = strchr(response, '(');
		if(!ptr ||
			(sscanf(ptr+1,"%d,%d,%d,%d,%d,%d", &a1, &a2, &a3, &a4, &p1, &p2) != 6)) {
			//ftp_debug(conn,g_strdup_printf("PASV response parse error `%s'", ptr?ptr+1:response));
			g_free(response);
			return GNOME_VFS_ERROR_CORRUPTEDDATA; /* uhh - I guess */
		}

		host = g_strdup_printf("%d.%d.%d.%d", a1, a2, a3, a4);
		port = p1*256 + p2;

		g_free(response);

		//ftp_debug(conn,g_strdup_printf("connecting to %s:%d", host, port));
	}

	/* connect */
  result = gnome_vfs_inet_connection_create(&conn->data_connection,
      host, port,
      NULL /* FIXME where do I get a GnomeVFSCancellation? */);

  if(result != GNOME_VFS_OK) {
    /* FIXME - should really return the error to the app somehow */
		//ftp_debug(conn,g_strdup_printf("gnome_vfs_inet_connection_create failed."));
		g_free(host);
    return result;
  }

	conn->data_iobuf = gnome_vfs_inet_connection_get_iobuf(conn->data_connection);

	if(conn->iobuf == NULL) {
		gnome_vfs_inet_connection_destroy(conn->data_connection, NULL);
		g_free(host);
		return GNOME_VFS_ERROR_GENERIC;
	}

	result = do_control_write(conn, command);

	if(result != GNOME_VFS_OK) {
		// FIXME free stuff?
		return result;
	}

	result = get_response(conn);

	if(result != GNOME_VFS_OK) {
		// FIXME free stuff?
		return result;
	}

	//ftp_debug(conn,g_strdup_printf("`%s' returned `%s'", command, gnome_vfs_result_to_string(result)));

	return result;
}

static GnomeVFSResult end_transfer(FtpConnection *conn) {
	GnomeVFSResult result;

	//ftp_debug(conn, g_strdup("end_transfer()"));

	if(conn->data_connection) {
		gnome_vfs_inet_connection_destroy(conn->data_connection, NULL);
		conn->data_connection = NULL;
	}

	if(conn->data_iobuf) {
		gnome_vfs_iobuf_destroy(conn->data_iobuf);
		conn->data_iobuf = NULL;
	}

	result = get_response(conn);

	return result;

}

static GnomeVFSResult ftp_connection_create(FtpConnection **connptr,
		FtpConnectionUri *uri) {
	FtpConnection *conn = g_new(FtpConnection, 1);
	GnomeVFSResult result;
	gchar *tmpstring;

	conn->uri = uri;
	conn->cwd = NULL;
	conn->data_connection = NULL;
	conn->data_iobuf = NULL;
	conn->response_buffer = g_string_new("");
	conn->response_message = NULL;
	conn->response_code = -1;

	result = gnome_vfs_inet_connection_create(&conn->inet_connection, 
			uri->host, uri->port, 
			NULL /* FIXME where do I get a GnomeVFSCancellation? */);
	if(result != GNOME_VFS_OK) {
		g_warning("gnome_vfs_inet_connection_create(\"%s\", %d) = \"%s\"",
				uri->host, uri->port, gnome_vfs_result_to_string(result));
		g_string_free(conn->response_buffer, TRUE);
		g_free(conn);
		return result;
	}

	conn->iobuf = gnome_vfs_inet_connection_get_iobuf(conn->inet_connection);

	if(conn->iobuf == NULL) {
		g_warning("gnome_vfs_inet_connection_get_iobuf() failed");
		gnome_vfs_inet_connection_destroy(conn->inet_connection, NULL);
		g_string_free(conn->response_buffer, TRUE);
		g_free(conn);
		return GNOME_VFS_ERROR_GENERIC;
	}

	result = get_response(conn);

	if(result != GNOME_VFS_OK) { 
		g_warning("ftp server (%s:%d) said `%d %s'", uri->host, uri->port,
				conn->response_code, conn->response_message);
		g_string_free(conn->response_buffer, TRUE);
		g_free(conn);
		return result;
	}

	tmpstring = g_strdup_printf("USER %s", conn->uri->user);
	result = do_basic_command(conn, tmpstring);
	g_free(tmpstring);

	if(IS_300(conn->response_code)) {
		tmpstring = g_strdup_printf("PASS %s", conn->uri->pass);
		result = do_basic_command(conn, tmpstring);
		g_free(tmpstring);
	}

	if(result != GNOME_VFS_OK) {
		/* login failed */
		g_warning("FTP server said: \"%d %s\"\n", conn->response_code,
				conn->response_message);
		gnome_vfs_iobuf_destroy(conn->iobuf);
		gnome_vfs_inet_connection_destroy(conn->inet_connection, NULL);
		g_free(conn);
		/* FIXME - should really return the error to the app somehow */
		return result;
	}

	/* okay, we should be connected now */

	/* Image mode (binary to the uninitiated) */

	do_basic_command(conn, "TYPE I");

	*connptr = conn;

	ftp_debug(conn, g_strdup("created"));

	return GNOME_VFS_OK;
}

guint ftp_connection_uri_hash(gconstpointer c) {
	FtpConnectionUri *conn = (FtpConnectionUri *)c;

	return g_str_hash(conn->host) + g_str_hash(conn->user) + 
		     g_str_hash(conn->pass) + conn->port;
}

gint ftp_connection_uri_equal(gconstpointer c, gconstpointer d) {
	FtpConnectionUri *conn1 = (FtpConnectionUri *)c;
	FtpConnectionUri *conn2 = (FtpConnectionUri *)d;

	return g_str_equal(conn1->host, conn2->host) &&
		     g_str_equal(conn1->user, conn2->user) &&
				 g_str_equal(conn1->pass, conn2->pass) &&
				 conn1->port == conn2->port;
}

#if 0
/* some debugging routines */
static gchar *ftp_connection_uri_to_string(FtpConnectionUri *furi) {
	return g_strdup_printf("ftp://%s@%s:%s:%d", furi->user, furi->pass, 
			furi->host, furi->port);
}

static gchar *ftp_uri_to_string(FtpUri *furi) {
	gchar *cu = ftp_connection_uri_to_string(&furi->connection_uri),
		*u = g_strdup_printf("%s%s", cu, furi->path);
	g_free(cu);
	return u;
}
#endif

static GnomeVFSResult ftp_connection_aquire(FtpUri *furi, FtpConnection **connection) {
	FtpConnectionUri *uri = &(furi->connection_uri);
	GList *possible_connections;
	FtpConnection *conn = NULL;
	GnomeVFSResult result = GNOME_VFS_OK;

	G_LOCK(spare_connections);

	if(spare_connections == NULL) {
		//ftp_debug(NULL, strdup("creating spare_connections hash"));
		spare_connections = 
			g_hash_table_new(ftp_connection_uri_hash, ftp_connection_uri_equal);
	}

	possible_connections = g_hash_table_lookup(spare_connections, uri);

	if(possible_connections) {
		/* spare connection(s) found */
		conn = (FtpConnection *)possible_connections->data;
		ftp_debug(conn, strdup("found a connection"));
		possible_connections = g_list_remove(possible_connections, conn);
		g_hash_table_insert(spare_connections, uri, possible_connections);

		/* make sure connection hasn't timed out */
		result = do_basic_command(conn, "PWD");
		if(result != GNOME_VFS_OK) {
			result = ftp_connection_create(&conn, uri);
		}

	} else {
		result = ftp_connection_create(&conn, uri);
	}

	G_UNLOCK(spare_connections);

	*connection = conn;

	return result;
}


static void ftp_connection_release(FtpConnection *conn) {
	GList *possible_connections;

	if(conn == NULL) { /* FIXME - turn into g_return_if_fail */
		g_warning("ftp_connection_release(NULL) called");
		return;
	}

	G_LOCK(spare_connections);
	if(spare_connections == NULL) 
		spare_connections = 
			g_hash_table_new(ftp_connection_uri_hash, ftp_connection_uri_equal);

	possible_connections = g_hash_table_lookup(spare_connections, conn->uri);
	ftp_debug(conn, g_strdup_printf("releasing [len = %d]", g_list_length(possible_connections)));
	possible_connections = g_list_append(possible_connections, conn);
	g_hash_table_insert(spare_connections, conn->uri, possible_connections);

	G_UNLOCK(spare_connections);
}

static FtpUri *ftp_uri_create(GnomeVFSURI *vuri) {
	FtpUri *furi = g_new(FtpUri, 1);

	furi->connection_uri.host = g_strdup(gnome_vfs_uri_get_host_name(vuri));
	furi->connection_uri.user = g_strdup(gnome_vfs_uri_get_user_name(vuri));
	furi->connection_uri.pass = g_strdup(gnome_vfs_uri_get_password(vuri));
	furi->connection_uri.port = gnome_vfs_uri_get_host_port(vuri);
	furi->path = g_strdup(vuri->text);

	/* handle anonymous ftp */
	if(furi->connection_uri.user == NULL) {
		furi->connection_uri.user = g_strdup(anon_user);
		if(furi->connection_uri.pass == NULL) {
			furi->connection_uri.pass = g_strdup(anon_pass);
		}
	}

	/* default port */
	if(furi->connection_uri.port == 0) {
		furi->connection_uri.port = control_port;
	}

	return furi;
}


gboolean 
do_is_local (GnomeVFSMethod *method, const GnomeVFSURI *uri)
{
	return FALSE;
}


static GnomeVFSResult do_open	   (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode,
					 GnomeVFSContext *context) {

	FtpUri *furi = ftp_uri_create(uri);
	GnomeVFSResult result;
	FtpConnection *conn;
	gchar *command;

	result = ftp_connection_aquire(furi, &conn);
	if(result != GNOME_VFS_OK) return result;

	if(mode == GNOME_VFS_OPEN_READ) {
		command = g_strdup_printf("RETR %s", furi->path);
		conn->operation = FTP_READ;
	} else if(mode == GNOME_VFS_OPEN_WRITE) {
		command = g_strdup_printf("STOR %s", furi->path);
		conn->operation = FTP_WRITE;
	} else {
		g_warning("Unsupported open mode %d\n", mode);
		ftp_connection_release(conn);
		/* FIXME - free lots-o-stuff */
		return GNOME_VFS_ERROR_INVALIDOPENMODE;
	}
	result = do_transfer_command(conn, command);
	g_free(command);
	if(result == GNOME_VFS_OK) {
		*method_handle = (GnomeVFSMethodHandle *)conn;
	} else {
		*method_handle = NULL;
		ftp_connection_release(conn);
	}
	return result;
}

static GnomeVFSResult do_close	  (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 GnomeVFSContext *context) {
	FtpConnection *conn = (FtpConnection *)method_handle;

	GnomeVFSResult result = end_transfer(conn);

	ftp_connection_release(conn);

	return result;
}

static GnomeVFSResult do_read	   (GnomeVFSMethod *method, GnomeVFSMethodHandle *method_handle,
					 gpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_read, GnomeVFSContext *context) {
	FtpConnection *conn = (FtpConnection *)method_handle;
	/*
	if(conn->operation != FTP_READ) {
		g_print("attempted to read when conn->operation = %d\n", conn->operation);
		return GNOME_VFS_ERROR_NOTPERMITTED;
	}*/
	//g_print("do_read(%p)\n", method_handle);
	return gnome_vfs_iobuf_read(conn->data_iobuf, buffer, num_bytes, bytes_read);
}

static GnomeVFSResult do_write	  (GnomeVFSMethod *method, GnomeVFSMethodHandle *method_handle,
					 gconstpointer buffer,
					 GnomeVFSFileSize num_bytes,
					 GnomeVFSFileSize *bytes_written, GnomeVFSContext *context) {
	FtpConnection *conn = (FtpConnection *)method_handle;
	//g_print("do_write()\n");
	if(conn->operation != FTP_WRITE) return GNOME_VFS_ERROR_NOTPERMITTED;
	return 
		gnome_vfs_iobuf_write(conn->data_iobuf, buffer, num_bytes, bytes_written);
}


static gboolean ls_to_file_info(gchar *ls, GnomeVFSFileInfo *file_info) {
	struct stat s;
	gchar *filename = NULL, *linkname = NULL;

	g_print(ls);

	gnome_vfs_parse_ls_lga((const gchar *)ls, &s, &filename, &linkname);

	if(filename) {

		gnome_vfs_stat_to_file_info(file_info, &s);

		file_info->name = g_strdup(g_basename(filename));

		if(*(file_info->name) == '\0') {
			g_free(file_info->name);
			file_info->name = g_strdup("/");
		}

		if(linkname) {
			file_info->symlink_name = linkname;
			file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME;
		}

		if(file_info->type == GNOME_VFS_FILE_TYPE_REGULAR) {
			gchar *mime_type = (gchar *)gnome_vfs_mime_type_or_default(file_info->name, NULL);
			//ftp_debug(conn, g_strdup_printf("1: mimetype = %s", mime_type));
			if(mime_type == NULL) {
				//ftp_debug(conn, g_strdup_printf("mode = %d", s.st_mode));
				mime_type = (gchar *)gnome_vfs_mime_type_from_mode(s.st_mode);
			}
			//ftp_debug(conn, g_strdup_printf("2: mimetype = %s", mime_type));
			file_info->mime_type = g_strdup(mime_type);
		} else {
			file_info->mime_type = g_strdup(gnome_vfs_mime_type_from_mode(s.st_mode));
		}

		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;

		//ftp_debug(conn, g_strdup_printf("info got name `%s'", file_info->name));

		if(filename) g_free(filename);

		return TRUE;
	} else {
		return FALSE;
	}
}


static GnomeVFSResult internal_get_file_info  (GnomeVFSMethod *method,
					 FtpUri *furi,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 GnomeVFSContext *context) {
	FtpConnection *conn;
	/* FIXME - take away LS syntax */
	gchar *command = g_strdup_printf("LIST -ld %s", furi->path);
	GnomeVFSResult result;
	GnomeVFSFileSize num_bytes = 1024, bytes_read;
	gchar buffer[num_bytes+1];

	result = ftp_connection_aquire(furi, &conn);
	if(result != GNOME_VFS_OK) {
		g_free(command);
		return result;
	}

	//g_print("do_get_file_info()\n");

	do_transfer_command(conn, command);
	g_free(command);

	result = gnome_vfs_iobuf_read(conn->data_iobuf, buffer, num_bytes, &bytes_read);

	if(result != GNOME_VFS_OK) {
		//ftp_debug(conn, g_strdup("gnome_vfs_iobuf_read failed"));
		ftp_connection_release(conn);
		return result;
	}

	result = end_transfer(conn);

	/* FIXME check return? */

	ftp_connection_release(conn);

	if(result != GNOME_VFS_OK) {
		//ftp_debug(conn,g_strdup("LIST for get_file_info failed."));
		return result;
	}

	if(bytes_read>0) {

		buffer[bytes_read] = '\0';

		if(ls_to_file_info(buffer, file_info)) return GNOME_VFS_OK;

	}
	
	return GNOME_VFS_ERROR_NOTFOUND;

}

static GnomeVFSResult do_get_file_info  (GnomeVFSMethod *method,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 GnomeVFSContext *context) {

	FtpUri *furi = ftp_uri_create(uri);
	return internal_get_file_info(method, furi, file_info, options, 
			meta_keys, context);
}

#if 0

/* for this funciton to work we need to have the current URI stored in
 * the connection. This isn't currently the case and would take a bit of
 * recoding.
 */
static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSContext *context)
{
	FtpConnection *conn = (FtpConnection *)method_handle;
	return internal_get_file_info(method, conn->uri, file_info, options, 
			meta_keys, context);
}
#endif

static GnomeVFSResult do_open_directory (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 const GnomeVFSDirectoryFilter *filter,
					 GnomeVFSContext *context) {
	/* FIXME implement filters */
	FtpUri *furi = ftp_uri_create(uri);
	FtpConnection *conn;
	gchar *command = g_strdup_printf("LIST %s", furi->path);
	GnomeVFSResult result;
	GnomeVFSFileSize num_bytes = 1024, bytes_read;
	gchar buffer[num_bytes+1];
	GString *dirlist = g_string_new("");

	result = ftp_connection_aquire(furi, &conn);
	if(result != GNOME_VFS_OK) {
		g_free(command);
		g_string_free(dirlist, TRUE);
		return result;
	}

	//g_print("do_open_directory()\n");

	result = do_transfer_command(conn, command);

	if(result != GNOME_VFS_OK) {
		g_warning("\"%s\" failed because \"%s\"", command, 
				gnome_vfs_result_to_string(result));
		ftp_connection_release(conn);
		g_free(command);
		g_string_free(dirlist, TRUE);
		return result;
	}

	g_free(command);

	while(result == GNOME_VFS_OK) {
		result = gnome_vfs_iobuf_read(conn->data_iobuf, buffer, num_bytes, &bytes_read);
		if(result == GNOME_VFS_OK && bytes_read>0) {
			buffer[bytes_read] = '\0';
			dirlist = g_string_append(dirlist, buffer);
		} else {
			break;
		}
	} 

	result = end_transfer(conn);

	if(result != GNOME_VFS_OK) g_warning("end_transfer(conn) failed!!!!");

	conn->dirlist = g_strdup(dirlist->str);
	conn->dirlistptr = conn->dirlist;

	g_string_free(dirlist,TRUE);

	*method_handle = (GnomeVFSMethodHandle *)conn;

	return result;
}

static GnomeVFSResult do_close_directory(GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 GnomeVFSContext *context) {
	FtpConnection *conn = (FtpConnection *)method_handle;

	//g_print("do_close_directory()\n");

	g_free(conn->dirlist);
	conn->dirlist = NULL;
	conn->dirlistptr = NULL;
	ftp_connection_release(conn);

	return GNOME_VFS_OK;
}

static GnomeVFSResult do_read_directory (GnomeVFSMethod *method,
					 GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSContext *context) {

	FtpConnection *conn = (FtpConnection *)method_handle;

	if(!conn->dirlistptr || *(conn->dirlistptr) == '\0')
		return GNOME_VFS_ERROR_EOF;

	while(TRUE) {
		gboolean success = ls_to_file_info(conn->dirlistptr, file_info);

		/* go till we find \r\n */
		while(conn->dirlistptr &&
				*conn->dirlistptr && 
				*conn->dirlistptr != '\r' && 
				*conn->dirlistptr != '\n') {
			conn->dirlistptr++;
		}
		/* go past \r\n */
		while(conn->dirlistptr && *conn->dirlistptr && isspace(*conn->dirlistptr)) {
			conn->dirlistptr++;
		}

		if(success) break;
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod *method,
      GnomeVFSURI *a,
      GnomeVFSURI *b,
      gboolean *same_fs_return,
      GnomeVFSContext *context)
{
	FtpUri *furi1 = ftp_uri_create(a);
	FtpUri *furi2 = ftp_uri_create(b);

	if(ftp_connection_uri_equal(&furi1->connection_uri, &furi2->connection_uri)) {
    *same_fs_return = TRUE;
  } else {
    *same_fs_return = FALSE;
  }
	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_make_directory (GnomeVFSMethod *method,
		   GnomeVFSURI *uri,
		   guint perm,
		   GnomeVFSContext *context)
{
	/* FIXME care about perms */
	FtpUri *furi = ftp_uri_create(uri);
	FtpConnection *conn;
	gchar *command = g_strdup_printf("MKD %s", furi->path);
	GnomeVFSResult result;

	result = ftp_connection_aquire(furi, &conn);
	if(result != GNOME_VFS_OK) {
		g_free(command);
		return result;
	}

	result = do_basic_command(conn, command);

	g_free(command);
	ftp_connection_release(conn);

	return result;
}


static GnomeVFSResult
do_remove_directory (GnomeVFSMethod *method,
		     GnomeVFSURI *uri,
		     GnomeVFSContext *context)
{
	FtpUri *furi = ftp_uri_create(uri);
	FtpConnection *conn;
	gchar *command = g_strdup_printf("RMD %s", furi->path);
	GnomeVFSResult result;

	result = ftp_connection_aquire(furi, &conn);
	if(result != GNOME_VFS_OK) {
		g_free(command);
		return result;
	}

	result = do_basic_command(conn, command);

	g_free(command);
	ftp_connection_release(conn);

	return result;
}


static GnomeVFSResult
do_move (GnomeVFSMethod *method,
	 GnomeVFSURI *old_uri,
	 GnomeVFSURI *new_uri,
	 gboolean force_replace,
	 GnomeVFSContext *context)
{
	FtpUri *furi1 = ftp_uri_create(old_uri);
	FtpUri *furi2 = ftp_uri_create(new_uri);
	if(ftp_connection_uri_equal(&furi1->connection_uri, &furi2->connection_uri)) {
		FtpConnection *conn;
		gchar *command = g_strdup_printf("RNFR %s", furi1->path);
		GnomeVFSResult result;

		result = ftp_connection_aquire(furi1, &conn);
		if(result != GNOME_VFS_OK) {
			g_free(command);
			return result;
		}
		result = do_basic_command(conn, command);
		
		if(result == GNOME_VFS_OK) {
			g_free(command);
			command = g_strdup_printf("RNTO %s", furi2->path);
			result = do_basic_command(conn, command);
		}

		g_free(command);
		ftp_connection_release(conn);

		return result;
	} else {
		return GNOME_VFS_ERROR_NOTSAMEFS;
	}
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod *method,
	   GnomeVFSURI *uri,
	   GnomeVFSContext *context)
{
	FtpUri *furi = ftp_uri_create(uri);
	FtpConnection *conn;
	gchar *command = g_strdup_printf("DELE %s", furi->path);
	GnomeVFSResult result;

	result = ftp_connection_aquire(furi, &conn);
	if(result != GNOME_VFS_OK) {
		g_free(command);
		return result;
	}

	result = do_basic_command(conn, command);
	g_free(command);
	ftp_connection_release(conn);

	return result;
}

static GnomeVFSMethod method = {
	do_open,
	NULL, /* do_create */
	do_close,
	do_read, /* do_read */
	do_write, /* do_write */
	NULL, /* seek */
	NULL, /* tell */
	NULL, /* truncate */
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,
	NULL,
	do_is_local,
	do_make_directory, /* make directory */
	do_remove_directory, /* remove directory */
	do_move, /* rename */
	do_unlink, /* unlink */
	do_check_same_fs
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
