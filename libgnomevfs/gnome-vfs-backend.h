#ifndef GNOME_VFS_BACKEND_H
#define GNOME_VFS_BACKEND_H

#include <libgnomevfs/gnome-vfs-context.h>

G_BEGIN_DECLS

void	    gnome_vfs_get_current_context       (/* OUT */ GnomeVFSContext **context);
void	    gnome_vfs_dispatch_callback         (GnomeVFSCallback callback,
						 gpointer user_data,
						 gconstpointer in, gsize in_size,
						 gpointer out, gsize out_size);

G_END_DECLS

#endif
