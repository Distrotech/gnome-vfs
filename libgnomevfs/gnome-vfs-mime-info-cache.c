/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sts=0 sw=8 tw=80 noexpandtab: */
/* gnome-vfs-mime-info.c - GNOME xdg mime information implementation.

 Copyright (C) 2004 Red Hat, Inc
 All rights reserved.
 
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
*/

#include <config.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <glib.h>
#include "gnome-vfs-mime-info-cache.h"
#include "eggdesktopentries.h"
#include "eggdirfuncs.h"

typedef struct {
	char *path;
	GHashTable *mime_info_cache_map;
	GHashTable *defaults_list_map;
	time_t mime_info_cache_timestamp;
        time_t mime_info_cache_last_stat_time;
	time_t defaults_list_timestamp;
	time_t defaults_list_last_stat_time;
} GnomeVFSMimeInfoCacheDir;

typedef struct {
	GList *dirs;               /* mimeinfo.cache and defaults.list */
	GList *defaults_list_dirs; /* defaults.list only dirs */
} GnomeVFSMimeInfoCache;

static void free_mime_info_cache_map_list (GList *list);
static void gnome_vfs_mime_info_cache_dir_load (GnomeVFSMimeInfoCacheDir *dir);
static void gnome_vfs_mime_info_cache_dir_load_defaults_list (GnomeVFSMimeInfoCacheDir *dir);
static GnomeVFSMimeInfoCacheDir * gnome_vfs_mime_info_cache_dir_new (const char *path);
static void gnome_vfs_mime_info_cache_dir_free (GnomeVFSMimeInfoCacheDir *dir);
static char **gnome_vfs_mime_info_cache_get_search_path (void);
static char **gnome_vfs_mime_info_cache_get_defaults_search_path (void);
;
static gboolean gnome_vfs_mime_info_cache_dir_desktop_entry_is_valid (GnomeVFSMimeInfoCacheDir *dir,
								      const char *desktop_entry);
static void gnome_vfs_mime_info_cache_dir_add_desktop_entries (GnomeVFSMimeInfoCacheDir *dir,
							       const char *mime_type,
							       char **new_desktop_file_ids);
static void gnome_vfs_mime_info_cache_load (void);
static GnomeVFSMimeInfoCache * gnome_vfs_mime_info_cache_new (void);
static void gnome_vfs_mime_info_cache_free (GnomeVFSMimeInfoCache *cache);

static GnomeVFSMimeInfoCache *mime_info_cache = NULL;
G_LOCK_DEFINE_STATIC (mime_info_cache);

static void
free_mime_info_cache_map_list (GList *list)
{
	g_list_foreach (list, (GFunc) g_free, NULL);
	g_list_free (list);
}

static gboolean
gnome_vfs_mime_info_cache_dir_out_of_date (GnomeVFSMimeInfoCacheDir *dir)
{
	struct stat buf;
	char *filename;
	struct timeval tv;
	time_t current_time;
	int retval = TRUE;

	if (dir->mime_info_cache_map == NULL) {
		return TRUE;
	}

	gettimeofday (&tv, NULL);
	current_time = tv.tv_sec;

	if (dir->mime_info_cache_last_stat_time == 0) {
		dir->mime_info_cache_last_stat_time = current_time;
		retval = TRUE;
	} else if (current_time > dir->mime_info_cache_last_stat_time + 5) {
		retval = TRUE;
	} else {
		retval = FALSE;
	}

	if (retval == FALSE)
		return retval;

	filename = g_build_filename (dir->path, "mimeinfo.cache", NULL);

	if (stat (filename, &buf) < 0) {
		g_free (filename);
		dir->mime_info_cache_last_stat_time = current_time;
		return TRUE;
	}
	g_free (filename);

	dir->mime_info_cache_last_stat_time = current_time;

	if (buf.st_mtime != dir->mime_info_cache_timestamp) 
		return TRUE;

	return FALSE;
}

static gboolean
gnome_vfs_mime_info_cache_dir_defaults_list_out_of_date (GnomeVFSMimeInfoCacheDir *dir)
{
	struct stat buf;
	char *filename;
	struct timeval tv;
	time_t current_time;
	int retval = TRUE;

	if (dir->defaults_list_map == NULL)
		return TRUE;

	gettimeofday (&tv, NULL);
	current_time = tv.tv_sec;

	if (dir->defaults_list_last_stat_time == 0) {
		dir->defaults_list_last_stat_time = current_time;
		retval = TRUE;
	} else if (current_time >= dir->defaults_list_last_stat_time + 5) {
		retval = TRUE;
	} else {
		retval = FALSE;
	}

	if (retval == FALSE)
		return retval;

	filename = g_build_filename (dir->path, "defaults.list", NULL);

	if (stat (filename, &buf) < 0) {
		g_free (filename);
		dir->defaults_list_last_stat_time = current_time;
		return TRUE;
	}
	g_free (filename);

	dir->defaults_list_last_stat_time = current_time;
	
	if (buf.st_mtime != dir->defaults_list_timestamp) {
		return TRUE;
	}

	return FALSE;
}

