#ifndef _GNOME_VFS_DAEMON_H_
#define _GNOME_VFS_DAEMON_H_

#include <bonobo/bonobo-object.h>
#include <GNOME_VFS_Daemon.h>

G_BEGIN_DECLS

typedef struct _GnomeVfsDaemon GnomeVfsDaemon;

#define GNOME_TYPE_VFS_DAEMON        (gnome_vfs_daemon_get_type ())
#define GNOME_VFS_DAEMON(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_DAEMON, GnomeVfsDaemon))
#define GNOME_VFS_DAEMON_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_DAEMON, GnomeVfsDaemonClass))
#define GNOME_IS_VFS_DAEMON(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_DAEMON))
#define GNOME_IS_VFS_DAEMON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_DAEMON))

struct _GnomeVfsDaemon {
	BonoboObject parent;

	GList       *clients;

	gpointer     private;
};

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_VFS_Daemon__epv epv;
} GnomeVfsDaemonClass;

GType gnome_vfs_daemon_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _GNOME_VFS_DAEMON_H_ */
