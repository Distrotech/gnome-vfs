#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include "gnome-vfs-daemon.h"

/* Global daemon */
static GnomeVfsDaemon *daemon = NULL;

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVfsDaemon,
	gnome_vfs_daemon,
	GNOME_VFS_Daemon,
	BonoboObject,
	BONOBO_TYPE_OBJECT);

static void
set_password (PortableServer_Servant servant,
	      const CORBA_char      *key,
	      const CORBA_char      *passwd,
	      CORBA_Environment     *ev)
{
	g_warning ("Set password '%s' to '%s'", key, passwd);
}

static CORBA_string
get_password (PortableServer_Servant _servant,
	      const CORBA_char * key,
	      CORBA_Environment * ev)
{
	g_warning ("Get password '%s'", key);
	return CORBA_string_dup ("Frobnicate");
}

static void
remove_client (ORBitConnection *cnx,
	       GNOME_VFS_Client client)
{
	g_signal_handlers_disconnect_by_func (
		cnx, G_CALLBACK (remove_client), client);

	daemon->clients = g_list_remove (daemon->clients, client);
	CORBA_Object_release (client, NULL);

	if (!daemon->clients) {
		/* FIXME: timeout / be more clever here ... */
		g_warning ("All clients dead, quitting ...");
		bonobo_main_quit ();
	}
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
	
	cnx = ORBit_small_get_connection (client);
	if (!cnx) {
		g_warning ("client died already !");
		return;
	}

	g_signal_connect (cnx, "broken",
			  G_CALLBACK (remove_client),
			  client);

	daemon->clients = g_list_prepend (
		daemon->clients,
		CORBA_Object_duplicate (client, NULL));
}

static void
gnome_vfs_daemon_finalize (GObject *object)
{
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_daemon_instance_init (GnomeVfsDaemon *daemon)
{
}

static void
gnome_vfs_daemon_class_init (GnomeVfsDaemonClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_Daemon__epv *epv = &klass->epv;

	object_class->finalize = gnome_vfs_daemon_finalize;
	
	epv->getPassword  = get_password;
	epv->setPassword  = set_password;
	epv->registerClient   = register_client;
	epv->deRegisterClient = de_register_client;
}

static BonoboObject *
gnome_vfs_daemon_factory (BonoboGenericFactory *factory,
			  const char           *component_id,
			  gpointer              closure)
{
	if (!daemon) {
		daemon = g_object_new (GNOME_TYPE_VFS_DAEMON, NULL);
		bonobo_object_set_immortal (BONOBO_OBJECT (daemon), TRUE);
	}
	return bonobo_object_ref (daemon);
}

BONOBO_ACTIVATION_FACTORY("OAFIID:GNOME_VFS_Daemon_Factory",
			  "gnome-vfs-daemon", "0.1",
			  gnome_vfs_daemon_factory,
			  NULL);
