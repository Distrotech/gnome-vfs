/* Some helper functions that gdesktopentries needs
 * Copyright (C) 2004 Ray Strode
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "eggdirfuncs.h"
#include "glib.h"

/**
 * egg_get_user_data_dir:
 * 
 * Returns the data directory of the user.
 * 
 * Return value: the value of the data directory of the user as 
 *               specified by the XDG Base Directory specification. 
 **/
gchar*
egg_get_user_data_dir (void)
{
  gchar *data_dir;  

  data_dir = (gchar *) g_getenv ("XDG_DATA_HOME");

  if (data_dir && data_dir[0])
    data_dir = g_strdup (data_dir);
  else
    data_dir = g_build_filename (g_get_home_dir (), ".local", "share", NULL);

  return data_dir;
}

/**
 * egg_get_user_configuration_dir:
 * 
 * Returns the configuration directory of the user.
 * 
 * Return value: the value of the configuration directory of the user
 *               as specified by the XDG Base Directory specification. 
 **/
gchar*
egg_get_user_configuration_dir (void)
{
  gchar *conf_dir;  

  conf_dir = (gchar *) g_getenv ("XDG_CONFIG_HOME");

  if (conf_dir && conf_dir[0])
    conf_dir = g_strdup (conf_dir);
  else
    conf_dir = g_build_filename (g_get_home_dir (), ".config", NULL);

  return conf_dir;
}

/**
 * egg_get_user_cache_dir:
 * 
 * Returns the cache directory of the user.
 * 
 * Return value: the value of the cache directory of the user
 *               as specified by the XDG Base Directory specification. 
 **/
gchar*
egg_get_user_cache_dir (void)
{
  gchar *cache_dir;  

  cache_dir = (gchar *) g_getenv ("XDG_CACHE_HOME");

  if (cache_dir && cache_dir[0])
    cache_dir = g_strdup (cache_dir);
  else
    cache_dir = g_build_filename (g_get_home_dir (), ".cache", NULL);

  return cache_dir;
}

/**
 * egg_get_secondary_data_dirs:
 * 
 * Returns a %NULL-terminated array of preference ordered data directories.
 * 
 * Return value: a %NULL-terminated array of preference ordered data 
 *               directories as specified by the XDG Base Directory 
 *               specification. Use g_strfreev to free it.
 **/
gchar**
egg_get_secondary_data_dirs (void)
{
  gchar *data_dirs, **data_dir_vector;

  data_dirs = (gchar *) g_getenv ("XDG_DATA_DIRS");

  if (!data_dirs || !data_dirs[0])
    data_dirs = "/usr/local/share/:/usr/share/";

  data_dir_vector = g_strsplit (data_dirs, ":", 0);

  return data_dir_vector;
}

/**
 * egg_get_secondary_configuration_dirs:
 * 
 * Returns a %NULL-terminated array of preference ordered configuration
 * directories.
 * 
 * Return value: a %NULL-terminated array of preference ordered 
 *               configuration directories as specified by the XDG 
 *               Base Directory specification. Use g_strfreev to free 
 *               it.
 **/
gchar**
egg_get_secondary_configuration_dirs (void)
{
  gchar *conf_dirs, **conf_dir_vector;

  conf_dirs = (gchar *) g_getenv ("XDG_CONFIG_DIRS");

  if (!conf_dirs || !conf_dirs[0])
    conf_dirs = "/etc/xdg";

  conf_dir_vector = g_strsplit (conf_dirs, ":", 0);

  return conf_dir_vector;
}

/**
 * g_find_file_in_configuration_dir:
 * @file: the relative filename to look for
 * @tests: bitfield of #GFileTest flags
 * 
 * Looks for a file named @file that passes all @tests.  
 * g_find_file_in_data_dir first checks in the path returned by 
 * egg_get_user_configuration_dir and then checks the paths returned by
 * egg_get_secondary_configuration_dirs.
 * 
 * Return value: the full path of file named @file that passes all @tests or
 *               %NULL if no file could be found.
 **/
gchar*
egg_find_file_in_configuration_dir (const gchar *file, 
				    GFileTest    tests)
{
  gchar *conf_dir, *path, **secondary_conf_dirs;
  int i;

  path = NULL;

  conf_dir = egg_get_user_configuration_dir ();

  if (conf_dir) 
    {
      path = g_build_filename (conf_dir, file, NULL);
      g_free (conf_dir);
      conf_dir = NULL;
    }

  if (path && egg_file_full_test (path, tests))
    return path;

  secondary_conf_dirs = egg_get_secondary_configuration_dirs ();

  i = 0;
  while (secondary_conf_dirs && (conf_dir = secondary_conf_dirs[i]))
      {
        path = g_build_filename (conf_dir, file, NULL);

        if (egg_file_full_test (path, tests))
          break;

        g_free (path);
        path = NULL;
        i++;
      }

  g_strfreev (secondary_conf_dirs);

  return path;
}

gboolean
egg_file_full_test (const gchar *filename,
                  GFileTest tests)
{
  /* Note this array needs to be kept in sync with the enumeration 
   * defined in the gfileutils.h
   */
  static const GFileTest all_tests[] = { G_FILE_TEST_IS_REGULAR, 
                                         G_FILE_TEST_IS_SYMLINK,  
                                         G_FILE_TEST_IS_DIR,
                                         G_FILE_TEST_IS_EXECUTABLE,
                                         G_FILE_TEST_EXISTS,
					 /*
                                         G_FILE_TEST_IS_READABLE,
                                         G_FILE_TEST_IS_WRITABLE,
					 */
                                         (GFileTest) 0 };
  int i;

  for (i = 0; tests && all_tests[i]; i++)
    {
      if (tests & all_tests[i])
        {
          if (!g_file_test (filename, all_tests[i]))
            return FALSE;

          tests &= ~all_tests[i];
        }
    }

  /* Either the user sent an invalid bitmask or the tests array has 
   * gotten out of sync with the GFileTest enumeration
   */
  g_return_val_if_fail (tests == (GFileTest) 0, FALSE);

  return TRUE;
}
