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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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

static GList *module_path_list = NULL;
G_LOCK_DEFINE_STATIC (module_path_list);


static gboolean
init_hash_table (void)
{
	G_LOCK (method_hash);
	method_hash = g_hash_table_new (g_str_hash, g_str_equal);
	G_UNLOCK (method_hash);

	return TRUE;
}

static gboolean
install_path_list (const gchar *user_path_list)
{
	const gchar *p, *oldp;

	/* Notice that this assumes the list has already been locked.  */

	oldp = user_path_list;
	while (1) {
		gchar *elem;

		p = strchr (oldp, ':');

		if (p == NULL) {
			if (*oldp != '\0') {
				elem = g_strdup (oldp);
				module_path_list = g_list_append
						       (module_path_list, elem);
			}
			break;
		} else if (p != oldp) {
			elem = g_strndup (oldp, p - oldp);
			module_path_list = g_list_append (module_path_list,
							  elem);
		} else {
			elem = NULL;
		}

		oldp = p + 1;
	}

	return TRUE;
}

static gboolean
init_path_list (void)
{
	const gchar *user_path_list;
	gboolean retval;

	retval = TRUE;

	G_LOCK (module_path_list);

	if (module_path_list != NULL) {
		retval = FALSE;
		goto end;
	}

	/* User-supplied path.  */

	user_path_list = getenv ("GNOME_VFS_MODULE_PATH");
	if (user_path_list != NULL) {
		if (! install_path_list (user_path_list)) {
			retval = FALSE;
			goto end;
		}
	}

	/* Default path.  It comes last so that users can override it.  */

	module_path_list = g_list_append (module_path_list,
					  g_strdup (GNOME_VFS_MODULE_DIR));

 end:
	G_UNLOCK (module_path_list);
	return retval;
}

gboolean
gnome_vfs_method_init (void)
{
	if (! init_hash_table ())
		return FALSE;
	if (! init_path_list ())
		return FALSE;

	return TRUE;
}

static GnomeVFSMethod *
load_module (const gchar *module_name)
{
	GnomeVFSMethod *method;
	GModule        *module;
	GnomeVFSMethod * (*init_function) (void) = NULL;
	void           * (*shutdown_function) (GnomeVFSMethod *) = NULL;

	/* FIXME */
	g_warning ("Loading module `%s'", module_name);

	module = g_module_open (module_name, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_warning ("Cannot load module `%s'", module_name);
		return NULL;
	}

	if (! g_module_symbol (module, GNOME_VFS_MODULE_INIT,
			       (gpointer *) &init_function)
	    || init_function == NULL) {
		g_warning ("module '%s' has no init fn", module_name);
		return NULL;
	}

	if (! g_module_symbol (module, GNOME_VFS_MODULE_SHUTDOWN,
			       (gpointer *) &shutdown_function)
	    || shutdown_function == NULL)
		g_warning ("module '%s' has no shutdown fn", module_name);

	method = init_function ();

	if (method == NULL) {
		g_warning ("module '%s' returned a NULL handle", module_name);
		return NULL;
	}

	/* Some basic checks */
	if (method->open == NULL) {
		g_warning ("module '%s' has no open fn", module_name);
		return NULL;
#if 0
	} else if (method->create == NULL) {
		g_warning ("module '%s' has no create fn", module_name);
		return NULL;
#endif
	} else if (method->is_local == NULL) {
		g_warning ("module '%s' has no is-local fn", module_name);
		return NULL;
	}
#if 0
	else if (method->get_file_info == NULL) {
		g_warning ("module '%s' has no get-file-info fn", module_name);
		return NULL;
	}
#endif

	/* More advanced assumptions.  */
	if (method->tell != NULL && method->seek == NULL) {
		g_warning ("module '%s' has seek and no tell", module_name);
		return NULL;
	}

	return method;
}

static GnomeVFSMethod *
load_module_in_path_list (const gchar *base_name)
{
	GList *p;

	for (p = module_path_list; p != NULL; p = p->next) {
		GnomeVFSMethod *method;
		const gchar *path;
		gchar *name;

		path = p->data;
		name = g_strconcat (path, "/", base_name, NULL);

		method = load_module (name);

		g_free (name);

		if (method != NULL)
			return method;
	}

	return NULL;
}

GnomeVFSMethod *
gnome_vfs_method_get (const gchar *name)
{
	GnomeVFSMethod *method;
	MethodElement *method_element;
	const gchar *module_name;
	pid_t saved_uid;
	gid_t saved_gid;

	g_return_val_if_fail (name != NULL, NULL);

	G_LOCK (method_hash);
	method_element = g_hash_table_lookup (method_hash, name);
	G_UNLOCK (method_hash);

	if (method_element != NULL)
		return method_element->method;

	module_name = gnome_vfs_configuration_get_module_path (name);
	if (module_name == NULL)
		return NULL;

	/* Set the effective UID/GID to the user UID/GID to prevent attacks to
           setuid/setgid executables.  */

	saved_uid = geteuid ();
	saved_gid = getegid ();
	seteuid (getuid ());
	setegid (getgid ());

	if (g_path_is_absolute (module_name))
		method = load_module (module_name);
	else
		method = load_module_in_path_list (module_name);

	seteuid (saved_uid);
	setegid (saved_gid);

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
