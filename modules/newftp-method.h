#ifndef NEWFTP_METHOD_H
#define NEWFTP_METHOD_H

#include "gnome-vfs-module.h"

typedef struct _FtpConnectionUri {
	gchar *host;
	gchar *user;
	gchar *pass;
	gint port;
} FtpConnectionUri;

typedef struct _FtpUri {
	FtpConnectionUri connection_uri;
	gchar *path;
} FtpUri;

typedef struct _FtpConnection {
	GnomeVFSMethodHandle method_handle;
	GnomeVFSInetConnection *inet_connection;
	GnomeVFSIOBuf *iobuf;
	FtpConnectionUri *uri;
	gchar *cwd;
	GString *response_buffer;
	gchar *response_message;
	gint response_code;
	GnomeVFSInetConnection *data_connection;
	GnomeVFSIOBuf *data_iobuf;
	enum {
		FTP_NOTHING,
		FTP_READ,
		FTP_WRITE,
		FTP_READDIR
	} operation;
	gchar *dirlist;
	gchar *dirlistptr;
} FtpConnection;

#endif /* NEWFTP_METHOD_H */
