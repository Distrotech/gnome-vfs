#ifndef _GNOME_VFS_DAEMON_H_
#define _GNOME_VFS_DAEMON_H_

#include <bonobo/bonobo-object.h>
#include <GNOME_VFS_Daemon.h>
#include <gnome-vfs-daemon-handle.h>
#include <gnome-vfs-daemon-dir-handle.h>

G_BEGIN_DECLS

typedef struct _GnomeVFSDaemon GnomeVFSDaemon;

#define GNOME_TYPE_VFS_DAEMON        (gnome_vfs_daemon_get_type ())
#define GNOME_VFS_DAEMON(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_DAEMON, GnomeVFSDaemon))
#define GNOME_VFS_DAEMON_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_DAEMON, GnomeVFSDaemonClass))
#define GNOME_IS_VFS_DAEMON(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_DAEMON))
#define GNOME_IS_VFS_DAEMON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_DAEMON))

struct _GnomeVFSDaemon {
	BonoboObject parent;

	GList       *clients;

	gpointer     priv;
};

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_VFS_Daemon__epv epv;
} GnomeVFSDaemonClass;

GType gnome_vfs_daemon_get_type (void) G_GNUC_CONST;

void gnome_vfs_daemon_add_context              (const GNOME_VFS_Client   client,
						GnomeVFSContext         *context);
void gnome_vfs_daemon_remove_context           (const GNOME_VFS_Client   client,
						GnomeVFSContext         *context);
void gnome_vfs_daemon_add_client_handle        (const GNOME_VFS_Client   client,
						GnomeVFSDaemonHandle    *handle);
void gnome_vfs_daemon_remove_client_handle     (const GNOME_VFS_Client   client,
						GnomeVFSDaemonHandle    *handle);
void gnome_vfs_daemon_add_client_dir_handle    (const GNOME_VFS_Client   client,
						GnomeVFSDaemonDirHandle *handle);
void gnome_vfs_daemon_remove_client_dir_handle (const GNOME_VFS_Client   client,
						GnomeVFSDaemonDirHandle *handle);




G_END_DECLS

#endif /* _GNOME_VFS_DAEMON_H_ */
