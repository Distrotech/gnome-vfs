/* -*- Mode: IDL; tab-width: 8; indent-tabs-mode: 8; c-basic-offset: 8 -*- */

/* gnome-vfs-metafile.h - server side of GnomeVFS::MetafileFactory
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

#ifndef GNOME_VFS_METAFILE_FACTORY_H
#define GNOME_VFS_METAFILE_FACTORY_H

#include "gnome-vfs-metafile-server.h"

#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-xobject.h>

#define GNOME_VFS_TYPE_METAFILE_FACTORY	          (gnome_vfs_metafile_factory_get_type ())
#define GNOME_VFS_METAFILE_FACTORY(obj)	          (GTK_CHECK_CAST ((obj), GNOME_VFS_TYPE_METAFILE_FACTORY, GnomeVFSMetafileFactory))
#define GNOME_VFS_METAFILE_FACTORY_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GNOME_VFS_TYPE_METAFILE_FACTORY, GnomeVFSMetafileFactoryClass))
#define GNOME_VFS_IS_METAFILE_FACTORY(obj)         (GTK_CHECK_TYPE ((obj), GNOME_VFS_TYPE_METAFILE_FACTORY))
#define GNOME_VFS_IS_METAFILE_FACTORY_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GNOME_VFS_TYPE_METAFILE_FACTORY))

typedef struct GnomeVFSMetafileFactoryDetails GnomeVFSMetafileFactoryDetails;

typedef struct {
	BonoboXObject parent_slot;
	GnomeVFSMetafileFactoryDetails *details;
} GnomeVFSMetafileFactory;

typedef struct {
	BonoboXObjectClass parent_slot;
	POA_GnomeVFS_MetafileFactory__epv epv;
} GnomeVFSMetafileFactoryClass;

GtkType gnome_vfs_metafile_factory_get_type (void);


#define METAFILE_FACTORY_IID "OAFIID:gnome_vfs_metafile_factory:f2a7b643-15f2-4110-829b-33d2de8cf200"

GnomeVFSMetafileFactory *gnome_vfs_metafile_factory_get_instance (void);

#endif /* GNOME_VFS_METAFILE_FACTORY_H */
