#ifndef _GNOME_VFS_CLIENT_CALL_H_
#define _GNOME_VFS_CLIENT_CALL_H_

#include <bonobo/bonobo-object.h>
#include <GNOME_VFS_Daemon.h>

G_BEGIN_DECLS

typedef struct _GnomeVFSClientCall GnomeVFSClientCall;

#define GNOME_TYPE_VFS_CLIENT_CALL        (gnome_vfs_client_call_get_type ())
#define GNOME_VFS_CLIENT_CALL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_CLIENT_CALL, GnomeVFSClientCall))
#define GNOME_VFS_CLIENT_CALL_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_CLIENT_CALL, GnomeVFSClientCallClass))
#define GNOME_IS_VFS_CLIENT_CALL(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_CLIENT_CALL))
#define GNOME_IS_VFS_CLIENT_CALL_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_CLIENT_CALL))

struct _GnomeVFSClientCall {
	BonoboObject parent;

	GMutex *mutex;
};

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_VFS_ClientCall__epv epv;
} GnomeVFSClientCallClass;

GType gnome_vfs_client_call_get_type (void) G_GNUC_CONST;

GnomeVFSClientCall *_gnome_vfs_client_call_get (void);

G_END_DECLS

#endif /* _GNOME_VFS_CLIENT_CALL_H_ */
