// this file is a hack to fix the build
// until the file is really checked in

#include "gnome-vfs-socket.h"

GnomeVFSSocket* gnome_vfs_socket_new (GnomeVFSSocketImpl *impl, void *connection) {
  return NULL;
}
