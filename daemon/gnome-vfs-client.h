#ifndef _GNOME_VFS_CLIENT_H_
#define _GNOME_VFS_CLIENT_H_

#include <bonobo/bonobo-object.h>
#include <GNOME_VFS_Daemon.h>

G_BEGIN_DECLS

typedef struct _GnomeVfsClient GnomeVfsClient;

#define GNOME_TYPE_VFS_CLIENT        (gnome_vfs_client_get_type ())
#define GNOME_VFS_CLIENT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_CLIENT, GnomeVfsClient))
#define GNOME_VFS_CLIENT_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_CLIENT, GnomeVfsClientClass))
#define GNOME_IS_VFS_CLIENT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_CLIENT))
#define GNOME_IS_VFS_CLIENT_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_CLIENT))

struct _GnomeVfsClient {
	BonoboObject parent;

	gpointer    private;
};

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_VFS_Client__epv epv;
} GnomeVfsClientClass;

GType gnome_vfs_client_get_type (void) G_GNUC_CONST;
void  gnome_vfs_client_deregister (GnomeVfsClient    *client,
				   GNOME_VFS_Daemon   daemon,
				   CORBA_Environment *ev);
void  gnome_vfs_client_register (GnomeVfsClient    *client,
				 GNOME_VFS_Daemon   daemon,
				 CORBA_Environment *ev);

G_END_DECLS

#endif /* _GNOME_VFS_CLIENT_H_ */
