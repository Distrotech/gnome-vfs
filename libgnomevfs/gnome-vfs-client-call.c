#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include "gnome-vfs-client-call.h"
#include "gnome-vfs-cancellable-ops.h"
#include "gnome-vfs-cancellation-private.h"

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
	GnomeVFSClientCall *client_call;

	client_call = GNOME_VFS_CLIENT_CALL (object);
	g_mutex_free (client_call->delay_finish_mutex);
	g_cond_free (client_call->delay_finish_cond);
	
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_client_call_instance_init (GnomeVFSClientCall *client_call)
{
	
	client_call->delay_finish_cond = g_cond_new ();
	client_call->delay_finish_mutex = g_mutex_new ();
	client_call->delay_finish = FALSE;
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
  /* DAEMON-TODO: Implement. Plus the whole auth marshalling thing needs
     to be reworked */
}


static void
gnome_vfs_client_call_class_init (GnomeVFSClientCallClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_ClientCall__epv *epv = &klass->epv;

	epv->SimpleAuthCallback = simple_auth_callback;
	
	object_class->finalize = gnome_vfs_client_call_finalize;
}

void
_gnome_vfs_client_call_delay_finish (GnomeVFSClientCall *client_call)
{
	g_mutex_lock (client_call->delay_finish_mutex);
	g_assert (!client_call->delay_finish);
	client_call->delay_finish = TRUE;
	g_mutex_unlock (client_call->delay_finish_mutex);
}

void
_gnome_vfs_client_call_delay_finish_done (GnomeVFSClientCall *client_call)
{
	g_mutex_lock (client_call->delay_finish_mutex);
	g_assert (client_call->delay_finish);
	client_call->delay_finish = FALSE;
	g_cond_signal (client_call->delay_finish_cond);
	g_mutex_unlock (client_call->delay_finish_mutex);
}

GnomeVFSClientCall *
_gnome_vfs_client_call_get (GnomeVFSContext *context)
{
	GnomeVFSClientCall *client_call;
	GnomeVFSCancellation *cancellation;
        PortableServer_POA poa;

	client_call = g_static_private_get (&job_private);

	if (client_call == NULL) {
		/* DAEMON-TODO: Verify that this poa thread hint is
		 * correct and working.
		 */
		poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_OBJECT);
		client_call = g_object_new (GNOME_TYPE_VFS_CLIENT_CALL,
					    "poa", poa,
					    NULL);
		CORBA_Object_release ((CORBA_Object)poa, NULL);
		g_static_private_set (&job_private,
				      client_call, (GDestroyNotify)bonobo_object_unref);
	}

	if (context != NULL) {
		cancellation = gnome_vfs_context_get_cancellation (context);
		if (cancellation != NULL) {
			_gnome_vfs_cancellation_add_client_call (cancellation,
								 client_call);
		}
	}
	
	return client_call;
}

void
_gnome_vfs_client_call_finished (GnomeVFSClientCall *client_call,
				 GnomeVFSContext *context)
{
	GnomeVFSCancellation *cancellation;
	
	if (context != NULL) {
		cancellation = gnome_vfs_context_get_cancellation (context);
		if (cancellation != NULL) {
			_gnome_vfs_cancellation_remove_client_call (cancellation,
								    client_call);
		}
	}

	g_mutex_lock (client_call->delay_finish_mutex);
	if (client_call->delay_finish) {
		g_cond_wait (client_call->delay_finish_cond,
			     client_call->delay_finish_mutex);
	}
	g_assert (!client_call->delay_finish);
	g_mutex_unlock (client_call->delay_finish_mutex);
}
