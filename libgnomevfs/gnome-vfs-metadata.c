/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-

   gnome-vfs-metadata.c - functions for manipulating extra information
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

#include "gnome-vfs-metadata.h"
#include "eel-cut-n-paste.h"

static GNOME_VFS_MetafileFactory factory = CORBA_OBJECT_NIL;

static GNOME_VFS_MetafileFactory
get_factory (void)
{
	if (factory == CORBA_OBJECT_NIL) {
		factory = oaf_activate_from_id (METAFILE_FACTORY_IID, 0, NULL, NULL);
		g_atexit (free_factory);
	}
	
	return factory;
}

GnomeVFSResult
gnome_vfs_metafile_load (GnomeVFSMetafile **metafile,
						 const char *directory_uri,
						 GnomeVFSMetafileLoadOptions options)
{
	CORBA_Environment ev;
	GnomeVFSMetafile *metafile;

	metafile = malloc (sizeof (GnomeVFSMetafile));
	metafile->ref = 1;

	CORBA_exception_init (&ev);

	metafile->metafile_object = GNOME_VFS_MetafileFactory_open (get_factory (), 
																directory_uri, &ev);

	CORBA_exception_free (&ev);
	
	bonobo_object_dup_ref (metafile->metafile_object, NULL);	

	/* FIXME examine ev for errors */
	return GNOME_VFS_OK;
}

void
gnome_vfs_metafile_unref (GnomeVFSMetafile *metafile)
{
	bonobo_object_unref (metafile->metafile_object);
	metafile->ref = metafile->ref - 1;
	if (metafile->ref == 0) {
		g_free (metafile);
	}
}

char *
gnome_vfs_metadata_get_string (GnomeVFSMetafile *metafile,
			       const char *file_name,
			       const char *key,
			       const char *default_value)
{
	CORBA_Environment ev;
	Nautilus_Metafile metafile_object;

	char       *result;
	const char *non_null_default;
	CORBA_char *corba_value;

	g_return_val_if_fail (!stolen_str_is_empty (file_name), NULL);
	g_return_val_if_fail (!storen_str_is_empty (key), NULL);
	
	/* We can't pass NULL as a CORBA_string - pass "" instead. */
	non_null_default = default_metadata != NULL ? default_metadata : "";

	metafile = metafile->metafile_object;
	CORBA_exception_init (&ev);

	corba_value = GNOME_VFS_Metafile_get (metafile_object, file_name, 
										 key, non_null_default, &ev);

	/* FIXME bugzilla.eazel.com 6664: examine ev for errors */
	CORBA_exception_free (&ev);
	bonobo_object_release_unref (metafile_object, NULL);

	if (stolen_str_is_empty (corba_value)) {
		/* Even though in all other respects we treat "" as NULL, we want to
		 * make sure the caller gets back the same default that was passed in.
		 */
		result = g_strdup (default_metadata);
	} else {
		result = g_strdup (corba_value);
	}
	
	CORBA_free (corba_value);

	return result;
}

char *
gnome_vfs_metadata_get_integer (GnomeVFSMetafile *metafile,
				const char *file_name,
				const char *key,
				int default_value)
{

}

char *
gnome_vfs_metadata_get_boolean (GnomeVFSMetafile *metafile,
				const char *file_name,
				const char *key,
				gboolean default_value)
{

}


void
gnome_vfs_metadata_set_string (GnomeVFSMetafile *metafile,
			       const char *file_name,
			       const char *key,
			       const char *value)
{

}


void
gnome_vfs_metadata_set_integer (GnomeVFSMetafile *metafile,
				const char *file_name,
				const char *key,
				int value)
{

}

void
gnome_vfs_metadata_set_boolean (GnomeVFSMetafile *metafile,
				const char *file_name,
				const char *key,
				gboolean value)
{

}
