/* vi: set ts=8 sts=4 sw=8: */
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
#include <glib.h>
#include "gnome-vfs-mime-info-cache.h"
#include "eggdesktopentries.h"
#include "eggdirfuncs.h"

typedef struct {
      GHashTable *mime_types_map;
} GnomeVFSMimeInfoCache;

static char **gnome_vfs_mime_info_cache_get_search_path (GnomeVFSMimeInfoCache *cache);
static GnomeVFSMimeInfoCache *gnome_vfs_mime_info_cache_new ();
static void gnome_vfs_mime_info_cache_free (GnomeVFSMimeInfoCache *);
static void gnome_vfs_mime_info_cache_reload_dir (GnomeVFSMimeInfoCache *cache, const gchar *dir);
static void gnome_vfs_mime_info_cache_reload (GnomeVFSMimeInfoCache *cache);

static GnomeVFSMimeInfoCache *mime_info_cache = NULL;

static char **
gnome_vfs_mime_info_cache_get_search_path (GnomeVFSMimeInfoCache *cache)
{
      static char **args = NULL;
      char **data_dirs;
      int i, length;

      if (args != NULL)
            return args;

      data_dirs = egg_get_secondary_data_dirs ();

      for (length = 0; data_dirs[length] != NULL; length++);

      args = g_new (char *, length + 1);

      i = length - 1;
      while (i >= 0) {
            args[length - i - 1] = g_build_filename (data_dirs[i], "applications", NULL);
            i--;
      }
      args[length] = NULL;

      g_strfreev (data_dirs);

      return args;

}

static void
gnome_vfs_mime_info_cache_merge_mime_associations (GnomeVFSMimeInfoCache *cache,
                                         const gchar *mime_type,
                                         gchar **new_desktop_file_ids)
{
      GList *desktop_file_ids;
      int i;

      desktop_file_ids = g_hash_table_lookup (cache->mime_types_map,
                                              mime_type);

      for (i = 0; new_desktop_file_ids[i] != NULL; i++) {
            if (!g_list_find (desktop_file_ids, new_desktop_file_ids[i]))
                  desktop_file_ids = g_list_append (desktop_file_ids, g_strdup (new_desktop_file_ids[i]));
      }

      g_hash_table_insert (cache->mime_types_map, g_strdup (mime_type), desktop_file_ids);
}

static void
gnome_vfs_mime_info_cache_reload_dir (GnomeVFSMimeInfoCache *cache, const gchar *dir)
{
      GError *load_error;
      EggDesktopEntries *entries;
      gchar *filename, **desktop_file_ids, **mime_types;
      int i;
      static gchar *allowed_start_groups[] = { "MIME Cache", NULL };

      filename = g_build_filename (dir, "mimeinfo.cache", NULL);

      load_error = NULL;
      entries = 
          egg_desktop_entries_new_from_file (allowed_start_groups,
                                             EGG_DESKTOP_ENTRIES_GENERATE_LOOKUP_MAP |
                                             EGG_DESKTOP_ENTRIES_DISCARD_COMMENTS |
                                             EGG_DESKTOP_ENTRIES_DISCARD_TRANSLATIONS,
                                             filename,
                                             &load_error);
      g_free (filename);

      if (load_error != NULL) {
            g_error_free (load_error);
            return;
      }

      mime_types = egg_desktop_entries_get_keys (entries, "MIME Cache", NULL, &load_error);

      if (load_error != NULL);

      for (i = 0; mime_types[i] != NULL; i++) {
            desktop_file_ids = egg_desktop_entries_get_string_list (entries,
                                                                    "MIME Cache",
                                                                    mime_types[i],
                                                                    NULL,
                                                                    &load_error);
            gnome_vfs_mime_info_cache_merge_mime_associations (cache, mime_types[i], desktop_file_ids);

            g_strfreev (desktop_file_ids);
      }

      g_strfreev (mime_types);
}

static void
gnome_vfs_mime_info_cache_reload (GnomeVFSMimeInfoCache *cache)
{
      char **dirs;
      int i;

      dirs = gnome_vfs_mime_info_cache_get_search_path (cache);

      for (i = 0; dirs[i] != NULL; i++) {
            gnome_vfs_mime_info_cache_reload_dir (cache, dirs[i]);
      }
}


static GnomeVFSMimeInfoCache *
gnome_vfs_mime_info_cache_new ()
{
      GnomeVFSMimeInfoCache *cache;

      cache = g_new0 (GnomeVFSMimeInfoCache, 1);
      cache->mime_types_map = g_hash_table_new (g_str_hash, g_str_equal);

      return cache;
}

static void 
gnome_vfs_mime_info_cache_free (GnomeVFSMimeInfoCache *cache)
{
      if (cache->mime_types_map != NULL) {
            g_free (cache->mime_types_map);
      }
}


GList *
gnome_vfs_mime_get_all_desktop_entries (const char *mime_type)
{
      GList *list, *tmp;
      gchar **desktop_file_ids = NULL;
      int i;

      if (mime_info_cache == NULL) {
            mime_info_cache = gnome_vfs_mime_info_cache_new (mime_info_cache);
            gnome_vfs_mime_info_cache_reload (mime_info_cache);
      }

      list = g_hash_table_lookup (mime_info_cache->mime_types_map, mime_type);

      return list;
}

gchar *
gnome_vfs_mime_get_default_desktop_entry (const char *mime_type)
{
      return NULL;
}
