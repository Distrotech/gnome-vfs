/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-drive.c - Handling of drives for the GNOME Virtual File System.

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
#include <glib/gthread.h>

#include "gnome-vfs-drive.h"
#include "gnome-vfs-volume-monitor-private.h"

static void     gnome_vfs_drive_class_init           (GnomeVFSDriveClass *klass);
static void     gnome_vfs_drive_init                 (GnomeVFSDrive      *drive);
static void     gnome_vfs_drive_finalize             (GObject          *object);

enum
{
	VOLUME_MOUNTED,
	VOLUME_PRE_UNMOUNT,
	VOLUME_UNMOUNTED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint drive_signals [LAST_SIGNAL] = { 0 };

G_LOCK_DEFINE_STATIC (drives);

GType
gnome_vfs_drive_get_type (void)
{
	static GType drive_type = 0;

	if (!drive_type) {
		static const GTypeInfo drive_info = {
			sizeof (GnomeVFSDriveClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gnome_vfs_drive_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GnomeVFSDrive),
			0,              /* n_preallocs */
			(GInstanceInitFunc) gnome_vfs_drive_init
		};
		
		drive_type =
			g_type_register_static (G_TYPE_OBJECT, "GnomeVFSDrive",
						&drive_info, 0);
	}
	
	return drive_type;
}

static void
gnome_vfs_drive_class_init (GnomeVFSDriveClass *class)
{
	GObjectClass *o_class;
	
	parent_class = g_type_class_peek_parent (class);
	
	o_class = (GObjectClass *) class;
	
	/* GObject signals */
	o_class->finalize = gnome_vfs_drive_finalize;

	drive_signals[VOLUME_MOUNTED] =
		g_signal_new ("volume_mounted",
			      G_TYPE_FROM_CLASS (o_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GnomeVFSDriveClass, volume_mounted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      GNOME_VFS_TYPE_VOLUME);

	drive_signals[VOLUME_PRE_UNMOUNT] =
		g_signal_new ("volume_pre_unmount",
			      G_TYPE_FROM_CLASS (o_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GnomeVFSDriveClass, volume_pre_unmount),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      GNOME_VFS_TYPE_VOLUME);

	drive_signals[VOLUME_UNMOUNTED] =
		g_signal_new ("volume_unmounted",
			      G_TYPE_FROM_CLASS (o_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GnomeVFSDriveClass, volume_unmounted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      GNOME_VFS_TYPE_VOLUME);
}

static void
gnome_vfs_drive_init (GnomeVFSDrive *drive)
{
	static int drive_id_counter = 1;
	
	drive->priv = g_new0 (GnomeVFSDrivePrivate, 1);

	G_LOCK (drives);
	drive->priv->id = drive_id_counter++;
	G_UNLOCK (drives);
}

GnomeVFSDrive *
gnome_vfs_drive_ref (GnomeVFSDrive *drive)
{
	if (drive == NULL) {
		return NULL;
	}

	G_LOCK (drives);
	g_object_ref (drive);
	G_UNLOCK (drives);
	return drive;
}

void
gnome_vfs_drive_unref (GnomeVFSDrive *drive)
{
	if (drive == NULL) {
		return;
	}
	
	G_LOCK (drives);
	g_object_unref (drive);
	G_UNLOCK (drives);
}


/* Remeber that this could be running on a thread other
 * than the main thread */
static void
gnome_vfs_drive_finalize (GObject *object)
{
	GnomeVFSDrive *drive = (GnomeVFSDrive *) object;
	GnomeVFSDrivePrivate *priv;

	priv = drive->priv;

	if (priv->volume) {
		_gnome_vfs_volume_unset_drive (priv->volume,
					       drive);
		gnome_vfs_volume_unref (priv->volume);
	}
	g_free (priv->device_path);
	g_free (priv->activation_uri);
	g_free (priv->display_name);
	g_free (priv->icon);
	g_free (priv);
	drive->priv = NULL;
	
	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

gulong 
gnome_vfs_drive_get_id (GnomeVFSDrive *drive)
{
	return drive->priv->id;
}

GnomeVFSDeviceType
gnome_vfs_drive_get_device_type (GnomeVFSDrive *drive)
{
	return drive->priv->device_type;
}

GnomeVFSVolume *
gnome_vfs_drive_get_mounted_volume (GnomeVFSDrive *drive)
{
	GnomeVFSVolume *vol;
	
	G_LOCK (drives);
	vol = gnome_vfs_volume_ref (drive->priv->volume);
	G_UNLOCK (drives);

	return vol;
}

gboolean
gnome_vfs_drive_is_mounted (GnomeVFSDrive *drive)
{
	gboolean res;
	
	G_LOCK (drives);
	res = drive->priv->volume != NULL;
	G_UNLOCK (drives);
	
	return res;
}


void
_gnome_vfs_drive_unset_volume (GnomeVFSDrive      *drive,
			       GnomeVFSVolume     *volume)
{
	G_LOCK (drives);
	g_assert (drive->priv->volume == volume);
	drive->priv->volume = NULL;
	G_UNLOCK (drives);
	gnome_vfs_volume_unref (volume);
}

void
_gnome_vfs_drive_set_mounted_volume  (GnomeVFSDrive      *drive,
				      GnomeVFSVolume     *volume)
{
	G_LOCK (drives);
	g_assert (drive->priv->volume == NULL);
	
	drive->priv->volume = gnome_vfs_volume_ref (volume);
	G_UNLOCK (drives);
}

char *
gnome_vfs_drive_get_device_path (GnomeVFSDrive *drive)
{
	return g_strdup (drive->priv->device_path);
}


char *
gnome_vfs_drive_get_activation_uri (GnomeVFSDrive *drive)
{
	return g_strdup (drive->priv->activation_uri);
}

char *
gnome_vfs_drive_get_display_name (GnomeVFSDrive *drive)
{
	return g_strdup (drive->priv->display_name);
}

char *
gnome_vfs_drive_get_icon (GnomeVFSDrive *drive)
{
	return g_strdup (drive->priv->icon);
}

gboolean
gnome_vfs_drive_is_user_visible (GnomeVFSDrive *drive)
{
	return drive->priv->is_user_visible;
}

gboolean
gnome_vfs_drive_is_connected (GnomeVFSDrive *drive)
{
	return drive->priv->is_connected;
}

gint
gnome_vfs_drive_compare (GnomeVFSDrive *a,
			 GnomeVFSDrive *b)
{
	GnomeVFSDrivePrivate *priva, *privb;
	gint res;
	
	priva = a->priv;
	privb = b->priv;

	res = _gnome_vfs_device_type_get_sort_group (priva->device_type) - _gnome_vfs_device_type_get_sort_group (privb->device_type);
	if (res != 0) {
		return res;
	}

	res = strcmp (priva->display_name, privb->display_name);
	if (res != 0) {
		return res;
	}
	
	return privb->id - priva->id;
}

static CORBA_char *
corba_string_or_null_dup (char *str)
{
	if (str != NULL) {
		return CORBA_string_dup (str);
	} else {
		return CORBA_string_dup ("");
	}
}

/* empty string interpreted as NULL */
static char *
decode_corba_string_or_null (CORBA_char *str, gboolean empty_is_null)
{
	if (empty_is_null && *str == 0) {
		return NULL;
	} else {
		return g_strdup (str);
	}
}

void
_gnome_vfs_drive_to_corba (GnomeVFSDrive *drive,
			   GNOME_VFS_Drive *corba_drive)
{
	GnomeVFSVolume *volume;

	corba_drive->id = drive->priv->id;
	corba_drive->device_type = drive->priv->device_type;
	volume = gnome_vfs_drive_get_mounted_volume (drive);
	if (volume != NULL) {
		corba_drive->volume = volume->priv->id;
		gnome_vfs_volume_unref (volume);
	} else {
		corba_drive->volume = 0;
	}
	corba_drive->device_path = corba_string_or_null_dup (drive->priv->device_path);
	corba_drive->activation_uri = corba_string_or_null_dup (drive->priv->activation_uri);
	corba_drive->display_name = corba_string_or_null_dup (drive->priv->display_name);
	corba_drive->icon = corba_string_or_null_dup (drive->priv->icon);
	
	corba_drive->is_user_visible = drive->priv->is_user_visible;
	corba_drive->is_connected = drive->priv->is_connected;
}

GnomeVFSDrive *
_gnome_vfs_drive_from_corba (const GNOME_VFS_Drive *corba_drive,
			     GnomeVFSVolumeMonitor *volume_monitor)
{
	GnomeVFSDrive *drive;

	drive = g_object_new (GNOME_VFS_TYPE_DRIVE, NULL);
	
	drive->priv->id = corba_drive->id;
	drive->priv->device_type = corba_drive->device_type;

	if (corba_drive->volume != 0) {
		drive->priv->volume = gnome_vfs_volume_monitor_get_volume_by_id (volume_monitor,
										 corba_drive->volume);
		if (drive->priv->volume != NULL) {
			_gnome_vfs_volume_set_drive (drive->priv->volume, drive);
		}
	}
								  
	drive->priv->device_path = decode_corba_string_or_null (corba_drive->device_path, TRUE);
	drive->priv->activation_uri = decode_corba_string_or_null (corba_drive->activation_uri, TRUE);
	drive->priv->display_name = decode_corba_string_or_null (corba_drive->display_name, TRUE);
	drive->priv->icon = decode_corba_string_or_null (corba_drive->icon, TRUE);
	
	drive->priv->is_user_visible = corba_drive->is_user_visible;
	drive->priv->is_connected = corba_drive->is_connected;

	return drive;
}
