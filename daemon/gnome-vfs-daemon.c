#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include <gconf/gconf.h>
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
	GList *outstanding_dir_handles;
	GList *outstanding_contexts;
  /* DAEMON-TODO: outstanding_monitors */
} ClientInfo;

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSDaemon,
	gnome_vfs_daemon,
	GNOME_VFS_Daemon,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


/* Protects daemon->clients and their contents */
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
new_client_info (const GNOME_VFS_Client client)
{
	ClientInfo *client_info;
	
	client_info = g_new0 (ClientInfo, 1);
	client_info->client = CORBA_Object_duplicate (client, NULL);
	return client_info;
}


/* protected by client_lock */
static void
free_client_info (ClientInfo *client_info)
{
	GList *l;
	GnomeVFSDaemonHandle *handle;
	GnomeVFSDaemonDirHandle *dir_handle;
	GnomeVFSContext *context;
	GnomeVFSCancellation *cancellation;

	g_print ("free_client_info()\n");
	
	/* Cancel any outstanding operations for this client */
	for (l = client_info->outstanding_contexts; l != NULL; l = l->next) {
		context = l->data;
		cancellation = gnome_vfs_context_get_cancellation (context);
		if (cancellation) {
			gnome_vfs_cancellation_cancel (cancellation);
		}
	}

	/* Unref the handles outstanding for the client. If any
	 * operations methods are still running they are fine, because
	 * metods ref the object they correspond to while running.
	 */
	for (l = client_info->outstanding_handles; l != NULL; l = l->next) {
		handle = l->data;
		
		bonobo_object_unref (handle);
	}
	g_list_free (client_info->outstanding_handles);

	for (l = client_info->outstanding_dir_handles; l != NULL; l = l->next) {
		dir_handle = l->data;
		
		bonobo_object_unref (dir_handle);
	}
	g_list_free (client_info->outstanding_dir_handles);

	
	/* DAEMON-TODO: unref outstanding monitors (?) */
	
	CORBA_Object_release (client_info->client, NULL);
	g_free (client_info);
}

/* protected by client_list lock */
static ClientInfo *
lookup_client (GNOME_VFS_Client client)
{
	ClientInfo *client_info;
	GList *l;
	
	for (l = daemon->clients; l != NULL; l = l->next) {
		client_info = l->data;
		if (client_info->client == client)
			return client_info;
	}
	
	return NULL;
}


static void
remove_client (gpointer *cnx,
	       GNOME_VFS_Client client)
{
	ClientInfo *client_info;
	
	ORBit_small_unlisten_for_broken (client,
					 G_CALLBACK (remove_client));
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info != NULL) {
		daemon->clients = g_list_remove (daemon->clients, client_info);
		free_client_info (client_info);
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
	remove_client (NULL, client);
}

static void
register_client (PortableServer_Servant servant,
		 const GNOME_VFS_Client client,
		 CORBA_Environment     *ev)
{
	ORBitConnectionStatus status;
	ClientInfo *client_info;
	

	status = ORBit_small_listen_for_broken (client, 
						G_CALLBACK (remove_client),
						client);
	if (status != ORBIT_CONNECTION_CONNECTED) {
		g_warning ("client died already !");
		return;
	}

	client_info = new_client_info (client);
	
	G_LOCK (client_list);
	daemon->clients = g_list_prepend (daemon->clients,
					  client_info);
	G_UNLOCK (client_list);
}

void
gnome_vfs_daemon_add_context (const GNOME_VFS_Client client,
			      GnomeVFSContext *context)
{
	
	ClientInfo *client_info;
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		client_info->outstanding_contexts = g_list_prepend (client_info->outstanding_contexts,
								    context);
	}
	G_UNLOCK (client_list);
}

void
gnome_vfs_daemon_remove_context (const GNOME_VFS_Client client,
				 GnomeVFSContext *context)
{
	
	ClientInfo *client_info;
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		client_info->outstanding_contexts = g_list_remove (client_info->outstanding_contexts,
								   context);
	}
	G_UNLOCK (client_list);
}


void
gnome_vfs_daemon_add_client_handle (const GNOME_VFS_Client client,
				    GnomeVFSDaemonHandle *handle)
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

void
gnome_vfs_daemon_remove_client_handle (const GNOME_VFS_Client client,
				       GnomeVFSDaemonHandle *handle)
{
	ClientInfo *client_info;
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		client_info->outstanding_handles = g_list_remove (client_info->outstanding_handles,
								  handle);
	}
	G_UNLOCK (client_list);
}


void
gnome_vfs_daemon_add_client_dir_handle (const GNOME_VFS_Client client,
					GnomeVFSDaemonDirHandle *handle)
{
	ClientInfo *client_info;
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		client_info->outstanding_dir_handles = g_list_prepend (client_info->outstanding_dir_handles,
								       handle);
	}
	G_UNLOCK (client_list);
}

void
gnome_vfs_daemon_remove_client_dir_handle (const GNOME_VFS_Client client,
					   GnomeVFSDaemonDirHandle *handle)
{
	ClientInfo *client_info;
	
	G_LOCK (client_list);
	client_info = lookup_client (client);
	if (client_info) {
		client_info->outstanding_dir_handles = g_list_remove (client_info->outstanding_dir_handles,
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
	bonobo_object_set_immortal (BONOBO_OBJECT (daemon), TRUE);
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
        PortableServer_POA poa;
	
	if (!daemon) {
		daemon = g_object_new (GNOME_TYPE_VFS_DAEMON, NULL);

		poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST);
		async_daemon = g_object_new (GNOME_TYPE_VFS_ASYNC_DAEMON,
					     "poa", poa,
					     NULL);
		CORBA_Object_release ((CORBA_Object)poa, NULL);
		bonobo_object_add_interface (BONOBO_OBJECT (daemon),
					     BONOBO_OBJECT (async_daemon));
	}
	return BONOBO_OBJECT (daemon);
}

int
main (int argc, char *argv [])
{
	BonoboGenericFactory *factory;
	
	if (!bonobo_init (&argc, argv)) {
		g_error (_("Could not initialize Bonobo"));
		return 1;
	}

	gnome_vfs_set_is_daemon ();
	if (!gnome_vfs_init ()) {
		g_error (_("Could not initialize gnome vfs"));
		return 1;
		}
	
	factory = bonobo_generic_factory_new ("OAFIID:GNOME_VFS_Daemon_Factory",
					      gnome_vfs_daemon_factory,
					      NULL);
	
	if (factory) {
		g_print ("starting vfs daemon - Main thread: %p\n", g_thread_self());
		bonobo_main ();
		
		bonobo_object_unref (BONOBO_OBJECT (factory));

		if (daemon) {
			bonobo_object_set_immortal (BONOBO_OBJECT (daemon), FALSE);
			bonobo_object_unref (BONOBO_OBJECT (daemon));
		}

		gnome_vfs_shutdown();
		gconf_debug_shutdown();
		return bonobo_debug_shutdown ();
	} else {
		g_warning ("Failed to create factory\n");
		return 1;
	}
}
