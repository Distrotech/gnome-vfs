#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include "gnome-vfs-async-daemon.h"
#include "gnome-vfs-cancellable-ops.h"
#include "gnome-vfs-daemon-handle.h"
#include "gnome-vfs-client-call.h"
#include "gnome-vfs-daemon.h"
#include <unistd.h>

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSAsyncDaemon,
	gnome_vfs_async_daemon,
	GNOME_VFS_AsyncDaemon,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


/* Protects the client_call_context hashtable, and the existance of
 *  the context object that has been looked up */
G_LOCK_DEFINE_STATIC (client_call_context);

static GnomeVFSAsyncDaemon *async_daemon = NULL;

static void
gnome_vfs_async_daemon_finalize (GObject *object)
{
	/* All client calls should have finished before we kill this object */
	g_assert (g_hash_table_size (async_daemon->client_call_context) == 0);
	g_hash_table_destroy (async_daemon->client_call_context);
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
	async_daemon = NULL;
}

static void
gnome_vfs_async_daemon_instance_init (GnomeVFSAsyncDaemon *daemon)
{
	daemon->client_call_context = g_hash_table_new (NULL, NULL);
	async_daemon = daemon;
}

GnomeVFSContext *
gnome_vfs_async_daemon_get_context (const GNOME_VFS_ClientCall client_call,
				    const GNOME_VFS_Client client)
{
	GnomeVFSContext *context;

	if (async_daemon == NULL) {
		return NULL;
	}
	
	context = gnome_vfs_context_new ();
	G_LOCK (client_call_context);
	g_hash_table_insert (async_daemon->client_call_context, client_call, context);
	G_UNLOCK (client_call_context);

	gnome_vfs_daemon_add_context (client, context);
	_gnome_vfs_daemon_set_current_daemon_client_call (client_call);

	return context;
}

void
gnome_vfs_async_daemon_drop_context (const GNOME_VFS_ClientCall client_call,
				     const GNOME_VFS_Client client,
				     GnomeVFSContext *context)
{
	if (context != NULL) {
		_gnome_vfs_daemon_set_current_daemon_client_call (NULL);
		gnome_vfs_daemon_remove_context (client, context);
		G_LOCK (client_call_context);
		if (async_daemon != NULL) {
			g_hash_table_remove (async_daemon->client_call_context, client_call);
		}
		gnome_vfs_context_free (context);
		G_UNLOCK (client_call_context);
	}
}

static GNOME_VFS_Result
gnome_vfs_async_daemon_open (PortableServer_Servant _servant,
			     GNOME_VFS_DaemonHandle * handle_return,
			     const CORBA_char * uri_str,
			     const CORBA_long open_mode,
			     const GNOME_VFS_ClientCall client_call,
			     const GNOME_VFS_Client client,
			     CORBA_Environment * ev)
{
	GnomeVFSURI *uri;
	GnomeVFSHandle *vfs_handle;
	GnomeVFSResult res;
	GnomeVFSContext *context;
	GnomeVFSDaemonHandle *handle;
	
	g_print ("gnome_vfs_async_daemon_open(%s) - thread %p\n", uri_str, g_thread_self());
	
	*handle_return = NULL;

	uri = gnome_vfs_uri_new (uri_str);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	context = gnome_vfs_async_daemon_get_context (client_call, client);

	res = gnome_vfs_open_uri_cancellable (&vfs_handle,
					      uri, open_mode,
					      context);
	g_print ("res: %d\n", res);

	if (res == GNOME_VFS_OK) {
		handle = gnome_vfs_daemon_handle_new (vfs_handle);
		g_print ("handle = %p\n", handle);
		*handle_return = CORBA_Object_duplicate (BONOBO_OBJREF (handle), NULL);
		gnome_vfs_daemon_add_client_handle (client, handle);
	}
	
	gnome_vfs_async_daemon_drop_context (client_call, client, context);

	gnome_vfs_uri_unref (uri);
	
	return res;
}

static gboolean
cancel_client_call_callback (gpointer data)
{
	GnomeVFSContext *context;
	GnomeVFSCancellation *cancellation;
	GNOME_VFS_ClientCall client_call;

	client_call = data;

	G_LOCK (client_call_context);
	context = g_hash_table_lookup (async_daemon->client_call_context, client_call);
	if (context != NULL) {
		cancellation = gnome_vfs_context_get_cancellation (context);
		if (cancellation) {
			/* context + cancellation is guaranteed to live until
			 * the client_call exists, and it hasn't, since we
			 * looked up the context and haven't dropped the lock
			 * yet.
			 */
			gnome_vfs_cancellation_cancel (cancellation);
		}
	}
	G_UNLOCK (client_call_context);


	CORBA_Object_release (client_call, NULL);
	return FALSE;
}

static void
gnome_vfs_async_daemon_cancel (PortableServer_Servant _servant,
			       const GNOME_VFS_ClientCall client_call,
			       CORBA_Environment * ev)
{
	g_print ("gnome_vfs_async_daemon_cancel(%p) - thread %p\n", client_call, g_thread_self());
	
	/* Ref the client_call so it won't go away if the call finishes while
	 * waiting for the idle */
	CORBA_Object_duplicate (client_call, NULL);

	g_idle_add (cancel_client_call_callback, client_call);

}


static void
gnome_vfs_async_daemon_class_init (GnomeVFSAsyncDaemonClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_AsyncDaemon__epv *epv = &klass->epv;

	epv->Open = gnome_vfs_async_daemon_open;
	epv->Cancel = gnome_vfs_async_daemon_cancel;
	
	object_class->finalize = gnome_vfs_async_daemon_finalize;
}
