/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-volume-ops.c - mount/unmount/eject handling

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
#include <pthread.h>
#include <libbonobo.h>
#include <gconf/gconf-client.h>
#include "gnome-vfs-i18n.h"
#include "gnome-vfs-volume-monitor-private.h"
#include "gnome-vfs-volume.h"
#include "gnome-vfs-utils.h"
#include "gnome-vfs-drive.h"
#include "gnome-vfs-client.h"
#include "gnome-vfs-private.h"

#ifdef USE_VOLRMMOUNT

static const char *volrmmount_locations [] = {
       "/usr/bin/volrmmount",
       NULL
};

#define MOUNT_COMMAND volrmmount_locations
#define MOUNT_SEPARATOR " -i "
#define UMOUNT_COMMAND volrmmount_locations
#define UMOUNT_SEPARATOR " -e "

#else

static const char *mount_known_locations [] = {
	"/sbin/mount", "/bin/mount",
	"/usr/sbin/mount", "/usr/bin/mount",
	NULL
};

static const char *umount_known_locations [] = {
	"/sbin/umount", "/bin/umount",
	"/usr/sbin/umount", "/usr/bin/umount",
	NULL
};

#define MOUNT_COMMAND mount_known_locations
#define MOUNT_SEPARATOR " "
#define UMOUNT_COMMAND umount_known_locations
#define UMOUNT_SEPARATOR " "

#endif /* USE_VOLRMMOUNT */

/* Returns the full path to the queried command */
static const char *
find_command (const char **known_locations)
{
	int i;

	for (i = 0; known_locations [i]; i++){
		if (g_file_test (known_locations [i], G_FILE_TEST_EXISTS))
			return known_locations [i];
	}
	return NULL;
}

typedef struct {
	char *argv[4];
	char *mount_point;
	char *device_path;
	GnomeVFSDeviceType device_type;
	gboolean should_mount;
	gboolean should_eject;
	GnomeVFSVolumeOpCallback callback;
	gpointer user_data;

	/* Result: */
	gboolean succeeded;
	char *error_message;
	char *detailed_error_message;
} MountThreadInfo;

static char *
generate_mount_error_message (char *standard_error,
			      GnomeVFSDeviceType device_type)
{
	char *message;
	
	if ((strstr (standard_error, "is not a valid block device") != NULL) ||
	    (strstr (standard_error, "No medium found") != NULL)) {
		/* No media in drive */
		if (device_type == GNOME_VFS_DEVICE_TYPE_FLOPPY) {
			/* Handle floppy case */
			message = g_strdup_printf (_("Unable to mount the floppy drive. "
						     "There is probably no floppy in the drive."));
		} else {
			/* All others */
			message = g_strdup_printf (_("Unable to mount the volume. "
						     "There is probably no media in the device."));
		}
	} else if (strstr (standard_error, "wrong fs type, bad option, bad superblock on") != NULL) {
		/* Unknown filesystem */
		if (device_type == GNOME_VFS_DEVICE_TYPE_FLOPPY) {
			message = g_strdup_printf (_("Unable to mount the floppy drive. "
						     "The floppy is probably in a format that cannot be mounted."));
		} else {
			message = g_strdup_printf (_("Unable to mount the selected volume. "
						     "The volume is probably in a format that cannot be mounted."));
		}
	} else {
		if (device_type == GNOME_VFS_DEVICE_TYPE_FLOPPY) {
			message = g_strdup (_("Unable to mount the selected floppy drive."));
		} else {
			message = g_strdup (_("Unable to mount the selected volume."));
		}
	}
	return message;
}

static char *
generate_unmount_error_message (char *standard_error,
				GnomeVFSDeviceType device_type)
{
	char *message;
	
	message = g_strdup (_("Unable to unmount the selected volume."));
	return message;
}

static gboolean
report_mount_result (gpointer callback_data)
{
	MountThreadInfo *info;
	int i;

	info = callback_data;

	(info->callback) (info->succeeded,
			  info->error_message,
			  info->detailed_error_message,
			  info->user_data);

	i = 0;
	while (info->argv[i] != NULL) {
		g_free (info->argv[i]);
		i++;
	}
	g_free (info->mount_point);
	g_free (info->device_path);
	g_free (info->error_message);
	g_free (info->detailed_error_message);
	g_free (info);
	
	return FALSE;
}

