// this file is a hack to fix the build
// until the file is really checked in

#ifndef _GNOME_VFS_SOCKET_H
#define _GNOME_VFS_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <netdb.h>

#include "gnome-vfs-types.h"

typedef struct {
} GnomeVFSSocket;


typedef GnomeVFSResult (*GnomeVFSSocketReadFunc)  (GnomeVFSInetConnection *, gpointer, GnomeVFSFileSize, GnomeVFSFileSize*);
typedef GnomeVFSResult (*GnomeVFSSocketWriteFunc) (GnomeVFSInetConnection *, gpointer, GnomeVFSFileSize, GnomeVFSFileSize*);
typedef void           (*GnomeVFSSocketCloseFunc) (GnomeVFSInetConnection *, GnomeVFSCancellation *);
typedef struct {
  GnomeVFSSocketReadFunc one;
  GnomeVFSSocketWriteFunc two;
  GnomeVFSSocketCloseFunc three;
} GnomeVFSSocketImpl;

typedef struct {
	struct sockaddr_in addr;
	guint sock;
} SocketThinger;

GnomeVFSSocket* gnome_vfs_socket_new (GnomeVFSSocketImpl *impl, void *connection);
#endif