static void
gnome_vfs_mime_info_cache_dir_load (GnomeVFSMimeInfoCacheDir *dir)
{
	EggDesktopEntries *entries;
	GError *load_error;
	gchar *filename, **desktop_file_ids, **mime_types;
	int i;
	static gchar *allowed_start_groups[] = { "MIME Cache", NULL };
	struct stat buf;

	load_error = NULL;
	mime_types = NULL;
	entries = NULL;

	if (!gnome_vfs_mime_info_cache_dir_out_of_date (dir))
		return;
	
	if (dir->mime_info_cache_map != NULL) {
		g_hash_table_destroy (dir->mime_info_cache_map);
	}

	dir->mime_info_cache_map = g_hash_table_new_full (g_str_hash, g_str_equal,
							  (GDestroyNotify) g_free,
							  (GDestroyNotify) free_mime_info_cache_map_list);

	filename = g_build_filename (dir->path, "mimeinfo.cache", NULL);

	if (stat (filename, &buf) < 0)
		goto error;

	dir->mime_info_cache_timestamp = buf.st_mtime;

	entries =
		egg_desktop_entries_new_from_file (allowed_start_groups,
						   EGG_DESKTOP_ENTRIES_GENERATE_LOOKUP_MAP |
						   EGG_DESKTOP_ENTRIES_DISCARD_COMMENTS |
						   EGG_DESKTOP_ENTRIES_DISCARD_TRANSLATIONS,
						   filename,
						   &load_error);
	g_free (filename);
	filename = NULL;

	if (load_error != NULL)
		goto error;

	mime_types = egg_desktop_entries_get_keys (entries, "MIME Cache",
						   NULL, &load_error);

	if (load_error != NULL)
		goto error;

	for (i = 0; mime_types[i] != NULL; i++) {
		desktop_file_ids = egg_desktop_entries_get_string_list (entries,
									"MIME Cache",
									mime_types[i],
									NULL,
									&load_error);

		if (load_error != NULL) {
			g_error_free (load_error);
			load_error = NULL;
			continue;
		}

		gnome_vfs_mime_info_cache_dir_add_desktop_entries (dir,
								   mime_types[i],
								   desktop_file_ids);

		g_strfreev (desktop_file_ids);
	}

	g_strfreev (mime_types);
	egg_desktop_entries_free (entries);

	return;
error:
	if (filename)
		g_free (filename);

	if (entries != NULL)
		egg_desktop_entries_free (entries);

	if (mime_types != NULL)
		g_strfreev (mime_types);

	if (load_error)
		g_error_free (load_error);
}

