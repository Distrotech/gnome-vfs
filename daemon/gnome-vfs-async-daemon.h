#ifndef _GNOME_VFS_ASYNC_DAEMON_H_
#define _GNOME_VFS_ASYNC_DAEMON_H_

#include <bonobo/bonobo-object.h>
#include <GNOME_VFS_Daemon.h>

G_BEGIN_DECLS

typedef struct _GnomeVFSAsyncDaemon GnomeVFSAsyncDaemon;

#define GNOME_TYPE_VFS_ASYNC_DAEMON        (gnome_vfs_async_daemon_get_type ())
#define GNOME_VFS_ASYNC_DAEMON(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_ASYNC_DAEMON, GnomeVFSAsyncDaemon))
#define GNOME_VFS_ASYNC_DAEMON_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_ASYNC_DAEMON, GnomeVFSAsyncDaemonClass))
#define GNOME_IS_VFS_ASYNC_DAEMON(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_ASYNC_DAEMON))
#define GNOME_IS_VFS_ASYNC_DAEMON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_ASYNC_DAEMON))

struct _GnomeVFSAsyncDaemon {
	BonoboObject parent;

	GList       *clients;

	gpointer     private;
};

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_VFS_AsyncDaemon__epv epv;
} GnomeVFSAsyncDaemonClass;

GType gnome_vfs_async_daemon_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _GNOME_VFS_ASYNC_DAEMON_H_ */
