/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   gnome-vfs-metadata.h - functions for manipulating extra information
                          attached to files and directories ("metadata")
 
   Copyright (C) 2001 Free Software Foundation
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
  
   Author: Seth Nickell <snickell@stanford.edu>
*/

#ifndef GNOME_VFS_METADATA_H
#define GNOME_VFS_METADATA_H

#include <libgnomevfs/gnome-vfs-uri.h>
#include <glib.h>

typedef struct {
  int ref;
  GNOME_VFS_Metafile metafile_object;
} GnomeVFSMetafile;

typedef enum {
	GNOME_VFS_METAFILE_LOAD_COMPLETE = 0,
	GNOME_VFS_METAFILE_CACHE_FIELDS = 1 << 0
} GnomeVFSMetafileLoadOptions;

GnomeVFSResult gnome_vfs_metafile_load  (GnomeVFSMetafile **metafile,
					 const char *directory_uri,
					 GnomeVFSMetafileLoadOptions options);

void  gnome_vfs_metafile_unref          (GnomeVFSMetafile *metafile);



char* gnome_vfs_metadata_get_string     (GnomeVFSMetafile *metafile,
					 const char *file_name,
					 const char *key,
					 const char *default_value);
char* gnome_vfs_metadata_get_integer    (GnomeVFSMetafile *metafile,
					 const char *file_name,
					 const char *key,
					 int default_value);
char* gnome_vfs_metadata_get_boolean    (GnomeVFSMetafile *metafile,
					 const char *file_name,
					 const char *key,
					 gboolean default_value);


void  gnome_vfs_metadata_set_string     (GnomeVFSMetafile *metafile,
					 const char *file_name,
					 const char *key,
					 const char *value);
void  gnome_vfs_metadata_set_integer    (GnomeVFSMetafile *metafile,
					 const char *file_name,
					 const char *key,
					 int value);
void  gnome_vfs_metadata_set_boolean    (GnomeVFSMetafile *metafile,
					 const char *file_name,
					 const char *key,
					 gboolean value);
#endif