static void
gnome_vfs_mime_info_cache_dir_load_defaults_list (GnomeVFSMimeInfoCacheDir *dir)
{
	EggDesktopEntries *entries;
	GError *load_error;
	gchar *filename, *desktop_file_id, **mime_types;
	int i;
	struct stat buf;
	static gchar *allowed_start_groups[] = { "MIME Cache", NULL };

	load_error = NULL;
	mime_types = NULL;
	entries = NULL;

	if (!gnome_vfs_mime_info_cache_dir_defaults_list_out_of_date (dir))
		return;

	if (dir->defaults_list_map != NULL) {
		g_hash_table_destroy (dir->defaults_list_map);
	}

	dir->defaults_list_map = g_hash_table_new_full (g_str_hash, g_str_equal,
							g_free, g_free);

	filename = g_build_filename (dir->path, "defaults.list", NULL);

	if (stat (filename, &buf) < 0)
		goto error;

	dir->defaults_list_timestamp = buf.st_mtime;

	entries =
		egg_desktop_entries_new_from_file (allowed_start_groups,
						   EGG_DESKTOP_ENTRIES_GENERATE_LOOKUP_MAP |
						   EGG_DESKTOP_ENTRIES_DISCARD_COMMENTS |
						   EGG_DESKTOP_ENTRIES_DISCARD_TRANSLATIONS,
						   filename,
						   &load_error);
	g_free (filename);
	filename = NULL;

	if (load_error != NULL)
		goto error;

	mime_types = egg_desktop_entries_get_keys (entries, "MIME Cache",
						   NULL, &load_error);

	if (load_error != NULL)
		goto error;

	for (i = 0; mime_types[i] != NULL; i++) {
		GList *tmp;

		desktop_file_id = egg_desktop_entries_get_string (entries,
								  "MIME Cache",
								  mime_types[i],
								  &load_error);
		if (load_error != NULL) {
			g_error_free (load_error);
			load_error = NULL;
			continue;
		}

		/* defaults.list in $XDG_CONFIG_DIRS/mime specify desktop
		 * files relative to $XDG_DATA_DIRS/applications, so we
		 * need to iterate through ever dir in $XDG_DATA_DIRS/applications
		 * looking for a valid desktop file
		 */
		if (dir->mime_info_cache_map == NULL) {
			G_LOCK (mime_info_cache);
			tmp = mime_info_cache->dirs;
			while (tmp != NULL) {
				GnomeVFSMimeInfoCacheDir *app_dir;

				app_dir = (GnomeVFSMimeInfoCacheDir *) tmp->data;
				if (gnome_vfs_mime_info_cache_dir_desktop_entry_is_valid (app_dir,
											  desktop_file_id)) {
					g_hash_table_replace (dir->defaults_list_map,
							      g_strdup (mime_types[i]),
							      desktop_file_id);
					break;
				}
				tmp = tmp->next;
			}
			G_UNLOCK (mime_info_cache);
		} else {
			if (gnome_vfs_mime_info_cache_dir_desktop_entry_is_valid (dir,
										  desktop_file_id)) {
				g_hash_table_replace (dir->defaults_list_map,
						      g_strdup (mime_types[i]),
						      desktop_file_id);
			}
		}
	}

	g_strfreev (mime_types);
	egg_desktop_entries_free (entries);

	return;
error:
	if (filename)
		g_free (filename);

	if (entries != NULL)
		egg_desktop_entries_free (entries);

	if (mime_types != NULL)
		g_strfreev (mime_types);

	if (load_error)
		g_error_free (load_error);
}


static GnomeVFSMimeInfoCacheDir *
gnome_vfs_mime_info_cache_dir_new (const char *path)
{
	GnomeVFSMimeInfoCacheDir *dir;

	dir = g_new0 (GnomeVFSMimeInfoCacheDir, 1);
	dir->path = g_strdup (path);

	return dir;
}

static void
gnome_vfs_mime_info_cache_dir_free (GnomeVFSMimeInfoCacheDir *dir)
{
	if (dir == NULL)
		return;
	if (dir->mime_info_cache_map != NULL) {
		g_hash_table_destroy (dir->mime_info_cache_map);
		dir->mime_info_cache_map = NULL;

	}

	if (dir->defaults_list_map != NULL) {
		g_hash_table_destroy (dir->defaults_list_map);
		dir->defaults_list_map = NULL;
	}

	g_free (dir);
}

static char **
gnome_vfs_mime_info_cache_get_search_path (void)
{
	char **args = NULL;
	char **data_dirs;
	char *user_data_dir;
	int i, length;

	data_dirs = egg_get_secondary_data_dirs ();

	for (length = 0; data_dirs[length] != NULL; length++);

	args = g_new (char *, length + 2);

	user_data_dir = egg_get_user_data_dir ();
	args[length] = g_build_filename (user_data_dir, "applications", NULL);
	g_free (user_data_dir);
	
	i = length - 1;
	while (i >= 0) {
		args[length - i - 1] = g_build_filename (data_dirs[i],
							 "applications", NULL);
		i--;
	}
	args[length + 1] = NULL;

	g_strfreev (data_dirs);

	return args;
}

static char **
gnome_vfs_mime_info_cache_get_defaults_search_path (void)
{
	char **args = NULL;
	char **config_dirs;
	int i, length;

	config_dirs = egg_get_secondary_configuration_dirs ();

	for (length = 0; config_dirs[length] != NULL; length++);

	args = g_new (char *, length + 1);

	i = length - 1;
	while (i >= 0) {
		args[length - i - 1] = g_build_filename (config_dirs[i],
							 "mime", NULL);
		i--;
	}
	args[length] = NULL;

	g_strfreev (config_dirs);

	return args;
}

