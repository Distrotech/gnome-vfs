// this file is a hack to fix the build
// until the file is really checked in

#include "gnome-vfs-socket.h"

GnomeVFSSocket* gnome_vfs_socket_new (GnomeVFSSocketImpl *impl, void *connection) {
  return NULL;
}

GnomeVFSResult  
gnome_vfs_socket_write (GnomeVFSSocket *socket, char *buffer,
			int buffer_length, GnomeVFSFileSize *bytes_written)
{
  return GNOME_VFS_OK;
}

GnomeVFSResult  
gnome_vfs_socket_close (GnomeVFSSocket *socket)
{
  return GNOME_VFS_OK;
}
GnomeVFSResult  
gnome_vfs_socket_read  (GnomeVFSSocket *socket, char *buffer, 
			GnomeVFSFileSize bytes,
			GnomeVFSFileSize *bytes_read)
{
  return GNOME_VFS_OK;
}
