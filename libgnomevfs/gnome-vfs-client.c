#include <unistd.h>
#include <bonobo-activation/bonobo-activation.h>
#include <libbonobo.h>
#include "gnome-vfs-client.h"
#include "gnome-vfs-cancellation-private.h"

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSClient,
	gnome_vfs_client,
	GNOME_VFS_Client,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


struct _GnomeVFSClientPrivate {
	GNOME_VFS_Daemon daemon;
	GNOME_VFS_AsyncDaemon async_daemon;
};

static void activate_daemon (GnomeVFSClient *client);

static GnomeVFSClient *the_client = NULL;
G_LOCK_DEFINE_STATIC (the_client);

static void
gnome_vfs_client_finalize (GObject *object)
{
	GnomeVFSClient *client = GNOME_VFS_CLIENT (object);
	
	g_free (client->priv);
	
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_client_instance_init (GnomeVFSClient *client)
{
	client->priv = g_new0 (GnomeVFSClientPrivate, 1);
}

static void
gnome_vfs_client_monitor_callback (PortableServer_Servant _servant,
				   const GNOME_VFS_DaemonMonitor monitor,
				   const CORBA_char * monitor_uri,
				   const CORBA_char * info_uri,
				   const CORBA_long event_type,
				   CORBA_Environment * ev)
{
  
}

static void
gnome_vfs_client_class_init (GnomeVFSClientClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_Client__epv *epv = &klass->epv; 

	epv->MonitorCallback = gnome_vfs_client_monitor_callback;
	
	object_class->finalize = gnome_vfs_client_finalize;
}


static void
daemon_connection_broken (gpointer connection,
			  GnomeVFSClient *client)
{
	/* This is run in an idle, so some code might run between the
	 * connection going bork and this code running.
	 */
	G_LOCK (the_client);
	CORBA_Object_release (client->priv->daemon, NULL);
	client->priv->daemon = CORBA_OBJECT_NIL;
	CORBA_Object_release (client->priv->async_daemon, NULL);
	client->priv->async_daemon = CORBA_OBJECT_NIL;

	/* DAEMON-TODO: Free all objects tied to the daemon:
	 * DaemonMonitor - free, mark for recreation on daemon reconnect
	 * DaemonHandles - Calling these will keep giving I/O errors and they
	 * will be freed on close of the corresponding handle close.
	 */
	
	G_UNLOCK (the_client);
}


static void
activate_daemon (GnomeVFSClient *client)
{
	CORBA_Environment  ev;
	
	CORBA_exception_init (&ev);
	/* DAEMON-TODO: This call isn't really threadsafe */
	client->priv->daemon = bonobo_activation_activate_from_id ("OAFIID:GNOME_VFS_Daemon",
								   0, NULL, &ev);
	CORBA_exception_free (&ev);
	
	if (client->priv->daemon != CORBA_OBJECT_NIL) {
		CORBA_exception_init (&ev);
		GNOME_VFS_Daemon_registerClient (client->priv->daemon, BONOBO_OBJREF (client), &ev);
		/* If the registration fails for some reason we release the
		 * daemon object and return NIL. */
		if (BONOBO_EX (&ev)) {
			CORBA_exception_free (&ev);
			CORBA_Object_release (client->priv->daemon, NULL);
			client->priv->daemon = CORBA_OBJECT_NIL;
		}
	}
	
	if (client->priv->daemon != CORBA_OBJECT_NIL) {
		ORBit_small_listen_for_broken (client->priv->daemon, G_CALLBACK (daemon_connection_broken), client);
		
		/* DAEMON-TODO: Set up monitors that were up before a previous daemon disconnected */
	}
}

/**
 * gnome_vfs_client_get_daemon:
 * @client: The client object
 *
 * Returns a local duplicate of the daemon reference.
 * The client is guaranteed to be registred with the daemon.
 * May return CORBA_OBJECT_NIL. May return an object where the
 * connection has died. Safe to call from a thread. 
 */
GNOME_VFS_Daemon
_gnome_vfs_client_get_daemon (GnomeVFSClient *client)
{
	GNOME_VFS_Daemon daemon;

	G_LOCK (the_client);
	
	if (client->priv->daemon == CORBA_OBJECT_NIL)
		activate_daemon (client);

	if (client->priv->daemon != CORBA_OBJECT_NIL) 
		daemon = CORBA_Object_duplicate (client->priv->daemon, NULL);
	else
		daemon = CORBA_OBJECT_NIL;
	
	G_UNLOCK (the_client);
	
	return daemon;
}

/**
 * gnome_vfs_client_get_async_daemon:
 * @client: The client object
 *
 * Returns a local duplicate of the asyncdaemon reference.
 * The client is guaranteed to be registred with the daemon.
 * May return CORBA_OBJECT_NIL. May return an object where the
 * connection has died. Safe to call from a thread. 
 */
GNOME_VFS_AsyncDaemon
_gnome_vfs_client_get_async_daemon (GnomeVFSClient *client)
{
	GNOME_VFS_AsyncDaemon async_daemon;
	CORBA_Environment  ev;
	
	G_LOCK (the_client);

	async_daemon = CORBA_OBJECT_NIL;
	if (client->priv->async_daemon == CORBA_OBJECT_NIL) {
		if (client->priv->daemon == CORBA_OBJECT_NIL)
			activate_daemon (client);

		if (client->priv->daemon != CORBA_OBJECT_NIL) {
			CORBA_exception_init (&ev);
			client->priv->async_daemon = Bonobo_Unknown_queryInterface
				(client->priv->daemon, "IDL:GNOME/VFS/AsyncDaemon:1.0", &ev);
			if (client->priv->async_daemon == CORBA_OBJECT_NIL) {
				CORBA_exception_free (&ev);
				g_warning ("Failed to get async daemon interface");
			}
		}
	}
	
	if (client->priv->async_daemon != CORBA_OBJECT_NIL) {
		async_daemon = CORBA_Object_duplicate (client->priv->async_daemon, NULL);
	}

	G_UNLOCK (the_client);
	
	return async_daemon;
}


/* Returns local singleton object */
GnomeVFSClient *
_gnome_vfs_get_client (void)
{
	G_LOCK (the_client);
	if (the_client == NULL) 
		the_client = g_object_new (GNOME_TYPE_VFS_CLIENT, NULL);
	G_UNLOCK (the_client);
	
	return the_client;
}

void
_gnome_vfs_client_shutdown (void)
{
}