static gboolean
gnome_vfs_mime_info_cache_dir_desktop_entry_is_valid (GnomeVFSMimeInfoCacheDir *dir,
                                                      const char *desktop_entry)
{
	EggDesktopEntries *entries;
	GError *load_error;
	int i;
	gboolean can_show_in;
	char *filename;

	load_error = NULL;
	can_show_in = TRUE;

	filename = g_build_filename ("applications", desktop_entry, NULL);
	entries =
		egg_desktop_entries_new_from_file (NULL,
						   EGG_DESKTOP_ENTRIES_DISCARD_COMMENTS |
						   EGG_DESKTOP_ENTRIES_DISCARD_TRANSLATIONS,
						   filename,
						   &load_error);
	g_free (filename);

	if (load_error != NULL) {
		g_error_free (load_error);
		return FALSE;
	}

	if (egg_desktop_entries_has_key (entries,
					 egg_desktop_entries_get_start_group (entries),
					 "OnlyShowIn")) {

		char **only_show_in_list;
		only_show_in_list = egg_desktop_entries_get_string_list (entries,
									 egg_desktop_entries_get_start_group (entries),
									 "OnlyShowIn",
									 NULL,
									 &load_error);

		if (load_error != NULL) {
			g_error_free (load_error);
			g_strfreev (only_show_in_list);
			egg_desktop_entries_free (entries);
			return FALSE;
		}

		can_show_in = FALSE;
		for (i = 0; only_show_in_list[i] != NULL; i++) {
			if (strcmp (only_show_in_list[i], "GNOME") == 0) {
				can_show_in = TRUE;
				break;
			}
		}

		g_strfreev (only_show_in_list);
	}

	if (egg_desktop_entries_has_key (entries,
					 egg_desktop_entries_get_start_group (entries),
					 "NotShowIn")) {
		char **not_show_in_list;
		not_show_in_list = egg_desktop_entries_get_string_list (entries,
									egg_desktop_entries_get_start_group (entries),
									"NotShowIn",
									NULL,
									&load_error);

		if (load_error != NULL) {
			g_error_free (load_error);
			return FALSE;
		}

		for (i = 0; not_show_in_list[i] != NULL; i++) {
			if (strcmp (not_show_in_list[i], "GNOME") == 0) {
				can_show_in = FALSE;
				break;
			}
		}

		g_strfreev (not_show_in_list);
	}

	egg_desktop_entries_free (entries);
	return can_show_in;
}

static void
gnome_vfs_mime_info_cache_dir_add_desktop_entries (GnomeVFSMimeInfoCacheDir *dir,
                                                   const char *mime_type,
                                                   char **new_desktop_file_ids)
{
	GList *desktop_file_ids;
	int i;

	desktop_file_ids = g_hash_table_lookup (dir->mime_info_cache_map,
						mime_type);

	for (i = 0; new_desktop_file_ids[i] != NULL; i++) {
		if (!g_list_find (desktop_file_ids, new_desktop_file_ids[i]) &&
		    gnome_vfs_mime_info_cache_dir_desktop_entry_is_valid (dir,
									  new_desktop_file_ids[i]))
			desktop_file_ids = g_list_append (desktop_file_ids,
							  g_strdup (new_desktop_file_ids[i]));
	}

	g_hash_table_insert (dir->mime_info_cache_map, g_strdup (mime_type), desktop_file_ids);
}

static void
gnome_vfs_mime_info_cache_load (void)
{
	G_LOCK (mime_info_cache);
	if (mime_info_cache == NULL) {
		char **dirs;
		int i;

		mime_info_cache = gnome_vfs_mime_info_cache_new ();

		dirs = gnome_vfs_mime_info_cache_get_search_path ();

		for (i = 0; dirs[i] != NULL; i++) {
			GnomeVFSMimeInfoCacheDir *dir;

			dir = gnome_vfs_mime_info_cache_dir_new (dirs[i]);

			if (dir != NULL) {
				gnome_vfs_mime_info_cache_dir_load (dir);
				gnome_vfs_mime_info_cache_dir_load_defaults_list (dir);

				mime_info_cache->dirs = g_list_append (mime_info_cache->dirs,
							  	       dir);
			}
		}
		g_strfreev (dirs);

		dirs = gnome_vfs_mime_info_cache_get_defaults_search_path ();

		for (i = 0; dirs[i] != NULL; i++) {
			GnomeVFSMimeInfoCacheDir *dir;

			dir = gnome_vfs_mime_info_cache_dir_new (dirs[i]);

			if (dir != NULL) {
				G_LOCK (mime_info_cache);
				mime_info_cache->defaults_list_dirs =
					g_list_append (mime_info_cache->defaults_list_dirs, dir);
				G_UNLOCK (mime_info_cache);
				gnome_vfs_mime_info_cache_dir_load_defaults_list (dir);
			}
		}

		g_strfreev (dirs);

	} else {
		GList *tmp;
		tmp = mime_info_cache->dirs;
		while (tmp != NULL) {
			GnomeVFSMimeInfoCacheDir *dir;

			dir = (GnomeVFSMimeInfoCacheDir *) tmp->data;
			gnome_vfs_mime_info_cache_dir_load (dir);
			gnome_vfs_mime_info_cache_dir_load_defaults_list (dir);

			tmp = tmp->next;
		}

		tmp = mime_info_cache->defaults_list_dirs;
		while (tmp != NULL) {
			GnomeVFSMimeInfoCacheDir *dir;

			dir = (GnomeVFSMimeInfoCacheDir *) tmp->data;
			gnome_vfs_mime_info_cache_dir_load_defaults_list (dir);

			tmp = tmp->next;
		}
	}
	G_UNLOCK (mime_info_cache);
}


