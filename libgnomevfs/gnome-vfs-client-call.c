#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include "gnome-vfs-client-call.h"
#include "gnome-vfs-cancellable-ops.h"

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSClientCall,
	gnome_vfs_client_call,
	GNOME_VFS_ClientCall,
	BonoboObject,
	BONOBO_TYPE_OBJECT);

static GStaticPrivate job_private = G_STATIC_PRIVATE_INIT;

static void
gnome_vfs_client_call_finalize (GObject *object)
{
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_client_call_instance_init (GnomeVFSClientCall *handle)
{
}


static void
simple_auth_callback (PortableServer_Servant _servant,
		      const CORBA_char * uri,
		      const CORBA_char * realm,
		      const CORBA_boolean previous_attempt_failed,
		      const CORBA_long auth_type,
		      CORBA_string * username,
		      CORBA_string * password,
		      CORBA_Environment * ev)
{
}


static void
gnome_vfs_client_call_class_init (GnomeVFSClientCallClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_ClientCall__epv *epv = &klass->epv;

	epv->SimpleAuthCallback = simple_auth_callback;
	
	object_class->finalize = gnome_vfs_client_call_finalize;
}

GnomeVFSClientCall *
_gnome_vfs_client_call_get (void)
{
	GnomeVFSClientCall *client_call;

	client_call = g_static_private_get (&job_private);

	if (client_call == NULL) {
		/* TODO: What about the poa?
		 * callbacks should be in this thread
		 */
		client_call = g_object_new (GNOME_TYPE_VFS_CLIENT_CALL,
					    NULL);
		g_static_private_set (&job_private,
				      client_call, (GDestroyNotify)bonobo_object_unref);
	}
	return client_call;
}
