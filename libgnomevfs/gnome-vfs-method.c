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

   Author: Ettore Perazzoli <ettore@gnu.org> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gmodule.h>

#include "gnome-vfs-module.h"


struct _MethodElement {
	gchar *name;
	GnomeVFSMethod *method;
};
typedef struct _MethodElement MethodElement;

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

static GnomeVFSMethod *
module_get_sane_handle (gchar *module_name)
{
	GnomeVFSMethod *method;
	GModule        *module;
	GnomeVFSMethod * (*init_function) (void) = NULL;
	void           * (*shutdown_function) (GnomeVFSMethod *) = NULL;
	
	module = g_module_open (module_name, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_warning ("Cannot load module `%s'", module_name);
		return NULL;
	}

	if (! g_module_symbol (module, GNOME_VFS_MODULE_INIT, (gpointer *) &init_function) ||
	    !init_function) {
		g_warning ("module '%s' has no init fn", module_name);
		return NULL;
	}

	if (! g_module_symbol (module, GNOME_VFS_MODULE_SHUTDOWN, (gpointer *) &shutdown_function) ||
	    !shutdown_function)
		g_warning ("module '%s' has no shutdown fn", module_name);

	method = init_function ();

	if (method == NULL) {
		g_warning ("module '%s' returned a NULL handle", module_name);
		return NULL;
	}

	/* Some basic checks */
	if (!method->open) {
		g_warning ("module '%s' has no open fn", module_name);
		return NULL;
	} else if (!method->create) {
		g_warning ("module '%s' has no create fn", module_name);
		return NULL;
	} else if (!method->is_local) {
		g_warning ("module '%s' has no is-local fn", module_name);
		return NULL;
	}
#if 0
	else if (!method->get_file_info) {
		g_warning ("module '%s' has no get-file-info fn", module_name);
		return NULL;
	}
#endif

	/* More advanced assumptions */
	if (!method->tell && method->seek) {
		g_warning ("module '%s' has seek and no tell", module_name);
		return NULL;
	}

	return method;
}

GnomeVFSMethod *
gnome_vfs_method_get (const gchar *name)
{
	GnomeVFSMethod *method;
	MethodElement *method_element;
	const gchar *base_module_name;
	gchar *module_name;

	g_return_val_if_fail (name != NULL, NULL);

	G_LOCK (method_hash);
	method_element = g_hash_table_lookup (method_hash, name);
	G_UNLOCK (method_hash);

	if (method_element != NULL)
		return method_element->method;

	base_module_name = gnome_vfs_configuration_get_module_path (name);
	if (base_module_name == NULL)
		return NULL;

	if (g_path_is_absolute (base_module_name)) {
		module_name = g_strdup (base_module_name);
	} else {
		module_name = g_strconcat (GNOME_VFS_MODULE_DIR,
					   "/", base_module_name, NULL);
	}

	method = module_get_sane_handle (module_name);
	g_free (module_name);

	if (method == NULL)
		return NULL;

	method_element = g_new (MethodElement, 1);
	method_element->name = g_strdup (name);
	method_element->method = method;

	G_LOCK (method_hash);
	g_hash_table_insert (method_hash, method_element->name, method_element);
	G_UNLOCK (method_hash);

	return method;
}
