#ifndef GNOME_VFS_BACKEND_H
#define GNOME_VFS_BACKEND_H

#include <glib/gtypes.h>

G_BEGIN_DECLS

void        gnome_vfs_backend_loadinit      		(gpointer app,
					     		 gpointer modinfo);
const char *gnome_vfs_backend_name          		(void);
gboolean    gnome_vfs_backend_init          		(gboolean init_deps);
void        gnome_vfs_backend_shutdown      		(void);

/* debugging calls */
int         gnome_vfs_backend_get_job_count		(void);

#include <libgnomevfs/gnome-vfs-context.h>

void	    gnome_vfs_backend_get_current_context       (/* OUT */ GnomeVFSContext **context);

void	    gnome_vfs_backend_dispatch_callback         (GnomeVFSCallback callback,
							 gpointer user_data,
							 gconstpointer in, gsize in_size,
							 gpointer out, gsize out_size);

G_END_DECLS

#endif
