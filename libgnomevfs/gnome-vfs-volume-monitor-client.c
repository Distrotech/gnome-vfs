/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-volume-monitor-client.c - client implementation of volume handling

   Copyright (C) 2003 Red Hat, Inc

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Alexander Larsson <alexl@redhat.com>
*/

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "gnome-vfs-volume-monitor-client.h"
#include "gnome-vfs-volume-monitor-private.h"
#include "gnome-vfs-cdrom.h"
#include "gnome-vfs-filesystem-type.h"

#include "gnome-vfs-dbus-utils.h"

#define d(x) 

static void gnome_vfs_volume_monitor_client_class_init (GnomeVFSVolumeMonitorClientClass *klass);
static void gnome_vfs_volume_monitor_client_init       (GnomeVFSVolumeMonitorClient      *volume_monitor_client);
static void gnome_vfs_volume_monitor_client_finalize   (GObject                          *object);

static void setup_dbus_connection    (GnomeVFSVolumeMonitorClient *volume_monitor_client);
static void shutdown_dbus_connection (GnomeVFSVolumeMonitorClient *volume_monitor_client);

static GnomeVFSVolumeMonitorClass *parent_class = NULL;

GType
gnome_vfs_volume_monitor_client_get_type (void)
{
	static GType volume_monitor_client_type = 0;

	if (!volume_monitor_client_type) {
		static const GTypeInfo volume_monitor_client_info = {
			sizeof (GnomeVFSVolumeMonitorClientClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gnome_vfs_volume_monitor_client_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GnomeVFSVolumeMonitorClient),
			0,              /* n_preallocs */
			(GInstanceInitFunc) gnome_vfs_volume_monitor_client_init
		};
		
		volume_monitor_client_type =
			g_type_register_static (GNOME_VFS_TYPE_VOLUME_MONITOR, "GnomeVFSVolumeMonitorClient",
						&volume_monitor_client_info, 0);
	}
	
	return volume_monitor_client_type;
}

static void
gnome_vfs_volume_monitor_client_class_init (GnomeVFSVolumeMonitorClientClass *class)
{
	GObjectClass *o_class;

	parent_class = g_type_class_peek_parent (class);
	
	o_class = (GObjectClass *) class;

	/* GObject signals */
	o_class->finalize = gnome_vfs_volume_monitor_client_finalize;
}

static void
read_drives_from_daemon (GnomeVFSVolumeMonitorClient *volume_monitor_client)
{
	GnomeVFSVolumeMonitor *volume_monitor;
	GnomeVFSDrive         *drive;
	GList                 *list, *l;

	volume_monitor = GNOME_VFS_VOLUME_MONITOR (volume_monitor_client);

	list = _gnome_vfs_dbus_utils_get_drives (volume_monitor_client->dbus_conn, volume_monitor);
	for (l = list; l; l = l->next) {
		drive = l->data;

		_gnome_vfs_volume_monitor_connected (volume_monitor, drive);
		gnome_vfs_drive_unref (drive);
	}

	g_list_free (list);
}

static void
read_volumes_from_daemon (GnomeVFSVolumeMonitorClient *volume_monitor_client)
{
	GnomeVFSVolumeMonitor *volume_monitor;
	GnomeVFSVolume        *volume;
	GList                 *list, *l;

	volume_monitor = GNOME_VFS_VOLUME_MONITOR (volume_monitor_client);

	list = _gnome_vfs_dbus_utils_get_volumes (volume_monitor_client->dbus_conn, volume_monitor);
	for (l = list; l; l = l->next) {
		volume = l->data;
		
		_gnome_vfs_volume_monitor_mounted (volume_monitor, volume);
		gnome_vfs_volume_unref (volume);
	}

	g_list_free (list);
}

static void
gnome_vfs_volume_monitor_client_init (GnomeVFSVolumeMonitorClient *volume_monitor_client)
{
	setup_dbus_connection (volume_monitor_client);
	read_drives_from_daemon (volume_monitor_client);
	read_volumes_from_daemon (volume_monitor_client);
}

/* Remeber that this could be running on a thread other
 * than the main thread */
