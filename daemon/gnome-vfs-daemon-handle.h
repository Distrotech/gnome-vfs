#ifndef _GNOME_VFS_DAEMON_HANDLE_H_
#define _GNOME_VFS_DAEMON_HANDLE_H_

#include <bonobo/bonobo-object.h>
#include "GNOME_VFS_Daemon.h"
#include "gnome-vfs-handle.h"

G_BEGIN_DECLS

typedef struct _GnomeVFSDaemonHandle GnomeVFSDaemonHandle;

#define GNOME_TYPE_VFS_DAEMON_HANDLE        (gnome_vfs_daemon_handle_get_type ())
#define GNOME_VFS_DAEMON_HANDLE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_DAEMON_HANDLE, GnomeVFSDaemonHandle))
#define GNOME_VFS_DAEMON_HANDLE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_DAEMON_HANDLE, GnomeVFSDaemonHandleClass))
#define GNOME_IS_VFS_DAEMON_HANDLE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_DAEMON_HANDLE))
#define GNOME_IS_VFS_DAEMON_HANDLE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_DAEMON_HANDLE))

struct _GnomeVFSDaemonHandle {
	BonoboObject parent;

	GMutex *mutex;
	GnomeVFSHandle *real_handle;
};

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_VFS_DaemonHandle__epv epv;
} GnomeVFSDaemonHandleClass;

GType gnome_vfs_daemon_handle_get_type (void) G_GNUC_CONST;

GnomeVFSDaemonHandle *gnome_vfs_daemon_handle_new (GnomeVFSHandle *handle);

G_END_DECLS

#endif /* _GNOME_VFS_DAEMON_HANDLE_H_ */
