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

static void
do_register (GnomeVfsClient    *client,
	     GNOME_VFS_Daemon   daemon,
	     CORBA_Environment *ev)
{
	GNOME_VFS_Daemon_registerClient (daemon, BONOBO_OBJREF (client), ev);
}


static void
do_deregister (GnomeVfsClient    *client,
	       GNOME_VFS_Daemon   daemon,
	       CORBA_Environment *ev)
{
	GNOME_VFS_Daemon_deRegisterClient (daemon, BONOBO_OBJREF (client), ev);
}

int
main (int argc, char **argv)
{
	GnomeVfsClient    *client;
	GNOME_VFS_Daemon   daemon;
	CORBA_Environment *ev, real_ev;

	if (!bonobo_init (&argc, argv)) {
		g_error ("Failed to initialize");
	}
	
	client = g_object_new (GNOME_TYPE_VFS_CLIENT, NULL);

	CORBA_exception_init (&real_ev); ev = &real_ev;

	if (!(daemon = bonobo_activation_activate_from_id (
		"OAFIID:GNOME_VFS_Daemon", 0, NULL, ev))) {
		g_error ("Failed to activate the daemon '%s'",
			 bonobo_exception_get_text (ev));
	}

	do_register (client, daemon, ev);

	GNOME_VFS_Daemon_setPassword (daemon, "ftp://titi:toto@home/teuf/titi", "user1", "password1",  ev);
	GNOME_VFS_Daemon_setPassword (daemon, "ftp://tutu:toto@home/teuf/titi", "user2", "password2",  ev);
	GNOME_VFS_Daemon_setPassword (daemon, "ftp://home", "user3", "password3",  ev);

	{
		CORBA_char *str;
		str = GNOME_VFS_Daemon_getPassword (daemon, "ftp://home/teuf/titi", "user1",  ev);
		if (!BONOBO_EX (ev)) {
			g_print ("Got password '%s'\n", str);
		} else {
			g_error (bonobo_exception_get_text (ev));
		}
		str = GNOME_VFS_Daemon_getPassword (daemon, "ftp://home/teuf/titi", "user2",  ev);
		if (!BONOBO_EX (ev)) {
			g_print ("Got password '%s'\n", str);
		} else {
			g_error (bonobo_exception_get_text (ev));
		}
		str = GNOME_VFS_Daemon_getPassword (daemon, "ftp://home", "user3",  ev);
		if (!BONOBO_EX (ev)) {
			g_print ("Got password '%s'\n", str);
		} else {
			g_error (bonobo_exception_get_text (ev));
		}

	}
	
	do_deregister (client, daemon, ev);

	bonobo_object_unref (client);
	CORBA_Object_release (daemon, NULL);

	g_message ("Clean client quit");
	return bonobo_debug_shutdown ();
}
