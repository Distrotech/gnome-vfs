/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* stolen-glib-extensions.h - interface for new functions that conceptually
                                belong in glib. Perhaps some of these will be
                                actually rolled into glib someday.

   Copyright (C) 2000 Eazel, Inc.

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

   Authors: John Sullivan <sullivan@eazel.com>
*/

#ifndef STOLEN_GLIB_EXTENSIONS_H
#define STOLEN_GLIB_EXTENSIONS_H

#include <glib/ghash.h>
#include <glib/glist.h>
#include <glib/gstring.h>
#include <time.h>

/* A gboolean variant for bit fields. */
typedef guint stolen_boolean_bit;

/* Use this until we can switch to G_N_ELEMENTS. */
#define STOLEN_N_ELEMENTS(array) (sizeof (array) / sizeof ((array)[0]))

/* Callback functions that have user data. */
typedef int      (* StolenCompareFunction)   (gconstpointer a,
					   gconstpointer b,
					   gpointer callback_data);
typedef int      (* StolenSearchFunction)    (gconstpointer item,
					   gpointer callback_data);

/* Predicate. */
typedef gboolean (* StolenPredicateFunction) (gpointer data,
					   gpointer callback_data);

/* GList functions. */
gboolean    stolen_g_list_exactly_one_item                 (GList                 *list);
gboolean    stolen_g_list_more_than_one_item               (GList                 *list);
gboolean    stolen_g_list_equal                            (GList                 *list_a,
							 GList                 *list_b);
GList *     stolen_g_list_copy                             (GList                 *list);
void        stolen_g_list_safe_for_each                    (GList                 *list,
							 GFunc                  function,
							 gpointer               user_data);
GList *     stolen_g_list_sort_custom                      (GList                 *list,
							 StolenCompareFunction     compare,
							 gpointer               user_data);
gboolean    stolen_g_lists_sort_and_check_for_intersection (GList                **list_a,
							 GList                **list_b);
GList *     stolen_g_list_partition                        (GList                 *list,
							 StolenPredicateFunction   predicate,
							 gpointer               user_data,
							 GList                **removed);

/* List functions for lists of g_free'able objects. */
void        stolen_g_list_free_deep                        (GList                 *list);
void        stolen_g_list_free_deep_custom                 (GList                 *list,
							 GFunc                  element_free_func,
							 gpointer               user_data);

/* List functions for lists of C strings. */
gboolean    stolen_g_str_list_equal                        (GList                 *str_list_a,
							 GList                 *str_list_b);
GList *     stolen_g_str_list_copy                         (GList                 *str_list);
GList *     stolen_g_str_list_alphabetize                  (GList                 *str_list);

/* GString functions */
void        stolen_g_string_append_len                     (GString               *string,
							 const char            *characters,
							 int                    length);

/* GHashTable functions */
GHashTable *stolen_g_hash_table_new_free_at_exit           (GHashFunc              hash_function,
							 GCompareFunc           key_compare_function,
							 const char            *display_name);
void        stolen_g_hash_table_safe_for_each              (GHashTable            *hash_table,
							 GHFunc                 callback,
							 gpointer               callback_data);
gboolean    stolen_g_hash_table_remove_deep_custom         (GHashTable            *hash_table,
							 gconstpointer          key,
							 GFunc                  key_free_func,
							 gpointer               key_free_data,
							 GFunc                  value_free_func,
							 gpointer               value_free_data);
gboolean    stolen_g_hash_table_remove_deep                (GHashTable            *hash_table,
							 gconstpointer          key);
void        stolen_g_hash_table_destroy_deep_custom        (GHashTable            *hash_table,
							 GFunc                  key_free_func,
							 gpointer               key_free_data,
							 GFunc                  value_free_func,
							 gpointer               value_free_data);
void        stolen_g_hash_table_destroy_deep               (GHashTable            *hash_table);

#endif /* STOLEN_GLIB_EXTENSIONS_H */
