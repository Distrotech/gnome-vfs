// this file is a hack to fix the build
// until the file is really checked in

#include "gnome-vfs-socket-buffer.h"

GnomeVFSSocketBuffer*  
gnome_vfs_socket_buffer_new      (GnomeVFSSocket *socket)
{
  return NULL;
}


GnomeVFSResult   
gnome_vfs_socket_buffer_read     (GnomeVFSSocketBuffer *buffer, char *character,
						   int who_knows, GnomeVFSFileSize *bytes_read)
{
  return GNOME_VFS_OK;
}
GnomeVFSResult   
gnome_vfs_socket_buffer_peekc    (GnomeVFSSocketBuffer *buffer, char *character)
{
  return GNOME_VFS_OK;
}
GnomeVFSResult   
gnome_vfs_socket_buffer_write    (GnomeVFSSocketBuffer *buffer, char *request, 
							 int len, GnomeVFSFileSize *bytes_written)
{
  return GNOME_VFS_OK;
}
GnomeVFSResult   
gnome_vfs_socket_buffer_flush    (GnomeVFSSocketBuffer *buffer)
{
  return GNOME_VFS_OK;
}
GnomeVFSResult   
gnome_vfs_socket_buffer_destroy  (GnomeVFSSocketBuffer *buffer, gboolean who_knows)
{
  return GNOME_VFS_OK;
}
