#include <unistd.h>
#include <bonobo-activation/bonobo-activation.h>
#include <libbonobo.h>
#include "gnome-vfs-client.h"

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVfsClient,
	gnome_vfs_client,
	GNOME_VFS_Client,
	BonoboObject,
	BONOBO_TYPE_OBJECT);

static void
impl_GNOME_VFS_Client_doSomething (PortableServer_Servant servant,
				   const CORBA_char      *aStr,
				   CORBA_Environment     *ev)
{
	g_warning ("Client do something '%s'", aStr);
}

static void
gnome_vfs_client_finalize (GObject *object)
{
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_client_instance_init (GnomeVfsClient *client)
{
}

static void
gnome_vfs_client_class_init (GnomeVfsClientClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_Client__epv *epv = &klass->epv;

	object_class->finalize = gnome_vfs_client_finalize;

	epv->doSomething = impl_GNOME_VFS_Client_doSomething;
}

void
gnome_vfs_client_register (GnomeVfsClient    *client,
			   GNOME_VFS_Daemon   daemon,
			   CORBA_Environment *ev)
{
	GNOME_VFS_Daemon_registerClient (daemon, BONOBO_OBJREF (client), ev);
}


void
gnome_vfs_client_deregister (GnomeVfsClient    *client,
			     GNOME_VFS_Daemon   daemon,
			     CORBA_Environment *ev)
{
	GNOME_VFS_Daemon_deRegisterClient (daemon, BONOBO_OBJREF (client), ev);
}
