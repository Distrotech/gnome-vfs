/* -*- Mode: IDL; tab-width: 8; indent-tabs-mode: 8; c-basic-offset: 8 -*- */

/* gnome-vfs-metafile.h - server side of GnomeVFS::Metafile
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

#include "gnome-vfs-metafile-server.h"
#include <gtk/gtk.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-xobject.h>
#include <gnome-xml/tree.h>

#define GNOME_VFS_TYPE_METAFILE	          (gnome_vfs_metafile_get_type ())
#define GNOME_VFS_METAFILE(obj)	          (GTK_CHECK_CAST ((obj), GNOME_VFS_TYPE_METAFILE, GnomeVFSMetafile))
#define GNOME_VFS_METAFILE_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GNOME_VFS_TYPE_METAFILE, GnomeVFSMetafileClass))
#define GNOME_VFS_IS_METAFILE(obj)         (GTK_CHECK_TYPE ((obj), GNOME_VFS_TYPE_METAFILE))
#define GNOME_VFS_IS_METAFILE_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GNOME_VFS_TYPE_METAFILE))

typedef struct GnomeVFSMetafileDetails GnomeVFSMetafileDetails;

typedef struct {
	BonoboXObject parent_slot;
	GnomeVFSMetafileDetails *details;
} GnomeVFSMetafile;

typedef struct {
	BonoboXObjectClass parent_slot;
	POA_GnomeVFS_Metafile__epv epv;
} GnomeVFSMetafileClass;

GtkType gnome_vfs_metafile_get_type (void);

GnomeVFSMetafile *gnome_vfs_metafile_get (const char *directory_uri);

/* Specifications for in-directory metafile. */
#define GNOME_VFS_METAFILE_NAME_SUFFIX ".gnome-metadata.xml"

#endif /* GNOME_VFS_METAFILE_H */