static void *
mount_unmount_thread (void *arg)
{
	MountThreadInfo *info;
	char *standard_error;
	gint exit_status;
	GError *error;
	char *envp[] = {
		"LC_ALL=C",
		NULL
	};

	info = arg;

	if (info != NULL) {
		error = NULL;
		if (g_spawn_sync (NULL,
				   info->argv,
				   envp,
				   G_SPAWN_STDOUT_TO_DEV_NULL,
				   NULL, NULL,
				   NULL,
				   &standard_error,
				   &exit_status,
				   &error)) {
			if (exit_status == 0) {
				info->succeeded = TRUE;
				info->error_message = NULL;
				info->detailed_error_message = NULL;
			} else {
				info->succeeded = FALSE;
				if (info->should_mount) {
					info->error_message = generate_mount_error_message (standard_error,
											    info->device_type);
				} else {
					info->error_message = generate_unmount_error_message (standard_error,
											      info->device_type);
				}
				info->detailed_error_message = g_strdup (standard_error);
			}

			g_free (standard_error);
		} else {
			/* spawn failure */
			info->succeeded = FALSE;
			info->error_message = g_strdup (_("Failed to start command"));
			info->detailed_error_message = g_strdup (error->message);
			g_error_free (error);
		}
	}

	if (info->should_eject) {
		char *argv[3] = {
			"eject",
			NULL,
			NULL
		};
		argv[1] = info->device_path?info->device_path:info->mount_point;

		if (g_spawn_sync (NULL,
				  argv,
				  NULL,
				  G_SPAWN_SEARCH_PATH,
				  NULL, NULL,
				  NULL, &standard_error,
				  &exit_status,
				  &error)) {
			if (exit_status != 0 &&
			    info->succeeded) {
				info->succeeded = FALSE;
				info->error_message = g_strdup (_("Unable to eject media"));
				info->detailed_error_message = g_strdup (standard_error);
			}
		} else {
			/* Spawn failure */
			if (info->succeeded) {
				info->succeeded = FALSE;
				info->error_message = g_strdup (_("Failed to start command"));
				info->detailed_error_message = g_strdup (error->message);
			}
			g_error_free (error);
		}
		g_free (standard_error);
	}

	g_idle_add (report_mount_result, info);	
	
	pthread_exit (NULL); 	
	
	return NULL;
}


static void
mount_unmount_operation (const char *mount_point,
			 const char *device_path,
			 GnomeVFSDeviceType device_type,
			 gboolean should_mount,
			 gboolean should_eject,
			 GnomeVFSVolumeOpCallback  callback,
			 gpointer                  user_data)
{
	const char *command;
	MountThreadInfo *mount_info;
	pthread_t mount_thread;
	const char *name;
	int i;

	
#ifdef USE_VOLRMMOUNT
       name = strrchr (mount_point, '/');
       if (name != NULL) {
               name = name + 1;
       } else {
	       name = mount_point;
       }
#else
       name = mount_point;
#endif
       
       if (should_mount) {
               command = find_command (MOUNT_COMMAND);
       } else {
               command = find_command (UMOUNT_COMMAND);
       }

	mount_info = g_new0 (MountThreadInfo, 1);
	i = 0;
	mount_info->argv[i++] = g_strdup (command);
	if (should_mount) {
#ifdef  MOUNT_ARGUMENT
		mount_info->argv[i++] = g_strdup (MOUNT_ARGUMENT);
#endif
	} else {
#ifdef  UNMOUNT_ARGUMENT
		mount_info->argv[i++] = g_strdup (UNMOUNT_ARGUMENT);
#endif
	}
	mount_info->argv[i++] = g_strdup (name);
	mount_info->argv[i++] = NULL;
	
	mount_info->mount_point = g_strdup (mount_point);
	mount_info->device_path = g_strdup (device_path);
	mount_info->device_type = device_type;
	mount_info->should_mount = should_mount;
	mount_info->should_eject = should_eject;
	mount_info->callback = callback;
	mount_info->user_data = user_data;
	
	pthread_create (&mount_thread, NULL, mount_unmount_thread, mount_info);
}


/* TODO: check if already mounted/unmounted, emit pre_unmount, check for mount types */

