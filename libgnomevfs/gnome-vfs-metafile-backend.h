/*
 * gnome-vfs-metafile-backend.h - server side of GnomeVFS::Metafile
 *
 * Copyright (C) 2001 Eazel, Inc.
 * Copyright (C) 2001 Free Software Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef GNOME_VFS_METAFILE_H
#define GNOME_VFS_METAFILE_H

#include <glib/gmacros.h>
#include "gnome-vfs-metafile-server.h"
#include <bonobo/bonobo-object.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

#define GNOME_VFS_METAFILE_TYPE        (gnome_vfs_mime_monitor_get_type ())
#define GNOME_VFS_METAFILE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_VFS_METAFILE_TYPE, GnomeVFSMIMEMonitor))
#define GNOME_VFS_METAFILE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GNOME_VFS_METAFILE_TYPE, GnomeVFSMIMEMonitorClass))
#define GNOME_VFS_IS_METAFILE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_VFS_METAFILE_TYPE))
#define GNOME_VFS_IS_METAFILE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_VFS_METAFILE_TYPE))

typedef struct GnomeVFSMetafileDetails GnomeVFSMetafileDetails;

typedef struct {
	BonoboObject parent_slot;
	GnomeVFSMetafileDetails *details;
} GnomeVFSMetafile;

typedef struct {
	BonoboObjectClass parent_slot;

	POA_GNOME_VFS_Metafile__epv epv;
} GnomeVFSMetafileClass;

GType             gnome_vfs_metafile_get_type (void);

GnomeVFSMetafile *gnome_vfs_metafile_get      (const char *directory_uri);

/* Specifications for in-directory metafile. */
#define GNOME_VFS_METAFILE_NAME_SUFFIX ".gnome-metadata.xml"

G_END_DECLS

#endif /* GNOME_VFS_METAFILE_H */
