#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include "gnome-vfs-daemon.h"
#include "gnome-vfs-async-daemon.h"
#include "gnome-vfs-private.h"

#define QUIT_TIMEOUT (3*1000)

/* Global daemon */
static GnomeVFSDaemon *daemon = NULL;
static GnomeVFSAsyncDaemon *async_daemon = NULL;

typedef struct {
	GNOME_VFS_Client client;
	GList *outstanding_handles;
} ClientInfo;

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSDaemon,
	gnome_vfs_daemon,
	GNOME_VFS_Daemon,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


G_LOCK_DEFINE_STATIC (client_list);


static gboolean
quit_timeout (gpointer data)
{
	if (daemon->clients == NULL) {
		g_print ("All clients dead, quitting ...\n");
		bonobo_main_quit ();
	}
	return FALSE;
}

static ClientInfo *
lookup_client (GNOME_VFS_Client client)
{
	ClientInfo *client_info;
	GList *l;
	
	l = daemon->clients;
	while (l != NULL) {
		client_info = l->data;
		if (client_info->client == client)
			return client_info;
	}
	
	return NULL;
}


static void
remove_client (ORBitConnection *cnx,
	       GNOME_VFS_Client client)
{
	ClientInfo *client_info;
	
	g_signal_handlers_disconnect_by_func (
		cnx, G_CALLBACK (remove_client), client);

	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		daemon->clients = g_list_remove (daemon->clients, client_info);
		CORBA_Object_release (client_info->client, NULL);
		g_free (client_info);
	}
	G_UNLOCK (client_list);

	if (daemon->clients == NULL) 
		g_timeout_add (QUIT_TIMEOUT, quit_timeout, NULL);
}

static void
de_register_client (PortableServer_Servant servant,
		    const GNOME_VFS_Client client,
		    CORBA_Environment     *ev)
{
	remove_client (ORBit_small_get_connection (client), client);
}

static void
register_client (PortableServer_Servant servant,
		 const GNOME_VFS_Client client,
		 CORBA_Environment     *ev)
{
	ORBitConnection *cnx;
	ClientInfo *client_info;
	
	cnx = ORBit_small_get_connection (client);
	if (!cnx) {
		g_warning ("client died already !");
		return;
	}

	g_signal_connect (cnx, "broken",
			  G_CALLBACK (remove_client),
			  client);

	client_info = g_new0 (ClientInfo, 1);
	client_info->client = CORBA_Object_duplicate (client, NULL);
	
	G_LOCK (client_list);
	daemon->clients = g_list_prepend (
		daemon->clients,
		client_info);
	G_UNLOCK (client_list);
}

void
gnome_vfs_daemon_add_client_handle (const GNOME_VFS_Client client,
				    GNOME_VFS_DaemonHandle handle)
{
	ClientInfo *client_info;
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		client_info->outstanding_handles = g_list_prepend (client_info->outstanding_handles,
								   handle);
	}
	G_UNLOCK (client_list);
}

static void
gnome_vfs_daemon_finalize (GObject *object)
{
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_daemon_instance_init (GnomeVFSDaemon *daemon)
{
}

static void
gnome_vfs_daemon_class_init (GnomeVFSDaemonClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_Daemon__epv *epv = &klass->epv;

	object_class->finalize = gnome_vfs_daemon_finalize;
	
	epv->registerClient   = register_client;
	epv->deRegisterClient = de_register_client;

	gnome_vfs_init ();
}

static BonoboObject *
gnome_vfs_daemon_factory (BonoboGenericFactory *factory,
			  const char           *component_id,
			  gpointer              closure)
{
	if (!daemon) {
		daemon = g_object_new (GNOME_TYPE_VFS_DAEMON, NULL);
		bonobo_object_set_immortal (BONOBO_OBJECT (daemon), TRUE);
		
		async_daemon = g_object_new (GNOME_TYPE_VFS_ASYNC_DAEMON,
					     "poa", bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST),
					     NULL);
		bonobo_object_add_interface (BONOBO_OBJECT (daemon),
					     BONOBO_OBJECT (async_daemon));
	}
	return bonobo_object_ref (daemon);
}

int
main (int argc, char *argv [])
{
	BonoboGenericFactory *factory;
	
	if (!bonobo_init (&argc, argv)) 
		g_error (_("Could not initialize Bonobo"));

	gnome_vfs_set_is_daemon ();
	if (!gnome_vfs_init ())
		g_error (_("Could not initialize gnome vfs"));
	
	
	factory = bonobo_generic_factory_new ("OAFIID:GNOME_VFS_Daemon_Factory",
					      gnome_vfs_daemon_factory,
					      NULL);
	if (factory) {
		bonobo_main ();
		bonobo_object_unref (BONOBO_OBJECT (factory));
		
		return bonobo_debug_shutdown ();
	} else {
		g_warning ("Failed to create factory\n");
		return 1;
	}
}