static void
gnome_vfs_volume_monitor_client_finalize (GObject *object)
{
	GnomeVFSVolumeMonitorClient *volume_monitor_client;

	volume_monitor_client = GNOME_VFS_VOLUME_MONITOR_CLIENT (object);

	g_assert (volume_monitor_client->is_shutdown);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

void
_gnome_vfs_volume_monitor_client_daemon_died (GnomeVFSVolumeMonitorClient *volume_monitor_client)
{
	GnomeVFSVolumeMonitor *volume_monitor;

	volume_monitor = GNOME_VFS_VOLUME_MONITOR (volume_monitor_client);
	
	_gnome_vfs_volume_monitor_unmount_all (volume_monitor);
	_gnome_vfs_volume_monitor_disconnect_all (volume_monitor);

	read_drives_from_daemon (volume_monitor_client);
	read_volumes_from_daemon (volume_monitor_client);
}

void
gnome_vfs_volume_monitor_client_shutdown_private (GnomeVFSVolumeMonitorClient *volume_monitor_client)
{
	if (volume_monitor_client->is_shutdown)
		return;
	
	volume_monitor_client->is_shutdown = TRUE;

	shutdown_dbus_connection (volume_monitor_client);
}

#define DAEMON_SIGNAL_RULE \
  "type='signal',sender='org.gnome.GnomeVFS.Daemon',interface='org.gnome.GnomeVFS.Daemon'"

#define NAME_OWNER_CHANGED_SIGNAL_RULE \
  "type='signal',sender='" DBUS_SERVICE_DBUS "',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='org.gnome.GnomeVFS.Daemon'"

static guint retry_timeout_id = 0;


/* Should only be called from the main thread. */
static gboolean
dbus_try_activate_daemon_helper (GnomeVFSVolumeMonitorClient *volume_monitor)
{
	DBusError error;

	if (volume_monitor->is_shutdown) {
		/* If we're shutdown, we don't want to retry, so we treat this
		 * as success.
		 */
		return TRUE;
	}
	
	d(g_print ("Try activating daemon.\n"));

	dbus_error_init (&error);
	if (!dbus_bus_start_service_by_name (volume_monitor->dbus_conn,
					     DVD_DAEMON_SERVICE,
					     0,
					     NULL,
					     &error)) {
		g_warning ("Failed to re-activate daemon: %s", error.message);
		dbus_error_free (&error);
	} else {
		/* Succeeded, reload drives/volumes. */
		_gnome_vfs_volume_monitor_client_daemon_died (volume_monitor);

		return TRUE;
	}

	return FALSE;
}

static gboolean 
dbus_try_activate_daemon_timeout_func (gpointer data)
{
	GnomeVFSVolumeMonitorClient *volume_monitor;

	volume_monitor = GNOME_VFS_VOLUME_MONITOR_CLIENT (gnome_vfs_get_volume_monitor ());

	if (volume_monitor->is_shutdown) {
		retry_timeout_id = 0;
		return FALSE;
	}

	if (dbus_try_activate_daemon_helper (volume_monitor)) {
		retry_timeout_id = 0;
		return FALSE;
	}

	/* Try again. */
	return TRUE;
}

/* Will re-try every 5 seconds until succeeding. */
static void
dbus_try_activate_daemon (GnomeVFSVolumeMonitorClient *volume_monitor)
{
	if (retry_timeout_id != 0) {
		return;
	}
	
	if (dbus_try_activate_daemon_helper (volume_monitor)) {
		return;
	}

	/* We failed to activate the daemon. This should only happen if the
	 * daemon has been explicitly killed by the user or some OOM
	 * functionality just after we tried to activate it. We try again in 5
	 * seconds.
	 */
	retry_timeout_id = g_timeout_add (5000, dbus_try_activate_daemon_timeout_func, NULL);
}

static DBusHandlerResult
dbus_filter_func (DBusConnection *connection,
		  DBusMessage    *message,
		  void           *data)
{
	GnomeVFSVolumeMonitor *volume_monitor;
	GnomeVFSVolume        *volume;
	GnomeVFSDrive         *drive;
	DBusMessageIter        iter;
	dbus_int32_t           id;

	volume_monitor = gnome_vfs_get_volume_monitor ();
	if (GNOME_VFS_VOLUME_MONITOR_CLIENT (volume_monitor)->is_shutdown) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	
	if (dbus_message_is_signal (message,
				    DVD_DAEMON_INTERFACE,
				    DVD_DAEMON_VOLUME_MOUNTED_SIGNAL)) {
		dbus_message_iter_init (message, &iter);
		volume = _gnome_vfs_volume_from_dbus (&iter, volume_monitor);
		_gnome_vfs_volume_monitor_mounted (volume_monitor, volume);
		gnome_vfs_volume_unref (volume);
	}
	else if (dbus_message_is_signal (message,
					 DVD_DAEMON_INTERFACE,
					 DVD_DAEMON_VOLUME_PRE_UNMOUNT_SIGNAL)) {
		if (dbus_message_get_args (message, NULL,
					   DBUS_TYPE_INT32, &id,
					   DBUS_TYPE_INVALID)) {
			volume = gnome_vfs_volume_monitor_get_volume_by_id (volume_monitor, id);
			if (volume != NULL) {
				gnome_vfs_volume_monitor_emit_pre_unmount (volume_monitor,
									   volume);
				gnome_vfs_volume_unref (volume);
			}
		}
	}
	else if (dbus_message_is_signal (message,
					 DVD_DAEMON_INTERFACE,
					 DVD_DAEMON_VOLUME_UNMOUNTED_SIGNAL)) {
		if (dbus_message_get_args (message, NULL,
					   DBUS_TYPE_INT32, &id,
					   DBUS_TYPE_INVALID)) {
			volume = gnome_vfs_volume_monitor_get_volume_by_id (volume_monitor, id);
			if (volume != NULL) {
				_gnome_vfs_volume_monitor_unmounted (volume_monitor, volume);
				gnome_vfs_volume_unref (volume);
			}
		}
	}
	else if (dbus_message_is_signal (message,
					 DVD_DAEMON_INTERFACE,
					 DVD_DAEMON_DRIVE_CONNECTED_SIGNAL)) {
		dbus_message_iter_init (message, &iter);		
		drive = _gnome_vfs_drive_from_dbus (&iter, volume_monitor);
		_gnome_vfs_volume_monitor_connected (volume_monitor, drive);
		gnome_vfs_drive_unref (drive);
	}
	else if (dbus_message_is_signal (message,
					 DVD_DAEMON_INTERFACE,
					 DVD_DAEMON_DRIVE_DISCONNECTED_SIGNAL)) {
		if (dbus_message_get_args (message, NULL,
					   DBUS_TYPE_INT32, &id,
					   DBUS_TYPE_INVALID)) {
			drive = gnome_vfs_volume_monitor_get_drive_by_id (volume_monitor, id);
			if (drive != NULL) {
				_gnome_vfs_volume_monitor_disconnected (volume_monitor, drive);
				gnome_vfs_drive_unref (drive);
			}
		}
	}
	else if (dbus_message_is_signal (message,
					 DBUS_INTERFACE_DBUS,
					 "NameOwnerChanged")) {
		gchar *service, *old_owner, *new_owner;

		dbus_message_get_args (message,
				       NULL,
				       DBUS_TYPE_STRING, &service,
				       DBUS_TYPE_STRING, &old_owner,
				       DBUS_TYPE_STRING, &new_owner,
				       DBUS_TYPE_INVALID);
		
		if (strcmp (service, DVD_DAEMON_SERVICE) == 0) {
			if (strcmp (old_owner, "") != 0 &&
			    strcmp (new_owner, "") == 0) {
				/* No new owner, try to restart it. */
				dbus_try_activate_daemon (
					GNOME_VFS_VOLUME_MONITOR_CLIENT (volume_monitor));
			}
		}
	}
	
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
setup_dbus_connection (GnomeVFSVolumeMonitorClient *client)
{
	DBusError error;

	dbus_error_init (&error);
	
	client->dbus_conn = dbus_bus_get (DBUS_BUS_SESSION, &error);
	if (dbus_error_is_set (&error)) {
		g_warning ("Failed to open session DBUS connection: %s\n"
			   "Volume monitoring will not work.", error.message);
		dbus_error_free (&error);
		client->dbus_conn = NULL;
		return;
	} 
	/* We pass an error here to make this block (otherwise it just
	 * sends off the match rule when flushing the connection. This
	 * way we are sure to receive signals as soon as possible).
	 */
	dbus_bus_add_match (client->dbus_conn, DAEMON_SIGNAL_RULE, NULL);
	dbus_bus_add_match (client->dbus_conn, NAME_OWNER_CHANGED_SIGNAL_RULE, &error);
	if (dbus_error_is_set (&error)) {
		g_warning ("Couldn't add match rule.");
		dbus_error_free (&error);
	}
	
	if (!dbus_bus_start_service_by_name (client->dbus_conn,
					     DVD_DAEMON_SERVICE,
					     0,
					     NULL,
					     &error)) {
		g_warning ("Failed to activate daemon: %s", error.message);
		dbus_error_free (&error);
	}
	
	dbus_connection_add_filter (client->dbus_conn,
				    dbus_filter_func,
				    NULL,
				    NULL);
	
	dbus_connection_setup_with_g_main (client->dbus_conn, NULL);
}

static void
shutdown_dbus_connection (GnomeVFSVolumeMonitorClient *client)
{
	if (retry_timeout_id) {
		g_source_remove (retry_timeout_id);
		retry_timeout_id = 0;
	}
	
	if (client->dbus_conn) {
		dbus_bus_remove_match (client->dbus_conn, DAEMON_SIGNAL_RULE, NULL);
		dbus_bus_remove_match (client->dbus_conn, NAME_OWNER_CHANGED_SIGNAL_RULE, NULL);
		
		dbus_connection_remove_filter (client->dbus_conn,
					       dbus_filter_func,
					       NULL);

		if (!dbus_connection_get_is_connected (client->dbus_conn)) {
			dbus_connection_unref (client->dbus_conn);
		}
		client->dbus_conn = NULL;
	}
}

void
_gnome_vfs_volume_monitor_client_dbus_force_probe (GnomeVFSVolumeMonitorClient *volume_monitor_client)
{
	DBusMessage *message, *reply;

	if (volume_monitor_client->dbus_conn == NULL) {
		return;
	}
	
	message = dbus_message_new_method_call (DVD_DAEMON_SERVICE,
						DVD_DAEMON_OBJECT,
						DVD_DAEMON_INTERFACE,
						DVD_DAEMON_METHOD_FORCE_PROBE);
	g_assert (message != NULL);

	reply = dbus_connection_send_with_reply_and_block (volume_monitor_client->dbus_conn, 
							   message,
							   -1,
							   NULL);

	dbus_message_unref (message);
	if (reply) {
		dbus_message_unref (reply);
	}
}

void
_gnome_vfs_volume_monitor_client_dbus_emit_pre_unmount (GnomeVFSVolumeMonitorClient *volume_monitor_client,
							GnomeVFSVolume              *volume)
{
	DBusMessage *message, *reply;
	gint32       id;

	if (volume_monitor_client->dbus_conn == NULL) {
		return;
	}
	
	message = dbus_message_new_method_call (DVD_DAEMON_SERVICE,
						DVD_DAEMON_OBJECT,
						DVD_DAEMON_INTERFACE,
						DVD_DAEMON_METHOD_EMIT_PRE_UNMOUNT_VOLUME);
	g_assert (message != NULL);

	id = gnome_vfs_volume_get_id (volume);
	dbus_message_append_args (message,
				  DBUS_TYPE_INT32, &id,
				  DBUS_TYPE_INVALID);
	
	reply = dbus_connection_send_with_reply_and_block (volume_monitor_client->dbus_conn, 
							   message,
							   -1,
							   NULL);

	dbus_message_unref (message);
	if (reply) {
		dbus_message_unref (reply);
	}
}
