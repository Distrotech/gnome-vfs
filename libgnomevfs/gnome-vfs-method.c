/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-method.c - Handling of access methods in the GNOME
   Virtual File System.

   Copyright (C) 1999 Free Software Foundation

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

   Author: Ettore Perazzoli <ettore@comm2000.it> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gmodule.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


static GHashTable *method_hash = NULL;
G_LOCK_DEFINE_STATIC (method_hash);


gboolean
gnome_vfs_method_init (void)
{
	G_LOCK (method_hash);
	method_hash = g_hash_table_new (g_str_hash, g_str_equal);
	G_UNLOCK (method_hash);

	return TRUE;
}

GnomeVFSMethod *
gnome_vfs_method_get (const gchar *name)
{
	GnomeVFSMethod *method;
	GnomeVFSMethod * (*init_function) (void);
	GModule *module;
	gchar *module_name;

	g_return_val_if_fail (name != NULL, NULL);

	G_LOCK (method_hash);
	method = g_hash_table_lookup (method_hash, name);
	G_UNLOCK (method_hash);

	if (method != NULL)
		return method;

	module_name = g_strconcat (PREFIX "/lib/vfs/modules/lib", name, ".so",
				   NULL);
	module = g_module_open (module_name, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_warning ("Cannot load module `%s'", module_name);
		return NULL;
	}

	g_free (module_name);

	if (! g_module_symbol (module, "init", (gpointer *) &init_function))
		return NULL;

	method = init_function ();
	if (method == NULL)
		return NULL;

	/* FIXME: Check method for sanity.  */

	G_LOCK (method_hash);
	g_hash_table_insert (method_hash, g_strdup (name), method);
	G_UNLOCK (method_hash);

	return method;
}