static void
emit_pre_unmount (GnomeVFSVolume *volume)
{
	GnomeVFSVolumeMonitor *volume_monitor;
	GnomeVFSClient *client;
	GNOME_VFS_Daemon daemon;
	CORBA_Environment  ev;
	
	volume_monitor = gnome_vfs_get_volume_monitor ();

	if (gnome_vfs_get_is_daemon ()) {
		gnome_vfs_volume_monitor_emit_pre_unmount (volume_monitor,
							   volume);
	} else {
		client = _gnome_vfs_get_client ();
		daemon = _gnome_vfs_client_get_daemon (client);
		
		CORBA_exception_init (&ev);
		 GNOME_VFS_Daemon_emitPreUnmountVolume (daemon,
							BONOBO_OBJREF (client),
							gnome_vfs_volume_get_id (volume),
							&ev);
		 if (BONOBO_EX (&ev)) {
			 CORBA_exception_free (&ev);
		 }
		 CORBA_Object_release (daemon, NULL);
	}
}

static void
unmount_connected_server (GnomeVFSVolume *volume,
			  GnomeVFSVolumeOpCallback  callback,
			  gpointer                   user_data)
{
	GConfClient *client;
	gboolean res;
	char *key;
	gboolean success;
	char *detailed_error;
	GError *error;

	success = TRUE;
	detailed_error = NULL;
	
	client = gconf_client_get_default ();

	key = g_strconcat (CONNECTED_SERVERS_DIR "/",
			   volume->priv->gconf_id,
			   "/uri", NULL);
	error = NULL;
	res = gconf_client_unset (client, key, &error);
	g_free (key);
	if (!res) {
		if (success) {
			detailed_error = g_strdup (error->message);
		}
		success = FALSE;
		g_error_free (error);
	}
	
	key = g_strconcat (CONNECTED_SERVERS_DIR "/",
			   volume->priv->gconf_id,
			   "/icon", NULL);
	res = gconf_client_unset (client, key, &error);
	g_free (key);
	if (!res) {
		if (success) {
			detailed_error = g_strdup (error->message);
		}
		success = FALSE;
		g_error_free (error);
	}
	
	key = g_strconcat (CONNECTED_SERVERS_DIR "/",
			   volume->priv->gconf_id,
			   "/display_name", NULL);
	res = gconf_client_unset (client, key, &error);
	g_free (key);
	if (!res) {
		if (success) {
			detailed_error = g_strdup (error->message);
		}
		success = FALSE;
		g_error_free (error);
	}
	
	g_object_unref (client);

	if (success) {
		(*callback) (success, NULL, NULL, user_data);
	} else {
		(*callback) (success, _("Unable to unmount connected server"), detailed_error, user_data);
	}
	g_free (detailed_error);
}


void
gnome_vfs_volume_unmount (GnomeVFSVolume *volume,
			  GnomeVFSVolumeOpCallback  callback,
			  gpointer                   user_data)
{
	char *mount_path, *device_path;
	char *uri;
	GnomeVFSVolumeType type;
	
	emit_pre_unmount (volume);

	type = gnome_vfs_volume_get_volume_type (volume);
	if (type == GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		uri = gnome_vfs_volume_get_activation_uri (volume);
		mount_path = gnome_vfs_get_local_path_from_uri (uri);
		g_free (uri);
		device_path = gnome_vfs_volume_get_device_path (volume);
		mount_unmount_operation (mount_path,
					 device_path,
					 gnome_vfs_volume_get_device_type (volume),
					 FALSE, FALSE,
					 callback, user_data);
		g_free (mount_path);
		g_free (device_path);
	} else {
		unmount_connected_server (volume, callback, user_data);
	}
}

void
gnome_vfs_volume_eject (GnomeVFSVolume *volume,
			GnomeVFSVolumeOpCallback  callback,
			gpointer                   user_data)
{
	char *mount_path, *device_path;
	char *uri;
	GnomeVFSVolumeType type;
	
	emit_pre_unmount (volume);

	type = gnome_vfs_volume_get_volume_type (volume);
	if (type == GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		uri = gnome_vfs_volume_get_activation_uri (volume);
		mount_path = gnome_vfs_get_local_path_from_uri (uri);
		g_free (uri);
		device_path = gnome_vfs_volume_get_device_path (volume);
		mount_unmount_operation (mount_path,
					 device_path,
					 gnome_vfs_volume_get_device_type (volume),
					 FALSE, TRUE,
					 callback, user_data);
		g_free (mount_path);
		g_free (device_path);
	} else {
		unmount_connected_server (volume, callback, user_data);
	}
}

