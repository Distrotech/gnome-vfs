#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs.h>
#include "gnome-vfs-daemon-handle.h"
#include "gnome-vfs-cancellable-ops.h"
#include "gnome-vfs-daemon.h"
#include "gnome-vfs-async-daemon.h"

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVFSDaemonHandle,
	gnome_vfs_daemon_handle,
	GNOME_VFS_DaemonHandle,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


static void
gnome_vfs_daemon_handle_finalize (GObject *object)
{
	GnomeVFSDaemonHandle *handle;

	handle = GNOME_VFS_DAEMON_HANDLE (object);
	g_print ("gnome_vfs_daemon_handle_finalize()\n");
	
        if (handle->real_handle != NULL) {
		gnome_vfs_close_cancellable (handle->real_handle, NULL);
	}
	g_mutex_free (handle->mutex);
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_daemon_handle_instance_init (GnomeVFSDaemonHandle *handle)
{
	handle->mutex = g_mutex_new ();
}

static GNOME_VFS_Result
gnome_vfs_daemon_handle_read (PortableServer_Servant _servant,
			      GNOME_VFS_buffer ** _buf,
			      const GNOME_VFS_FileSize num_bytes,
			      const GNOME_VFS_ClientCall client_call,
			      const GNOME_VFS_Client client,
			      CORBA_Environment * ev)
{
	GnomeVFSResult res;
	GnomeVFSDaemonHandle *handle;
	GnomeVFSFileSize bytes_written;
	GNOME_VFS_buffer * buf;
	GnomeVFSContext *context;

	buf = CORBA_sequence_CORBA_octet__alloc ();
	*_buf = buf;
	
	buf->_buffer = CORBA_sequence_CORBA_octet_allocbuf (num_bytes);
	buf->_length = 0;
	buf->_maximum = num_bytes;
	
	handle = GNOME_VFS_DAEMON_HANDLE (bonobo_object_from_servant (_servant));
	g_print ("gnome_vfs_daemon_handle_read(%p) - thread %p\n", handle, g_thread_self());

	context = gnome_vfs_async_daemon_get_context (client_call, client);
	
	res = gnome_vfs_read_cancellable (handle->real_handle,
					  buf->_buffer,
					  num_bytes,
					  &bytes_written,
					  context);
	
	gnome_vfs_async_daemon_drop_context (client_call, client, context);

	buf->_length = bytes_written;

	return res;
}


static GNOME_VFS_Result
gnome_vfs_daemon_handle_close (PortableServer_Servant _servant,
			       const GNOME_VFS_ClientCall client_call,
			       const GNOME_VFS_Client client,
			       CORBA_Environment * ev)
{
	GnomeVFSDaemonHandle *handle;
	GnomeVFSResult res;
	GnomeVFSContext *context;

	handle = GNOME_VFS_DAEMON_HANDLE (bonobo_object_from_servant (_servant));
	g_print ("gnome_vfs_daemon_handle_close(%p) - thread: %p\n", handle, g_thread_self());

	context = gnome_vfs_async_daemon_get_context (client_call, client);
	
	res = gnome_vfs_close_cancellable (handle->real_handle,
					   context);
	
	gnome_vfs_async_daemon_drop_context (client_call, client, context);
	
	if (res == GNOME_VFS_OK) {
		handle->real_handle = NULL;
		
		/* The client is now finished with the handle,
		   remove it from the list and free it */
		gnome_vfs_daemon_remove_client_handle (client,
						       handle);
		bonobo_object_unref (handle);
	}
	
	return res;
}

static void
gnome_vfs_daemon_handle_class_init (GnomeVFSDaemonHandleClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_DaemonHandle__epv *epv = &klass->epv;

	epv->Read = gnome_vfs_daemon_handle_read;
	epv->Close = gnome_vfs_daemon_handle_close;
	
	object_class->finalize = gnome_vfs_daemon_handle_finalize;
}

GnomeVFSDaemonHandle *
gnome_vfs_daemon_handle_new (GnomeVFSHandle *real_handle)
{
	GnomeVFSDaemonHandle *daemon_handle;
        PortableServer_POA poa;

	poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST);
	daemon_handle = g_object_new (GNOME_TYPE_VFS_DAEMON_HANDLE,
				      "poa", poa,
				      NULL);
	CORBA_Object_release ((CORBA_Object)poa, NULL);
	daemon_handle->real_handle = real_handle;

	return daemon_handle;
}
