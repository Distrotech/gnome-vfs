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


GnomeVFSSocket* gnome_vfs_socket_new     (GnomeVFSSocketImpl *impl, void *connection);
GnomeVFSResult  gnome_vfs_socket_write   (GnomeVFSSocket *socket, char *buffer,
					  int buffer_length, GnomeVFSFileSize *bytes_written);
GnomeVFSResult  gnome_vfs_socket_close   (GnomeVFSSocket *socket);
GnomeVFSResult  gnome_vfs_socket_read    (GnomeVFSSocket *socket, char *buffer, 
					  GnomeVFSFileSize bytes, 
					  GnomeVFSFileSize *bytes_read);
#endif