void
gnome_vfs_drive_mount (GnomeVFSDrive  *drive,
		       GnomeVFSVolumeOpCallback  callback,
		       gpointer                   user_data)
{
	char *mount_path, *device_path, *uri;
	
	uri = gnome_vfs_drive_get_activation_uri (drive);
	mount_path = gnome_vfs_get_local_path_from_uri (uri);
	g_free (uri);
	device_path = gnome_vfs_drive_get_device_path (drive);
	mount_unmount_operation (mount_path,
				 device_path,
				 GNOME_VFS_DEVICE_TYPE_UNKNOWN,
				 TRUE, FALSE,
				 callback, user_data);
	g_free (mount_path);
	g_free (device_path);
}

void
gnome_vfs_drive_unmount (GnomeVFSDrive  *drive,
			 GnomeVFSVolumeOpCallback  callback,
			 gpointer                   user_data)
{
	char *mount_path, *device_path, *uri;
	GnomeVFSVolume *volume;


	volume = gnome_vfs_drive_get_mounted_volume (drive);

	if (volume != NULL) {
		emit_pre_unmount (volume);
	}

	uri = gnome_vfs_drive_get_activation_uri (drive);
	mount_path = gnome_vfs_get_local_path_from_uri (uri);
	g_free (uri);
	device_path = gnome_vfs_drive_get_device_path (drive);
	mount_unmount_operation (mount_path,
				 device_path,
				 (volume == NULL) ? GNOME_VFS_DEVICE_TYPE_UNKNOWN:gnome_vfs_volume_get_device_type (volume),
				 FALSE, FALSE,
				 callback, user_data);
	g_free (mount_path);
	g_free (device_path);
	gnome_vfs_volume_unref (volume);
}

void
gnome_vfs_drive_eject (GnomeVFSDrive  *drive,
		       GnomeVFSVolumeOpCallback  callback,
		       gpointer                   user_data)
{
	char *mount_path, *device_path, *uri;
	GnomeVFSVolume *volume;


	volume = gnome_vfs_drive_get_mounted_volume (drive);

	if (volume != NULL) {
		emit_pre_unmount (volume);
	}

	uri = gnome_vfs_drive_get_activation_uri (drive);
	mount_path = gnome_vfs_get_local_path_from_uri (uri);
	g_free (uri);
	device_path = gnome_vfs_drive_get_device_path (drive);
	mount_unmount_operation (mount_path,
				 device_path,
				 (volume == NULL) ? GNOME_VFS_DEVICE_TYPE_UNKNOWN:gnome_vfs_volume_get_device_type (volume),
				 FALSE, TRUE,
				 callback, user_data);
	g_free (mount_path);
	g_free (device_path);
	gnome_vfs_volume_unref (volume);
}


void
gnome_vfs_connect_to_server (char                     *uri,
			     char                     *display_name,
			     char                     *icon)
{
	GConfClient *client;
	GSList *dirs, *l;
	char *dir, *dir_id;
	int max_id, gconf_id;
	char *key;
	char *id;

	client = gconf_client_get_default ();

	max_id = 0;
	dirs = gconf_client_all_dirs (client,
				      CONNECTED_SERVERS_DIR, NULL);
	for (l = dirs; l != NULL; l = l->next) {
		dir = l->data;

		dir_id = strrchr (dir, '/');
		if (dir_id != NULL) {
			dir_id++;
			gconf_id = strtol (dir_id, NULL, 10);
			max_id = MAX (max_id, gconf_id);
		}
		
		g_free (dir);
	}
	g_slist_free (dirs);

	id = g_strdup_printf ("%d", max_id + 1);
	
	key = g_strconcat (CONNECTED_SERVERS_DIR "/",
			   id,
			   "/icon", NULL);
	gconf_client_set_string (client, key, icon, NULL);
	g_free (key);
	
	key = g_strconcat (CONNECTED_SERVERS_DIR "/",
			   id,
			   "/display_name", NULL);
	gconf_client_set_string (client, key, display_name, NULL);
	g_free (key);

	/* Uri key creation triggers creation, do this last */
	key = g_strconcat (CONNECTED_SERVERS_DIR "/",
			   id,
			   "/uri", NULL);
	gconf_client_set_string (client, key, uri, NULL);
	g_free (key);
	
	g_free (id);
	g_object_unref (client);
}