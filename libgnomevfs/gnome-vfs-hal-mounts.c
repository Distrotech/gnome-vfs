/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-hal-mounts.c - read and monitor volumes using freedesktop HAL

   Copyright (C) 2004 Red Hat, Inc

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

   Author: David Zeuthen <davidz@redhat.com>
*/

#include <config.h>

#ifdef USE_HAL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-i18n.h>

#include <libhal.h>
#include <libhal-storage.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>


#include "gnome-vfs-hal-mounts.h"
#include "gnome-vfs-volume-monitor-daemon.h"
#include "gnome-vfs-volume-monitor-private.h"

typedef struct {
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	HalStoragePolicy *hal_storage_policy;
} GnomeVFSHalUserData;

static void
_hal_mainloop_integration (LibHalContext *ctx, 
			   DBusConnection * dbus_connection)
{
        dbus_connection_setup_with_g_main (dbus_connection, NULL);
}

static void 
_hal_device_added (LibHalContext *hal_ctx, 
		   const char *udi)
{
	GnomeVFSHalUserData *hal_userdata;
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	
	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	volume_monitor_daemon = hal_userdata->volume_monitor_daemon;

	/* Handle optical discs without data since these are not handled
	 * by GNOME VFS
	 */
	if (hal_device_get_property_bool (hal_ctx, udi, "volume.is_disc")) {
		const char *storage_udi;

		storage_udi = hal_device_get_property_string (hal_ctx, udi, "block.storage_device");
		if (storage_udi != NULL) {
			GnomeVFSDrive *drive;

			drive = _gnome_vfs_volume_monitor_find_drive_by_hal_udi (
				GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), storage_udi);
			if (drive != NULL) {
				/* this function also handles optical discs without data */
				_gnome_vfs_hal_mounts_modify_drive (volume_monitor_daemon, drive);
			}
		}		
	}
}

static void 
_hal_device_removed (LibHalContext *hal_ctx, const char *udi)
{
	GnomeVFSDrive *drive;
	GnomeVFSVolume *volume;
	GnomeVFSHalUserData *hal_userdata;
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	
	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	volume_monitor_daemon = hal_userdata->volume_monitor_daemon;

	drive = _gnome_vfs_volume_monitor_find_drive_by_hal_udi (
		GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), udi);

	volume = _gnome_vfs_volume_monitor_find_volume_by_hal_udi (
		GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), udi);

	/* Just remove the drive and or volume here; will also be done in 
	 * update_mtab_volumes and update_fstab_drives in the source file 
	 * gnome-vfs-volume-monitor-daemon.c but that wont matter much.
	 *
	 * We need to do this to handle optical discs without data since these
	 * are not handled by GNOME VFS
	 */
	if (volume != NULL) {
		_gnome_vfs_volume_monitor_unmounted (GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), volume);
	}

	if (drive != NULL) {
		_gnome_vfs_volume_monitor_disconnected (GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), drive);
	}
}

static void 
_hal_device_new_capability (LibHalContext *hal_ctx, 
			    const char *udi, 
			    const char *capability)
{
	GnomeVFSHalUserData *hal_userdata;
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	
	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	volume_monitor_daemon = hal_userdata->volume_monitor_daemon;

	/* do nothing */
}

static void 
_hal_device_lost_capability (LibHalContext *hal_ctx, 
			     const char *udi,
			     const char *capability)
{
	GnomeVFSHalUserData *hal_userdata;
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	
	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	volume_monitor_daemon = hal_userdata->volume_monitor_daemon;

	/* do nothing */
}

static void 
_hal_device_property_modified (LibHalContext *hal_ctx, 
			       const char *udi,
			       const char *key,
			       dbus_bool_t is_removed,
			       dbus_bool_t is_added)
{
	GnomeVFSHalUserData *hal_userdata;
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	
	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	volume_monitor_daemon = hal_userdata->volume_monitor_daemon;

	/* do nothing */
}

