/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-configuration.c - Handling of the GNOME Virtual File System
   configuration.

   Copyright (C) 1999 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Ettore Perazzoli <ettore@comm2000.it> */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <glib.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


struct _Configuration {
	GHashTable *method_to_module_path;
};
typedef struct _Configuration Configuration;

struct _ModulePathElement {
	gchar *method_name;
	gchar *path;
};
typedef struct _ModulePathElement ModulePathElement;

static Configuration *configuration = NULL;
G_LOCK_DEFINE_STATIC (configuration);


static ModulePathElement *
module_path_element_new (const gchar *method_name,
			 const gchar *path)
{
	ModulePathElement *new;

	new = g_new (ModulePathElement, 1);
	new->method_name = g_strdup (method_name);
	new->path = g_strdup (path);

	return new;
}

static void
module_path_element_free (ModulePathElement *module_path)
{
	g_free (module_path->method_name);
	g_free (module_path->path);
	g_free (module_path);
}


static void
hash_free_module_path (gpointer key,
		       gpointer value,
		       gpointer user_data)
{
	ModulePathElement *module_path;

	module_path = (ModulePathElement *) value;
	module_path_element_free (module_path);
}

/* Destroy configuration information.  */
static void
configuration_destroy (Configuration *configuration)
{
	g_return_if_fail (configuration != NULL);

	g_hash_table_foreach (configuration->method_to_module_path,
			      hash_free_module_path, NULL);
	g_hash_table_destroy (configuration->method_to_module_path);
	g_free (configuration);
}


/* This reads a line and handles backslashes at the end of the line to join
   lines.  */
static gint
read_line (FILE *stream,
	   gchar **line_return,
	   guint *n,
	   guint *lines_read)
{
#define START_BUFFER_SIZE 1024
	gboolean backslash;
	gint pos;

	if (feof (stream))
		return -1;

	pos = 0;
	backslash = FALSE;
	*lines_read = 0;
	while (1) {
		int c;

		if (pos == *n) {
			if (*n == 0)
				*n = START_BUFFER_SIZE;
			else
				*n *= 2;
			*line_return = g_realloc (*line_return, *n);
		}

		c = fgetc (stream);
		if (c == '\n')
			(*lines_read)++;
		if (c == EOF || (c == '\n' && ! backslash)) {
			(*line_return)[pos] = 0;
			return pos;
		}

		if (c == '\\' && ! backslash) {
			backslash = TRUE;
		} else if (c != '\n') {
			if (backslash)
				(*line_return)[pos++] = '\\';
			(*line_return)[pos] = c;
			pos++;
			backslash = FALSE;
		}
	}
#undef START_BUFFER_SIZE
}

static void
remove_comment (gchar *buf)
{
	gchar *p;

	p = strchr (buf, '#');
	if (p != NULL)
		*p = '\0';
}

/* This is necessary as `g_strstrip()' only removes spaces.  */
static void
remove_trailing_tabs (gchar *buf)
{
	gchar *p;

	if (*buf == '\0')
		return;

	p = buf + strlen (buf) - 1;
	while (*p == '\t') {
		*p = '\0';
		p--;
	}
}

