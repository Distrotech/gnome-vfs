#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include "gnome-vfs-async-daemon.h"
#include "gnome-vfs-cancellable-ops.h"
#include "gnome-vfs-daemon-handle.h"

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSAsyncDaemon,
	gnome_vfs_async_daemon,
	GNOME_VFS_AsyncDaemon,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


static void
gnome_vfs_async_daemon_finalize (GObject *object)
{
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_async_daemon_instance_init (GnomeVFSAsyncDaemon *daemon)
{
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
	  
	g_print ("gnome_vfs_async_daemon_open(%s)\n", uri_str);
	
	*handle_return = NULL;

	uri = gnome_vfs_uri_new (uri_str);
	if (uri == NULL)
		return GNOME_VFS_ERROR_INVALID_URI;

	/* TODO:
	 * create cancellation object, add hash client_call->cancellation object
	 */

	res = gnome_vfs_open_uri_cancellable (&vfs_handle,
					      uri, open_mode,
					      NULL /*context*/);
	handle = gnome_vfs_daemon_handle_new (vfs_handle);

	*handle_return = BONOBO_OBJREF (handle);
	
	/* TODO:
	 * Remove cancellation object from hashtable
	 */

	gnome_vfs_uri_unref (uri);

	return res;
}

static void
gnome_vfs_async_daemon_class_init (GnomeVFSAsyncDaemonClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_AsyncDaemon__epv *epv = &klass->epv;

	epv->Open = gnome_vfs_async_daemon_open;
	
	object_class->finalize = gnome_vfs_async_daemon_finalize;
}
