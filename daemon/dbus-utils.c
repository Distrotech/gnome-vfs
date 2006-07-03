/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004 Nokia Corporation.
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
 */

#include <config.h>
#include <string.h>
#include <glib.h>

#include <dbus/dbus.h>

#include "gnome-vfs-volume-monitor-private.h"
#include "gnome-vfs-dbus-utils.h"
#include "dbus-utils.h"

/*
 * Volume messages
 */
void
dbus_utils_message_append_volume_list (DBusMessage *message, GList *volumes)
{
	DBusMessageIter  iter, array_iter;
	GList           *l;
	GnomeVFSVolume  *volume;
	
	g_return_if_fail (message != NULL);

	if (!volumes) {
		return;
	}

	dbus_message_iter_init_append (message, &iter);

	dbus_message_iter_open_container (&iter,
					  DBUS_TYPE_ARRAY,
					  DBUS_STRUCT_BEGIN_CHAR_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_BOOLEAN_AS_STRING
					  DBUS_TYPE_BOOLEAN_AS_STRING
					  DBUS_TYPE_BOOLEAN_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_STRUCT_END_CHAR_AS_STRING,
					  &array_iter);
	
	for (l = volumes; l; l = l->next) {
		volume = l->data;

		gnome_vfs_volume_to_dbus (&array_iter, volume);
	}

	dbus_message_iter_close_container (&iter, &array_iter);
}

void
dbus_utils_message_append_drive_list (DBusMessage *message, GList *drives)
{
	DBusMessageIter  iter, array_iter;
	GList           *l;
	GnomeVFSDrive   *drive;
	
	g_return_if_fail (message != NULL);

	if (!drives) {
		return;
	}

	dbus_message_iter_init_append (message, &iter);
	
	dbus_message_iter_open_container (&iter,
					  DBUS_TYPE_ARRAY,
					  DBUS_STRUCT_BEGIN_CHAR_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_ARRAY_AS_STRING
					  DBUS_TYPE_INT32_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_STRING_AS_STRING
					  DBUS_TYPE_BOOLEAN_AS_STRING
					  DBUS_TYPE_BOOLEAN_AS_STRING
					  DBUS_TYPE_BOOLEAN_AS_STRING
					  DBUS_STRUCT_END_CHAR_AS_STRING,
					  &array_iter);
	
	for (l = drives; l; l = l->next) {
		drive = l->data;

		gnome_vfs_drive_to_dbus (&array_iter, drive);
	}

	dbus_message_iter_close_container (&iter, &array_iter);
}

void
dbus_utils_message_append_volume (DBusMessage *message, GnomeVFSVolume *volume)
{
	DBusMessageIter  iter;
	
	g_return_if_fail (message != NULL);
	g_return_if_fail (volume != NULL);

	dbus_message_iter_init_append (message, &iter);
	gnome_vfs_volume_to_dbus (&iter, volume);
}

void
dbus_utils_message_append_drive (DBusMessage *message, GnomeVFSDrive *drive)
{
	DBusMessageIter  iter;
	
	g_return_if_fail (message != NULL);
	g_return_if_fail (drive != NULL);

	dbus_message_iter_init_append (message, &iter);
	gnome_vfs_drive_to_dbus (&iter, drive);
}
