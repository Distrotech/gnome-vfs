/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004-2006 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Richard Hult <richard@imendio.com>
 */

#include <config.h>
#include <string.h>
#include <glib.h>

#include "gnome-vfs-volume.h"
#include "gnome-vfs-drive.h"
#include "gnome-vfs-volume-monitor-private.h"
#include "gnome-vfs-dbus-utils.h"

#define d(x)

GList *
_gnome_vfs_dbus_utils_get_drives (DBusConnection        *dbus_conn,
				  GnomeVFSVolumeMonitor *volume_monitor)
{
	DBusMessage     *message, *reply;
	GList           *list;
	DBusMessageIter  iter, array_iter;
	GnomeVFSDrive   *drive;

	message = dbus_message_new_method_call (DVD_DAEMON_SERVICE,
 						DVD_DAEMON_OBJECT,
						DVD_DAEMON_INTERFACE,
						DVD_DAEMON_METHOD_GET_DRIVES);

	reply = dbus_connection_send_with_reply_and_block (dbus_conn, 
							   message,
							   -1,
							   NULL);
	if (!reply) {
		d(g_print ("Error while getting drives from daemon.\n"));
		dbus_message_unref (message);
		return NULL;
	}

	list = NULL;	

	dbus_message_iter_init (reply, &iter);

	/* We can't recurse if there is no array. */
	if (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_ARRAY) {
		dbus_message_iter_recurse (&iter, &array_iter);
		
		while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRUCT) {
			drive = _gnome_vfs_drive_from_dbus (&array_iter, volume_monitor);
			
			list = g_list_prepend (list, drive);
			
			if (!dbus_message_iter_has_next (&array_iter)) {
				break;
			}
			
			dbus_message_iter_next (&array_iter);
		}
		
		list = g_list_reverse (list);
	}
	
	dbus_message_unref (message);
	dbus_message_unref (reply);

	return list;
}

GList *
_gnome_vfs_dbus_utils_get_volumes (DBusConnection        *dbus_conn,
				   GnomeVFSVolumeMonitor *volume_monitor)
{
	DBusMessage     *message, *reply;
	GList           *list;
	DBusMessageIter  iter, array_iter;
	GnomeVFSVolume  *volume;

	message = dbus_message_new_method_call (DVD_DAEMON_SERVICE,
 						DVD_DAEMON_OBJECT,
						DVD_DAEMON_INTERFACE,
						DVD_DAEMON_METHOD_GET_VOLUMES);

	reply = dbus_connection_send_with_reply_and_block (dbus_conn, 
							   message,
							   -1,
							   NULL);
	if (!reply) {
		d(g_print ("Error while getting volumes from daemon.\n"));
		dbus_message_unref (message);
		return NULL;
	}

	list = NULL;	

	dbus_message_iter_init (reply, &iter);

	/* We can't recurse if there is no array. */
	if (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_ARRAY) {
		dbus_message_iter_recurse (&iter, &array_iter);
		
		while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRUCT) {
			volume = _gnome_vfs_volume_from_dbus (&array_iter, volume_monitor);
			
			list = g_list_prepend (list, volume);
			
			if (!dbus_message_iter_has_next (&array_iter)) {
				break;
			}
			
			dbus_message_iter_next (&array_iter);
		}
		
		list = g_list_reverse (list);
	}
	
	dbus_message_unref (message);
	dbus_message_unref (reply);

	return list;
}

