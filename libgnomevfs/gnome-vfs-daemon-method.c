/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-daemon-method.c - Method that proxies work to the daemon

   Copyright (C) 2003 Red Hat Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Alexander Larsson <alexl@redhat.com> */

#include <config.h>
#include <libbonobo.h>
#include "gnome-vfs-client.h"
#include "gnome-vfs-client-call.h"
#include "gnome-vfs-daemon-method.h"
#include <string.h>


static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	GNOME_VFS_AsyncDaemon daemon;
	GnomeVFSClient *client;
	GnomeVFSResult res;
	CORBA_Environment ev;
	GNOME_VFS_DaemonHandle handle;
	char *uri_str;
	GnomeVFSClientCall *client_call;

	client = _gnome_vfs_get_client ();
	daemon = _gnome_vfs_client_get_async_daemon (client);
	
	if (daemon == CORBA_OBJECT_NIL)
		return GNOME_VFS_ERROR_INTERNAL;

	uri_str = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);

	client_call = _gnome_vfs_client_call_get (context);
	
	CORBA_exception_init (&ev);
	handle = CORBA_OBJECT_NIL;
	res = GNOME_VFS_AsyncDaemon_Open (daemon,
					  &handle,
					  uri_str,
					  mode,
					  BONOBO_OBJREF (client_call),
					  BONOBO_OBJREF (client),
					  &ev);

	if (handle != CORBA_OBJECT_NIL) {
		/* Don't allow reentrancy on handle method
		 * calls (except auth callbacks) */
		ORBit_object_set_policy  ((CORBA_Object) handle,
					  _gnome_vfs_get_client_policy());
	}

	_gnome_vfs_client_call_finished (client_call, context);
	
	*method_handle = (GnomeVFSMethodHandle *)handle;
	g_free (uri_str);
	
	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);
		res = GNOME_VFS_ERROR_INTERNAL;

	}

	CORBA_Object_release (daemon, NULL);
					  
	return res;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	GnomeVFSResult res;
	CORBA_Environment ev;
	GnomeVFSClientCall *client_call;
	GnomeVFSClient *client;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);
	
	client = _gnome_vfs_get_client ();
	client_call = _gnome_vfs_client_call_get (context);
	
	CORBA_exception_init (&ev);
	res = GNOME_VFS_DaemonHandle_Close ((GNOME_VFS_DaemonHandle) method_handle,
					    BONOBO_OBJREF (client_call),
					    BONOBO_OBJREF (client),
					    &ev);

	_gnome_vfs_client_call_finished (client_call, context);
	
	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);
		res = GNOME_VFS_ERROR_INTERNAL;
	}

	CORBA_Object_release ((GNOME_VFS_DaemonHandle) method_handle, NULL);
	
	return res;
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	GnomeVFSResult res;
	CORBA_Environment ev;
	GNOME_VFS_buffer *buf;
	GnomeVFSClientCall *client_call;
	GnomeVFSClient *client;
		
	client = _gnome_vfs_get_client ();
	client_call = _gnome_vfs_client_call_get (context);
	
	CORBA_exception_init (&ev);
	res = GNOME_VFS_DaemonHandle_Read ((GNOME_VFS_DaemonHandle) method_handle,
					   &buf, num_bytes,
					   BONOBO_OBJREF (client_call),
					   BONOBO_OBJREF (client),
					   &ev);

	_gnome_vfs_client_call_finished (client_call, context);
	
	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);
		res = GNOME_VFS_ERROR_INTERNAL;
	}

	*bytes_read = 0;
	if (res == GNOME_VFS_OK) {
		g_assert (buf->_length <= num_bytes);
		*bytes_read = buf->_length;
		memcpy (buffer, buf->_buffer, buf->_length);
	}

	CORBA_free (buf);
	
	return res;
}

static GnomeVFSMethod method = {
	sizeof (GnomeVFSMethod),
	do_open,
	NULL, //do_create
	do_close,
	do_read,
	/*
	do_write,
	do_seek,
	do_tell,
	do_truncate_handle,
	do_open_directory,
	do_close_directory,
	do_read_directory,
	do_get_file_info,
	do_get_file_info_from_handle,
	do_is_local,
	do_make_directory,
	do_remove_directory,
	do_move,
	do_unlink,
	do_check_same_fs,
	do_set_file_info,
	do_truncate,
	do_find_directory,
	do_create_symbolic_link,
	do_monitor_add,
	do_monitor_cancel,
	do_file_control
	*/
};


GnomeVFSMethod *
_gnome_vfs_daemon_method_get (void)
{
  return &method;
}
