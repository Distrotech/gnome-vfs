// this file is a hack to fix the build
// until the file is really checked in

#ifndef _GNOME_VFS_SOCKET_BUFFER_H
#define _GNOME_VFS_SOCKET_BUFFER_H

#include "gnome-vfs-socket.h"

typedef struct {

} GnomeVFSSocketBuffer;

GnomeVFSSocketBuffer*  gnome_vfs_socket_buffer_new      (GnomeVFSSocket *socket);


GnomeVFSResult   gnome_vfs_socket_buffer_read     (GnomeVFSSocketBuffer *buffer, char *character,
						   int who_knows, GnomeVFSFileSize *bytes_read);
GnomeVFSResult   gnome_vfs_socket_buffer_peekc    (GnomeVFSSocketBuffer *buffer, char *character);
GnomeVFSResult   gnome_vfs_socket_buffer_write    (GnomeVFSSocketBuffer *buffer, char *request, 
							 int len, GnomeVFSFileSize *bytes_written);
GnomeVFSResult   gnome_vfs_socket_buffer_flush    (GnomeVFSSocketBuffer *buffer);
GnomeVFSResult   gnome_vfs_socket_buffer_destroy  (GnomeVFSSocketBuffer *buffer, gboolean who_knows);

#endif