static gboolean
parse_line (Configuration *configuration,
	    gchar *line_buffer,
	    guint line_len,
	    const gchar *file_name,
	    guint line_number)
{
	guint string_len;
	gboolean retval;
	gchar *p;
	gchar *method_start;
	gchar *module_name;
	GList *method_list;
	GList *lp;

	string_len = strlen (line_buffer);
	if (string_len != line_len) {
		g_warning (_("%s:%d contains NUL characters."),
			   file_name, line_number);
		return FALSE;
	}

	remove_comment (line_buffer);
	line_buffer = g_strstrip (line_buffer);

	method_list = NULL;
	p = line_buffer;
	method_start = line_buffer;
	retval = TRUE;
	while (*p != '\0') {
		if (*p == ' ' || *p == '\t' || *p == ':') {
			gchar *method_name;

			if (p == method_start) {
				g_warning (_("%s:%d contains no method name."),
					   file_name, line_number);
				retval = FALSE;
				goto cleanup;
			}

			method_name = g_strndup (method_start,
						 p  - method_start);
			method_list = g_list_prepend (method_list, method_name);

			while (*p == ' ' || *p == '\t')
				p++;

			if (*p == ':') {
				p++;
				break;
			}
		}

		p++;
	}

	if (*p == '\0') {
		if (method_list != NULL) {
			g_warning (_("%s:%d contains no module name."),
				   file_name, line_number);
			retval = FALSE;
		} else {
			/* Empty line.  */
			retval = TRUE;
		}
		goto cleanup;
	}

	while (*p == ' ' || *p == '\t')
		p++;

	module_name = p;

	/* This is necessary as the `g_strstrip()' we called a few lines above
           only removes spaces.  */
	remove_trailing_tabs (module_name);

	for (lp = method_list; lp != NULL; lp = lp->next) {
		ModulePathElement *element;
		gchar *method_name;

		method_name = lp->data;
		element = module_path_element_new (method_name, module_name);
		g_hash_table_insert (configuration->method_to_module_path,
				     method_name, element);
	}

	retval = TRUE;

 cleanup:
	if (method_list != NULL)
		g_list_free (method_list);
	return retval;
}

/* FIXME maybe we should return FALSE if any errors during parsing happen so
   that we abort immediately, but this sounds a bit too overkill.  */
static gboolean
parse_file (Configuration *configuration,
	    const gchar *file_name)
{
	FILE *f;
	gchar *line_buffer;
	guint line_buffer_size;
	guint line_number;

	f = fopen (file_name, "r");
	if (f == NULL) {
		g_warning (_("Configuration file `%s' was not found: %s"),
			   file_name, strerror (errno));
		return FALSE;
	}

	line_buffer = NULL;
	line_buffer_size = 0;
	line_number = 0;
	while (1) {
		guint lines_read;
		gint line_len;

		line_len = read_line (f, &line_buffer, &line_buffer_size,
				      &lines_read);
		if (line_len == -1)
			break;	/* EOF */
		parse_line (configuration, line_buffer, line_len, file_name,
			    line_number);
		line_number += lines_read;
	}

	fclose (f);

	return TRUE;
}

/* Load configuration.  */
static Configuration *
configuration_load (void)
{
	Configuration *new;
	const gchar *file_name;

	file_name = GNOME_VFS_MODULE_CFGFILE;

	new = g_new (Configuration, 1);
	new->method_to_module_path = g_hash_table_new (g_str_hash, g_str_equal);

	if (! parse_file (new, file_name)) {
		configuration_destroy (new);
		return NULL;
	}

	return new;
}


gboolean
gnome_vfs_configuration_init (void)
{
	G_LOCK (configuration);
	if (configuration != NULL) {
		G_UNLOCK (configuration);
		return FALSE;
	}

	configuration = configuration_load ();
	G_UNLOCK (configuration);

	if (configuration == NULL)
		return FALSE;
	else
		return TRUE;
}

void
gnome_vfs_configuration_uninit (void)
{
	G_LOCK (configuration);
	if (configuration == NULL) {
		G_UNLOCK (configuration);
		return;
	}

	configuration_destroy (configuration);
	configuration = NULL;
	G_UNLOCK (configuration);
}

const gchar *
gnome_vfs_configuration_get_module_path (const gchar *method_name)
{
	ModulePathElement *element;

	g_return_val_if_fail (method_name != NULL, NULL);

	G_LOCK (configuration);
	if (configuration != NULL) {
		element = g_hash_table_lookup
			(configuration->method_to_module_path, method_name);
	} else {
		/* This should never happen.  */
		g_warning ("Internal error: the configuration system was not initialized.");
		element = NULL;
	}
	G_UNLOCK (configuration);

	if (element == NULL)
		return NULL;
	else
		return element->path;
}