static void 
_hal_device_condition (LibHalContext *hal_ctx, 
		       const char *udi,
		       const char *condition_name,
		       DBusMessage *message)
{
	GnomeVFSHalUserData *hal_userdata;
	GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon;
	
	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	volume_monitor_daemon = hal_userdata->volume_monitor_daemon;

	/* do nothing */
}

static LibHalFunctions
hal_functions = { _hal_mainloop_integration,
		  _hal_device_added,
		  _hal_device_removed,
		  _hal_device_new_capability,
		  _hal_device_lost_capability,
		  _hal_device_property_modified,
		  _hal_device_condition };

static HalStoragePolicyIconPair icon_mapping[] = {
	{HAL_STORAGE_ICON_DRIVE_REMOVABLE_DISK,           "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_REMOVABLE_DISK_IDE,       "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_REMOVABLE_DISK_SCSI,      "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_REMOVABLE_DISK_USB,       "gnome-dev-removable-usb"},
	{HAL_STORAGE_ICON_DRIVE_REMOVABLE_DISK_IEEE1394,  "gnome-dev-removable-1394"},
	{HAL_STORAGE_ICON_DRIVE_DISK,                     "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_DISK_IDE,                 "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_DISK_SCSI,                "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_DISK_USB,                 "gnome-dev-removable-usb"},
	{HAL_STORAGE_ICON_DRIVE_DISK_IEEE1394,            "gnome-dev-removable-1394"},
	{HAL_STORAGE_ICON_DRIVE_CDROM,                    "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_CDROM_IDE,                "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_CDROM_SCSI,               "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_CDROM_USB,                "gnome-dev-removable-usb"},
	{HAL_STORAGE_ICON_DRIVE_CDROM_IEEE1394,           "gnome-dev-removable-1394"},
	{HAL_STORAGE_ICON_DRIVE_FLOPPY,                   "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_FLOPPY_IDE,               "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_FLOPPY_SCSI,              "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_FLOPPY_USB,               "gnome-dev-removable-usb"},
	{HAL_STORAGE_ICON_DRIVE_FLOPPY_IEEE1394,          "gnome-dev-removable-1394"},
	{HAL_STORAGE_ICON_DRIVE_TAPE,                     "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_COMPACT_FLASH,            "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_MEMORY_STICK,             "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_SMART_MEDIA,              "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_SD_MMC,                   "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_CAMERA,                   "gnome-dev-removable"},
	{HAL_STORAGE_ICON_DRIVE_PORTABLE_AUDIO_PLAYER,    "gnome-dev-removable"},

	{HAL_STORAGE_ICON_VOLUME_REMOVABLE_DISK,          "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_REMOVABLE_DISK_IDE,      "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_REMOVABLE_DISK_SCSI,     "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_REMOVABLE_DISK_USB,      "gnome-dev-harddisk-usb"},
	{HAL_STORAGE_ICON_VOLUME_REMOVABLE_DISK_IEEE1394, "gnome-dev-harddisk-1394"},
	{HAL_STORAGE_ICON_VOLUME_DISK,                    "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_DISK_IDE,                "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_DISK_SCSI,               "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_DISK_USB,                "gnome-dev-harddisk-usb"},
	{HAL_STORAGE_ICON_VOLUME_DISK_IEEE1394,           "gnome-dev-harddisk-1394"},
	{HAL_STORAGE_ICON_VOLUME_CDROM,                   "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_VOLUME_CDROM_IDE,               "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_VOLUME_CDROM_SCSI,              "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_VOLUME_CDROM_USB,               "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_VOLUME_CDROM_IEEE1394,          "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_VOLUME_FLOPPY,                  "gnome-dev-floppy"},
	{HAL_STORAGE_ICON_VOLUME_FLOPPY_IDE,              "gnome-dev-floppy"},
	{HAL_STORAGE_ICON_VOLUME_FLOPPY_SCSI,             "gnome-dev-floppy"},
	{HAL_STORAGE_ICON_VOLUME_FLOPPY_USB,              "gnome-dev-floppy"},
	{HAL_STORAGE_ICON_VOLUME_FLOPPY_IEEE1394,         "gnome-dev-floppy"},
	{HAL_STORAGE_ICON_VOLUME_TAPE,                    "gnome-dev-harddisk"},
	{HAL_STORAGE_ICON_VOLUME_COMPACT_FLASH,           "gnome-dev-media-cf"},
	{HAL_STORAGE_ICON_VOLUME_MEMORY_STICK,            "gnome-dev-media-ms"},
	{HAL_STORAGE_ICON_VOLUME_SMART_MEDIA,             "gnome-dev-media-sm"},
	{HAL_STORAGE_ICON_VOLUME_SD_MMC,                  "gnome-dev-media-sdmmc"},
	{HAL_STORAGE_ICON_VOLUME_CAMERA,                  "camera"},
	{HAL_STORAGE_ICON_VOLUME_PORTABLE_AUDIO_PLAYER,   "gnome-dev-ipod"},

	{HAL_STORAGE_ICON_DISC_CDROM,                     "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_DISC_CDR,                       "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_DISC_CDRW,                      "gnome-dev-cdrom"},
	{HAL_STORAGE_ICON_DISC_DVDROM,                    "gnome-dev-dvd"},
	{HAL_STORAGE_ICON_DISC_DVDRAM,                    "gnome-dev-dvd"},
	{HAL_STORAGE_ICON_DISC_DVDR,                      "gnome-dev-dvd"},
	{HAL_STORAGE_ICON_DISC_DVDRW,                     "gnome-dev-dvd"},
	{HAL_STORAGE_ICON_DISC_DVDPLUSR,                  "gnome-dev-dvd"},
	{HAL_STORAGE_ICON_DISC_DVDPLUSRW,                 "gnome-dev-dvd"},

/*
	{HAL_STORAGE_ICON_DISC_CDROM,                     "gnome-dev-disc-cdrom"},
	{HAL_STORAGE_ICON_DISC_CDR,                       "gnome-dev-disc-cdr"},
	{HAL_STORAGE_ICON_DISC_CDRW,                      "gnome-dev-disc-cdrw"},
	{HAL_STORAGE_ICON_DISC_DVDROM,                    "gnome-dev-disc-dvdrom"},
	{HAL_STORAGE_ICON_DISC_DVDRAM,                    "gnome-dev-disc-dvdram"},
	{HAL_STORAGE_ICON_DISC_DVDR,                      "gnome-dev-disc-dvdr"},
	{HAL_STORAGE_ICON_DISC_DVDRW,                     "gnome-dev-disc-dvdrw"},
	{HAL_STORAGE_ICON_DISC_DVDPLUSR,                  "gnome-dev-disc-dvdr-plus"},
	{HAL_STORAGE_ICON_DISC_DVDPLUSRW,                 "gnome-dev-disc-dvdrw-plus"},
*/

	{0x00, NULL}
};


gboolean
_gnome_vfs_hal_mounts_init (GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon)
{
	GnomeVFSHalUserData *hal_userdata;
	HalStoragePolicy *hal_storage_policy;

	/* Initialise the connection to the hal daemon */
	if ((volume_monitor_daemon->hal_ctx = 
	     hal_initialize (&hal_functions, FALSE)) == NULL) {
		g_warning ("hal_initialize failed\n");
		return FALSE;
	}


	/* Setup GNOME specific policy - right now this is only icons */
	hal_storage_policy = hal_storage_policy_new ();
	hal_storage_policy_set_icon_mapping (hal_storage_policy, icon_mapping);

	/* Tie some data with the libhal context */
	hal_userdata = g_new0 (GnomeVFSHalUserData, 1);
	hal_userdata->volume_monitor_daemon = volume_monitor_daemon;
	hal_userdata->hal_storage_policy = hal_storage_policy;
	hal_ctx_set_user_data (volume_monitor_daemon->hal_ctx,
			       hal_userdata);
	return TRUE;
}

void
_gnome_vfs_hal_mounts_shutdown (GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon)
{
	GnomeVFSHalUserData *hal_userdata;

	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (volume_monitor_daemon->hal_ctx);
	hal_storage_policy_free (hal_userdata->hal_storage_policy);

	if (hal_shutdown (volume_monitor_daemon->hal_ctx) != 0) {
		g_warning ("hal_shutdown failed\n");
	}
}

/**************************************************************************/

/** Ask HAL for more information about the drive and modify properties on the
 *  GnomeVFSDrive as appropriate. Note that this happens before the object
 *  is added to the volume monitor.
 *
 *  @param  volume_monitor_daemon  Handle to the volume monitor daemon
 *  @param  drive                  Handle to the GnomeVFSDrive object
 */
void 
_gnome_vfs_hal_mounts_modify_drive (GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon, 
				    GnomeVFSDrive *drive)
{
	char *drive_name;
	char *drive_icon;
	char *unique_drive_name;
	LibHalContext *hal_ctx; 
	HalDrive *hal_drive;
	HalVolume *hal_volume;
	GnomeVFSHalUserData *hal_userdata;
	HalStoragePolicy *hal_storage_policy;
	char *target_mount_point;

	hal_drive = NULL;
	hal_volume = NULL;

	if ((hal_ctx = volume_monitor_daemon->hal_ctx) == NULL)
		goto out;

	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	hal_storage_policy = hal_userdata->hal_storage_policy;

	if (drive == NULL || drive->priv == NULL || drive->priv->device_path == NULL)
		goto out;

	/* Note, the device_path may point to what hal calls a volume, e.g. 
	 * /dev/sda1 etc, however we get the Drive object for the parent if
	 * that is the case. This is a feature of libhal-storage.
	 */
	if ((hal_drive = hal_drive_from_device_file (hal_ctx, drive->priv->device_path)) == NULL) {
		g_warning ("%s: no hal drive for device=%s", __FUNCTION__, drive->priv->device_path);
		goto out;
	}

	/* There may not be a volume object associated, so hal_volume may be NULL */
	hal_volume = hal_volume_from_device_file (hal_ctx, drive->priv->device_path);

	/* For optical discs, we manually add/remove GnomeVFSVolume optical discs without 
	 * data (e.g. blank and pure audio) since these don't appear in the mounted filesystems
	 * file /etc/mtab
	 */
	if (hal_volume != NULL && 
	    hal_drive_get_type (hal_drive) == HAL_DRIVE_TYPE_CDROM && 
	    hal_volume_is_disc (hal_volume) && !hal_volume_disc_has_data (hal_volume)) {
		GnomeVFSVolume *volume;
		char *volume_name;
		char *volume_icon;

		/* see if we already got a volume */
		volume = gnome_vfs_drive_get_mounted_volume (drive);
		if (volume != NULL) {
			gnome_vfs_volume_unref (volume);
		} else {
			
			volume_name = hal_volume_policy_compute_display_name (
				hal_drive, hal_volume, hal_storage_policy);

			/* set icon name; try dedicated icon name first... */
			if (hal_drive_get_dedicated_icon_volume (hal_drive) != NULL)
				volume_icon = strdup (hal_drive_get_dedicated_icon_volume (hal_drive));
			else
				volume_icon = hal_volume_policy_compute_icon_name (
					hal_drive, hal_volume, hal_storage_policy);

			volume = g_object_new (GNOME_VFS_TYPE_VOLUME, NULL);
			volume->priv->hal_udi = g_strdup (hal_volume_get_udi (hal_volume));
			volume->priv->volume_type = GNOME_VFS_VOLUME_TYPE_MOUNTPOINT;
			
			if (hal_volume_disc_is_blank (hal_volume)) {
				/* Blank discs should open the burn:/// location */
				volume->priv->device_path = g_strdup (hal_volume_get_device_file (hal_volume));
				volume->priv->activation_uri = g_strdup ("burn:///");
				volume->priv->unix_device = makedev (hal_volume_get_device_major (hal_volume), 
								     hal_volume_get_device_minor (hal_volume));
				volume->priv->filesystem_type = g_strdup (hal_volume_get_fstype (hal_volume));
			} else if (hal_volume_disc_has_audio (hal_volume)) {
				/* Audio discs with data should open the cdda:///dev/cdrom location */
				volume->priv->device_path = g_strdup (hal_volume_get_device_file (hal_volume));
				volume->priv->activation_uri = g_strdup_printf (
					"cdda://%s", hal_volume_get_device_file (hal_volume));
				volume->priv->unix_device = makedev (hal_volume_get_device_major (hal_volume), 
								     hal_volume_get_device_minor (hal_volume));
				volume->priv->filesystem_type = g_strdup (hal_volume_get_fstype (hal_volume));
			}
			
			volume->priv->is_read_only = TRUE;
			volume->priv->is_mounted = FALSE;
			
			volume->priv->device_type = GNOME_VFS_DEVICE_TYPE_CDROM;
			
			volume->priv->display_name = _gnome_vfs_volume_monitor_uniquify_volume_name (
				GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), volume_name);
			volume->priv->icon = g_strdup (volume_icon);
			volume->priv->is_user_visible = TRUE;
			
			volume->priv->drive = drive;
			_gnome_vfs_drive_add_mounted_volume (drive, volume);
			
			_gnome_vfs_volume_monitor_mounted (GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), volume);
			gnome_vfs_volume_unref (volume);

			free (volume_name);
			free (volume_icon);
		}
	} else if (hal_volume == NULL && hal_drive_get_type (hal_drive) == HAL_DRIVE_TYPE_CDROM) {
		GnomeVFSVolume *volume;

		/* Remove GnomeVFSVolume with same device file */

		volume = gnome_vfs_drive_get_mounted_volume (drive);
		if (volume != NULL) {
			_gnome_vfs_volume_monitor_unmounted (GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), volume);
		}
	}


	/* Now, modify the drive with the hal stuff, unless we've already done so */
	if (drive->priv->hal_udi != NULL)
		goto out;

	/* set whether we need to eject */
	drive->priv->must_eject_at_unmount = hal_drive_requires_eject (hal_drive);

	/* set display name */
	drive_name = hal_drive_policy_compute_display_name (hal_drive, hal_volume, hal_storage_policy);
	unique_drive_name = _gnome_vfs_volume_monitor_uniquify_drive_name (
		GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), drive_name);
	if (drive->priv->display_name != NULL)
		g_free (drive->priv->display_name);
	drive->priv->display_name = unique_drive_name;
	free (drive_name);

	/* set icon name; try dedicated icon name first... */
	if (hal_drive_get_dedicated_icon_drive (hal_drive) != NULL)
		drive_icon = strdup (hal_drive_get_dedicated_icon_drive (hal_drive));
	else
		drive_icon = hal_drive_policy_compute_icon_name (hal_drive, hal_volume, hal_storage_policy);
	if (drive->priv->icon != NULL)
		g_free (drive->priv->icon);
	drive->priv->icon = g_strdup (drive_icon);
	free (drive_icon);

	/* figure out target mount point; first see if we're mounted */
	target_mount_point = NULL;
	if (hal_volume != NULL) {
		const char *str;
		str = hal_volume_get_mount_point (hal_volume);
		if (str != NULL)
			target_mount_point = g_strdup (str);
	}

	/* otherwise take the mount path that was found in /etc/fstab or /etc/mtab */
	if (target_mount_point == NULL)
		target_mount_point = gnome_vfs_get_local_path_from_uri (drive->priv->activation_uri);

	/* if we don't use removable media and the volume shouldn't be visible, then hide the drive */
	if(!hal_drive_uses_removable_media (hal_drive) && 
	   !hal_volume_policy_should_be_visible (hal_drive, hal_volume, hal_storage_policy, target_mount_point))
		drive->priv->is_user_visible = FALSE;

	g_free (target_mount_point);

	/* set hal udi */
	drive->priv->hal_udi = g_strdup (hal_drive_get_udi (hal_drive));

out:
	hal_volume_free (hal_volume);
	hal_drive_free (hal_drive);
}

void 
_gnome_vfs_hal_mounts_modify_volume (GnomeVFSVolumeMonitorDaemon *volume_monitor_daemon, 
				     GnomeVFSVolume *volume)
{
	char *volume_name;
	char *volume_icon;
	char *unique_volume_name;
	LibHalContext *hal_ctx; 
	HalDrive *hal_drive;
	HalVolume *hal_volume;
	GnomeVFSHalUserData *hal_userdata;
	HalStoragePolicy *hal_storage_policy;
	char *target_mount_point;

	hal_volume = NULL;
	hal_drive = NULL;

	if ((hal_ctx = volume_monitor_daemon->hal_ctx) == NULL)
		goto out;
	if (volume == NULL || volume->priv == NULL || volume->priv->device_path == NULL)
		goto out;

	hal_userdata = (GnomeVFSHalUserData *) hal_ctx_get_user_data (hal_ctx);
	hal_storage_policy = hal_userdata->hal_storage_policy;

	/* Now, modify the drive with the hal stuff, unless we've already done so */
	if (volume->priv->hal_udi != NULL)
		goto out;

	/* Note, the device_path points to what hal calls a volume, e.g. 
	 * /dev/sda1 etc, however we get the Drive object for the parent if
	 * that is the case. This is a feature of libhal-storage.
	 */
	if ((hal_drive = hal_drive_from_device_file (hal_ctx, volume->priv->device_path)) == NULL) {
		g_warning ("%s: no hal drive for device=%s", __FUNCTION__, volume->priv->device_path);
		goto out;
	}
	if ((hal_volume = hal_volume_from_device_file (hal_ctx, volume->priv->device_path)) == NULL) {
		g_warning ("%s: no hal volume for device=%s", __FUNCTION__, volume->priv->device_path);
		goto out;
	}

	/* set display name */
	volume_name = hal_volume_policy_compute_display_name (hal_drive, hal_volume, hal_storage_policy);
	unique_volume_name = _gnome_vfs_volume_monitor_uniquify_volume_name (
		GNOME_VFS_VOLUME_MONITOR (volume_monitor_daemon), volume_name);
	if (volume->priv->display_name != NULL)
		g_free (volume->priv->display_name);
	volume->priv->display_name = unique_volume_name;
	free (volume_name);

	/* set icon name; try dedicated icon name first... */
	if (hal_drive_get_dedicated_icon_volume (hal_drive) != NULL)
		volume_icon = strdup (hal_drive_get_dedicated_icon_volume (hal_drive));
	else
		volume_icon = hal_volume_policy_compute_icon_name (hal_drive, hal_volume, hal_storage_policy);
	if (volume->priv->icon != NULL)
		g_free (volume->priv->icon);
	volume->priv->icon = g_strdup (volume_icon);
	free (volume_icon);

	/* figure out target mount point; first see if we're mounted */
	target_mount_point = NULL;
	{
		const char *str;
		str = hal_volume_get_mount_point (hal_volume);
		if (str != NULL)
			target_mount_point = g_strdup (str);
	}

	/* otherwise take the mount path that was found in /etc/fstab or /etc/mtab */
	if (target_mount_point == NULL)
		target_mount_point = gnome_vfs_get_local_path_from_uri (volume->priv->activation_uri);

	/* set whether it's visible on the desktop */
	volume->priv->is_user_visible = 
		hal_volume_policy_should_be_visible (hal_drive, hal_volume, hal_storage_policy, target_mount_point) &&
		(hal_drive_is_hotpluggable (hal_drive) || hal_drive_uses_removable_media (hal_drive));

	g_free (target_mount_point);

	/* set hal udi */
	volume->priv->hal_udi = g_strdup (hal_volume_get_udi (hal_volume));
out:
	hal_drive_free (hal_drive);
	hal_volume_free (hal_volume);
}

#endif /* USE_HAL */
