/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-volume.c - Handling of volumes for the GNOME Virtual File System.

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
#include <glib/gthread.h>

#include "gnome-vfs-volume.h"
#include "gnome-vfs-volume-monitor-private.h"
#include "gnome-vfs-filesystem-type.h"

static void     gnome_vfs_volume_class_init           (GnomeVFSVolumeClass *klass);
static void     gnome_vfs_volume_init                 (GnomeVFSVolume      *volume);
static void     gnome_vfs_volume_finalize             (GObject          *object);


static GObjectClass *parent_class = NULL;

G_LOCK_DEFINE_STATIC (volumes);

GType
gnome_vfs_volume_get_type (void)
{
	static GType volume_type = 0;

	if (!volume_type) {
		static const GTypeInfo volume_info = {
			sizeof (GnomeVFSVolumeClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gnome_vfs_volume_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GnomeVFSVolume),
			0,              /* n_preallocs */
			(GInstanceInitFunc) gnome_vfs_volume_init
		};
		
		volume_type =
			g_type_register_static (G_TYPE_OBJECT, "GnomeVFSVolume",
						&volume_info, 0);
	}
	
	return volume_type;
}

static void
gnome_vfs_volume_class_init (GnomeVFSVolumeClass *class)
{
	GObjectClass *o_class;
	
	parent_class = g_type_class_peek_parent (class);
	
	o_class = (GObjectClass *) class;
	
	/* GObject signals */
	o_class->finalize = gnome_vfs_volume_finalize;
}

static void
gnome_vfs_volume_init (GnomeVFSVolume *volume)
{
	static int volume_id_counter = 1;
	
	volume->priv = g_new0 (GnomeVFSVolumePrivate, 1);

	G_LOCK (volumes);
	volume->priv->id = volume_id_counter++;
	G_UNLOCK (volumes);
	
}

GnomeVFSVolume *
gnome_vfs_volume_ref (GnomeVFSVolume *volume)
{
	if (volume == NULL) {
		return NULL;
	}
		
	G_LOCK (volumes);
	g_object_ref (volume);
	G_UNLOCK (volumes);
	return volume;
}

void
gnome_vfs_volume_unref (GnomeVFSVolume *volume)
{
	if (volume == NULL) {
		return;
	}
	
	G_LOCK (volumes);
	g_object_unref (volume);
	G_UNLOCK (volumes);
}


/* Remeber that this could be running on a thread other
 * than the main thread */
static void
gnome_vfs_volume_finalize (GObject *object)
{
	GnomeVFSVolume *volume = (GnomeVFSVolume *) object;
	GnomeVFSVolumePrivate *priv;
	
	priv = volume->priv;

	/* The volume can't be finalized if there is a
	   drive that owns it */
	g_assert (priv->drive == NULL);
	
	g_free (priv->device_path);
	g_free (priv->activation_uri);
	g_free (priv->filesystem_type);
	g_free (priv->display_name);
	g_free (priv->icon);
	g_free (priv->gconf_id);
	g_free (priv);
	volume->priv = NULL;
	
	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

GnomeVFSVolumeType
gnome_vfs_volume_get_volume_type (GnomeVFSVolume *volume)
{
	return volume->priv->volume_type;
}

GnomeVFSDeviceType
gnome_vfs_volume_get_device_type (GnomeVFSVolume *volume)
{
	return volume->priv->device_type;
}

GnomeVFSDrive *
gnome_vfs_volume_get_drive (GnomeVFSVolume *volume)
{
	GnomeVFSDrive *drive;
	
	G_LOCK (volumes);
	drive = gnome_vfs_drive_ref (volume->priv->drive);
	G_UNLOCK (volumes);
	
	return drive;
}

void
_gnome_vfs_volume_unset_drive (GnomeVFSVolume     *volume,
			       GnomeVFSDrive      *drive)
{
	G_LOCK (volumes);
	g_assert (volume->priv->drive == drive);
	volume->priv->drive = NULL;
	G_UNLOCK (volumes);
}

void
_gnome_vfs_volume_set_drive           (GnomeVFSVolume     *volume,
				       GnomeVFSDrive      *drive)
{
	G_LOCK (volumes);
	g_assert (volume->priv->drive == NULL);
	volume->priv->drive = drive;
	G_UNLOCK (volumes);
}

char *
gnome_vfs_volume_get_device_path (GnomeVFSVolume *volume)
{
	return g_strdup (volume->priv->device_path);
}

char *
gnome_vfs_volume_get_activation_uri (GnomeVFSVolume *volume)
{
	return g_strdup (volume->priv->activation_uri);
}

char *
gnome_vfs_volume_get_filesystem_type (GnomeVFSVolume *volume)
{
	return g_strdup (volume->priv->filesystem_type);
}

char *
gnome_vfs_volume_get_display_name (GnomeVFSVolume *volume)
{
	return g_strdup (volume->priv->display_name);
}

char *
gnome_vfs_volume_get_icon (GnomeVFSVolume *volume)
{
	return g_strdup (volume->priv->icon);
}

gboolean
gnome_vfs_volume_is_user_visible (GnomeVFSVolume *volume)
{
	return volume->priv->is_user_visible;
}

gboolean
gnome_vfs_volume_is_read_only (GnomeVFSVolume *volume)
{
	return volume->priv->is_read_only;
}

gboolean
gnome_vfs_volume_is_mounted (GnomeVFSVolume *volume)
{
	return volume->priv->is_mounted;
}

gboolean
gnome_vfs_volume_handles_trash (GnomeVFSVolume *volume)
{
	if (volume->priv->device_type == GNOME_VFS_DEVICE_TYPE_AUTOFS) {
		return FALSE;
	}
	if (volume->priv->filesystem_type != NULL) {
		return _gnome_vfs_filesystem_use_trash (volume->priv->filesystem_type);
	}
	return FALSE;
}