static GnomeVFSMimeInfoCache *
gnome_vfs_mime_info_cache_new (void)
{
	GnomeVFSMimeInfoCache *cache;

	cache = g_new0 (GnomeVFSMimeInfoCache, 1);

	return cache;
}

static void
gnome_vfs_mime_info_cache_free (GnomeVFSMimeInfoCache *cache)
{
	if (cache == NULL)
		return;

	g_list_foreach (cache->dirs,
			(GFunc) gnome_vfs_mime_info_cache_dir_free,
			NULL);
	g_list_free (cache->dirs);
	g_list_foreach (cache->defaults_list_dirs,
			(GFunc) gnome_vfs_mime_info_cache_dir_free,
			NULL);
	g_list_free (cache->defaults_list_dirs);
}


void
gnome_vfs_mime_info_cache_flush (void)
{
	if (mime_info_cache != NULL) {
		G_LOCK (mime_info_cache);
		gnome_vfs_mime_info_cache_free (mime_info_cache);
		mime_info_cache = NULL;
		G_UNLOCK (mime_info_cache);
	}
}

GList *
gnome_vfs_mime_get_all_desktop_entries (const char *mime_type)
{
	GList *desktop_entries, *list, *dir_list, *tmp;

	gnome_vfs_mime_info_cache_load ();

	G_LOCK (mime_info_cache);
	dir_list = mime_info_cache->dirs;
	desktop_entries = NULL;
	while (dir_list != NULL) {
		GnomeVFSMimeInfoCacheDir *dir;

		dir = (GnomeVFSMimeInfoCacheDir *) dir_list->data;

		list = g_hash_table_lookup (dir->mime_info_cache_map, mime_type);

		tmp = list;
		while (tmp != NULL) {
			if (!g_list_find_custom (desktop_entries, tmp->data,
						 (GCompareFunc) strcmp)) {
				desktop_entries = g_list_prepend (desktop_entries,
								  g_strdup (tmp->data));
			}
			tmp = tmp->next;
		}
		dir_list = dir_list->next;
	}
	G_UNLOCK (mime_info_cache);

	desktop_entries = g_list_reverse (desktop_entries);

	return desktop_entries;
}

gchar *
gnome_vfs_mime_get_default_desktop_entry (const char *mime_type)
{
	gchar *desktop_entry;
	GList *dir_list;

	gnome_vfs_mime_info_cache_load ();

	G_LOCK (mime_info_cache);
	dir_list = mime_info_cache->defaults_list_dirs;
	desktop_entry = NULL;
	while (dir_list != NULL) {
		GnomeVFSMimeInfoCacheDir *dir;

		dir = (GnomeVFSMimeInfoCacheDir *) dir_list->data;

		desktop_entry = g_hash_table_lookup (dir->defaults_list_map,
						     mime_type);

		if (desktop_entry != NULL)
			break;

		dir_list = dir_list->next;
	}
	G_UNLOCK (mime_info_cache);

	if (desktop_entry != NULL)
		return desktop_entry;

	G_LOCK (mime_info_cache);
	dir_list = mime_info_cache->dirs;
	desktop_entry = NULL;
	while (dir_list != NULL) {
		GnomeVFSMimeInfoCacheDir *dir;

		dir = (GnomeVFSMimeInfoCacheDir *) dir_list->data;

		desktop_entry = g_hash_table_lookup (dir->defaults_list_map,
						     mime_type);

		if (desktop_entry != NULL)
			break;

		dir_list = dir_list->next;
	}
	G_UNLOCK (mime_info_cache);

	return desktop_entry;
}
