/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-drive.h - Handling of drives for the GNOME Virtual File System.

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

#ifndef GNOME_VFS_DRIVE_H
#define GNOME_VFS_DRIVE_H

#include <glib-object.h>
#include "gnome-vfs-volume.h"

G_BEGIN_DECLS

#define GNOME_VFS_TYPE_DRIVE        (gnome_vfs_drive_get_type ())
#define GNOME_VFS_DRIVE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_VFS_DRIVE, GnomeVFSDrive))
#define GNOME_VFS_DRIVE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_VFS_DRIVE, GnomeVFSDriveClass))
#define GNOME_IS_VFS_DRIVE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_VFS_DRIVE))
#define GNOME_IS_VFS_DRIVE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_VFS_DRIVE))

typedef struct _GnomeVFSDrivePrivate GnomeVFSDrivePrivate;

struct _GnomeVFSDrive {
	GObject parent;

        GnomeVFSDrivePrivate *priv;
};

typedef struct {
	GObjectClass parent_class;

	void (* volume_mounted)	  	(GnomeVFSDrive *drive,
				   	 GnomeVFSVolume	*volume);
	void (* volume_pre_unmount)	(GnomeVFSDrive *drive,
				   	 GnomeVFSVolume	*volume);
	void (* volume_unmounted)	(GnomeVFSDrive *drive,
				   	 GnomeVFSVolume	*volume);
} GnomeVFSDriveClass;

GType gnome_vfs_drive_get_type (void) G_GNUC_CONST;

GnomeVFSDrive *gnome_vfs_drive_ref   (GnomeVFSDrive *drive);
void           gnome_vfs_drive_unref (GnomeVFSDrive *drive);

gulong             gnome_vfs_drive_get_id              (GnomeVFSDrive *drive);
GnomeVFSDeviceType gnome_vfs_drive_get_device_type     (GnomeVFSDrive *drive);
GnomeVFSVolume *   gnome_vfs_drive_get_mounted_volume  (GnomeVFSDrive *drive);
char *             gnome_vfs_drive_get_device_path     (GnomeVFSDrive *drive);
char *             gnome_vfs_drive_get_activation_uri  (GnomeVFSDrive *drive);
char *             gnome_vfs_drive_get_display_name    (GnomeVFSDrive *drive);
char *             gnome_vfs_drive_get_icon            (GnomeVFSDrive *drive);
gboolean           gnome_vfs_drive_is_user_visible     (GnomeVFSDrive *drive);
gboolean           gnome_vfs_drive_is_connected        (GnomeVFSDrive *drive);

gboolean           gnome_vfs_drive_eject               (GnomeVFSDrive *drive,
							GError       **err);
gboolean           gnome_vfs_drive_unmount             (GnomeVFSDrive *drive,
							GError       **err);
gboolean           gnome_vfs_drive_mount               (GnomeVFSDrive *drive,
							GError       **err);

G_END_DECLS

#endif /* GNOME_VFS_DRIVE_H */
