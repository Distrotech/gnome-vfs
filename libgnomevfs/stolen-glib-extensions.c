/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-glib-extensions.c - implementation of new functions that conceptually
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

#include <config.h>
#include "stolen-glib-extensions.h"
#include "eel-cut-n-paste.h"
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <glib.h>

typedef struct {
	GHashTable *hash_table;
	char *display_name;
	gboolean keys_known_to_be_strings;
} HashTableToFree;

static GList *hash_tables_to_free_at_exit;

/**
 * stolen_g_list_exactly_one_item
 *
 * Like g_list_length (list) == 1, only O(1) instead of O(n).
 * @list: List.
 *
 * Return value: TRUE if the list has exactly one item.
 **/
gboolean
stolen_g_list_exactly_one_item (GList *list)
{
	return list != NULL && list->next == NULL;
}

/**
 * stolen_g_list_more_than_one_item
 *
 * Like g_list_length (list) > 1, only O(1) instead of O(n).
 * @list: List.
 *
 * Return value: TRUE if the list has more than one item.
 **/
gboolean
stolen_g_list_more_than_one_item (GList *list)
{
	return list != NULL && list->next != NULL;
}

/**
 * stolen_g_list_equal
 *
 * Compares two lists to see if they are equal.
 * @list_a: First list.
 * @list_b: Second list.
 *
 * Return value: TRUE if the lists are the same length with the same elements.
 **/
gboolean
stolen_g_list_equal (GList *list_a, GList *list_b)
{
	GList *p, *q;

	for (p = list_a, q = list_b; p != NULL && q != NULL; p = p->next, q = q->next) {
		if (p->data != q->data) {
			return FALSE;
		}
	}
	return p == NULL && q == NULL;
}

/**
 * stolen_g_list_copy
 *
 * @list: List to copy.
 * Return value: Shallow copy of @list.
 **/
GList *
stolen_g_list_copy (GList *list)
{
	GList *p, *result;

	result = NULL;
	
	if (list == NULL) {
		return NULL;
	}

	for (p = g_list_last (list); p != NULL; p = p->prev) {
		result = g_list_prepend (result, p->data);
	}
	return result;
}

/**
 * stolen_g_str_list_equal
 *
 * Compares two lists of C strings to see if they are equal.
 * @list_a: First list.
 * @list_b: Second list.
 *
 * Return value: TRUE if the lists contain the same strings.
 **/
gboolean
stolen_g_str_list_equal (GList *list_a, GList *list_b)
{
	GList *p, *q;

	for (p = list_a, q = list_b; p != NULL && q != NULL; p = p->next, q = q->next) {
		if (stolen_strcmp (p->data, q->data) != 0) {
			return FALSE;
		}
	}
	return p == NULL && q == NULL;
}

/**
 * stolen_g_str_list_copy
 *
 * @list: List of strings and/or NULLs to copy.
 * Return value: Deep copy of @list.
 **/
GList *
stolen_g_str_list_copy (GList *list)
{
	GList *node, *result;

	result = NULL;
	
	for (node = g_list_last (list); node != NULL; node = node->prev) {
		result = g_list_prepend (result, g_strdup (node->data));
	}
	return result;
}

/**
 * stolen_g_str_list_alphabetize
 *
 * Sort a list of strings using locale-sensitive rules.
 *
 * @list: List of strings and/or NULLs.
 * 
 * Return value: @list, sorted.
 **/
GList *
stolen_g_str_list_alphabetize (GList *list)
{
	return g_list_sort (list, (GCompareFunc) stolen_strcoll);
}

/**
 * stolen_g_list_free_deep_custom
 *
 * Frees the elements of a list and then the list, using a custom free function.
 *
 * @list: List of elements that can be freed with the provided free function.
 * @element_free_func: function to call with the data pointer and user_data to free it.
 * @user_data: User data to pass to element_free_func
 **/
void
stolen_g_list_free_deep_custom (GList *list, GFunc element_free_func, gpointer user_data)
{
	g_list_foreach (list, element_free_func, user_data);
	g_list_free (list);
}

/**
 * stolen_g_list_free_deep
 *
 * Frees the elements of a list and then the list.
 * @list: List of elements that can be freed with g_free.
 **/
void
stolen_g_list_free_deep (GList *list)
{
	stolen_g_list_free_deep_custom (list, (GFunc) g_free, NULL);
}

/**
 * stolen_g_list_safe_for_each
 * 
 * A version of g_list_foreach that works if the passed function
 * deletes the current element.
 * 
 * @list: List to iterate.
 * @function: Function to call on each element.
 * @user_data: Data to pass to function.
 */
void
stolen_g_list_safe_for_each (GList *list, GFunc function, gpointer user_data)
{
	GList *p, *next;

	for (p = list; p != NULL; p = next) {
		next = p->next;
		(* function) (p->data, user_data);
	}
}

static GList *
stolen_g_list_sort_merge (GList       *list_1, 
			    GList       *list_2,
			    StolenCompareFunction compare_func,
			    gpointer user_data)
{
  GList list_buffer, *list, *previous_node;

  list = &list_buffer; 
  previous_node = NULL;

  while (list_1 != NULL && 
	 list_2 != NULL) {
	  if (compare_func (list_1->data, list_2->data, user_data) < 0) {
		  list->next = list_1;
		  list = list->next;
		  list->prev = previous_node; 
		  previous_node = list;
		  list_1 = list_1->next;
	  } 
	  else {
		  list->next = list_2;
		  list = list->next;
		  list->prev = previous_node; 
		  previous_node = list;
		  list_2 = list_2->next;
	  }
  }

  list->next = list_1 ? list_1 : list_2;
  list->next->prev = list;

  return list_buffer.next;
}


static gboolean
stolen_g_list_is_already_sorted (GList *list,
				   StolenCompareFunction compare_func,
				   gpointer user_data)
{
	if (list == NULL) {
		return TRUE;
	}

	while (list->next != NULL) {
		if (compare_func (list->data, list->next->data, user_data) > 0) {
			return FALSE;
		}
		list = list->next;
		
	}

	return TRUE;
}

GList *
stolen_g_list_sort_custom (GList *list,
			     StolenCompareFunction compare_func,
			     gpointer user_data)
{
	GList *list_1, *list_2;
  
	if (stolen_g_list_is_already_sorted (list, compare_func, user_data)) {
		return list;
	}

	list_1 = list; 
	list_2 = list->next;

	/* Split the two lists half way down the middle */
	while (TRUE) {
		list_2 = list_2->next;
		if (list_2 == NULL) {
			break;
		}
		list_2 = list_2->next;
		if (list_2 == NULL) {
			break;
		}
		list_1 = list_1->next;
	}
	
	list_2 = list_1->next; 
	list_1->next = NULL; 

	return stolen_g_list_sort_merge (stolen_g_list_sort_custom (list, compare_func, user_data),
					   stolen_g_list_sort_custom (list_2, compare_func, user_data),
					   compare_func,
					   user_data);

}

static int
compare_pointers (gconstpointer pointer_1, gconstpointer pointer_2)
{
	if ((const char *) pointer_1 < (const char *) pointer_2) {
		return -1;
	}
	if ((const char *) pointer_1 > (const char *) pointer_2) {
		return +1;
	}
	return 0;
}



gboolean
stolen_g_lists_sort_and_check_for_intersection (GList **list_1,
						  GList **list_2) 

{
	GList *node_1, *node_2;
	int compare_result;
	
	*list_1 = g_list_sort (*list_1, compare_pointers);
	*list_2 = g_list_sort (*list_2, compare_pointers);

	node_1 = *list_1;
	node_2 = *list_2;

	while (node_1 != NULL && node_2 != NULL) {
		compare_result = compare_pointers (node_1->data, node_2->data);
		if (compare_result == 0) {
			return TRUE;
		}
		if (compare_result <= 0) {
			node_1 = node_1->next;
		}
		if (compare_result >= 0) {
			node_2 = node_2->next;
		}
	}

	return FALSE;
}


/**
 * stolen_g_list_partition
 * 
 * Parition a list into two parts depending on whether the data
 * elements satisfy a provided predicate. Order is preserved in both
 * of the resulting lists, and the original list is consumed. A list
 * of the items that satisfy the predicate is returned, and the list
 * of items not satisfying the predicate is returned via the failed
 * out argument.
 * 
 * @list: List to partition.
 * @predicate: Function to call on each element.
 * @user_data: Data to pass to function.  
 * @failed: The GList * variable pointed to by this argument will be
 * set to the list of elements for which the predicate returned
 * false. */

GList *
stolen_g_list_partition (GList *list,
			   StolenPredicateFunction  predicate,
			   gpointer user_data,
			   GList **failed)
{
	GList *predicate_true;
	GList *predicate_false;
	GList *reverse;
	GList *p;
	GList *next;

	predicate_true = NULL;
	predicate_false = NULL;

	reverse = g_list_reverse (list);

	for (p = reverse; p != NULL; p = next) {
		next = p->next;
		
		if (next != NULL) {
			next->prev = NULL;
		}

		if (predicate (p->data, user_data)) {
			p->next = predicate_true;
 			if (predicate_true != NULL) {
				predicate_true->prev = p;
			}
			predicate_true = p;
		} else {
			p->next = predicate_false;
 			if (predicate_false != NULL) {
				predicate_false->prev = p;
			}
			predicate_false = p;
		}
	}

	*failed = predicate_false;
	return predicate_true;
}



static void
print_key_string (gpointer key, gpointer value, gpointer callback_data)
{
	g_assert (callback_data == NULL);

	g_print ("--> %s\n", (char *) key);
}

static void
free_hash_tables_at_exit (void)
{
	GList *p;
	HashTableToFree *hash_table_to_free;
	guint size;

	for (p = hash_tables_to_free_at_exit; p != NULL; p = p->next) {
		hash_table_to_free = p->data;

		size = g_hash_table_size (hash_table_to_free->hash_table);
		if (size != 0) {
			if (hash_table_to_free->keys_known_to_be_strings) {
				g_print ("\n--- Hash table keys for warning below:\n");
				g_hash_table_foreach (hash_table_to_free->hash_table,
						      print_key_string,
						      NULL);
			}
			g_warning ("\"%s\" hash table still has %u element%s at quit time%s",
				   hash_table_to_free->display_name, size,
				   size == 1 ? "" : "s",
				   hash_table_to_free->keys_known_to_be_strings
				   ? " (keys above)" : "");
		}

		g_hash_table_destroy (hash_table_to_free->hash_table);
		g_free (hash_table_to_free->display_name);
		g_free (hash_table_to_free);
	}
	g_list_free (hash_tables_to_free_at_exit);
	hash_tables_to_free_at_exit = NULL;
}

GHashTable *
stolen_g_hash_table_new_free_at_exit (GHashFunc hash_func,
					GCompareFunc key_compare_func,
					const char *display_name)
{
	GHashTable *hash_table;
	HashTableToFree *hash_table_to_free;

	/* FIXME: We can take out the NAUTILUS_DEBUG check once we
	 * have fixed more of the leaks. For now, it's a bit too noisy
	 * for the general public.
	 */
	if (hash_tables_to_free_at_exit == NULL
	    && g_getenv ("NAUTILUS_DEBUG") != NULL) {
		g_atexit (free_hash_tables_at_exit);
	}

	hash_table = g_hash_table_new (hash_func, key_compare_func);

	hash_table_to_free = g_new (HashTableToFree, 1);
	hash_table_to_free->hash_table = hash_table;
	hash_table_to_free->display_name = g_strdup (display_name);
	hash_table_to_free->keys_known_to_be_strings =
		hash_func == g_str_hash;

	hash_tables_to_free_at_exit = g_list_prepend
		(hash_tables_to_free_at_exit, hash_table_to_free);

	return hash_table;
}

typedef struct {
	GList *keys;
	GList *values;
} FlattenedHashTable;

static void
flatten_hash_table_element (gpointer key, gpointer value, gpointer callback_data)
{
	FlattenedHashTable *flattened_table;

	flattened_table = callback_data;
	flattened_table->keys = g_list_prepend
		(flattened_table->keys, key);
	flattened_table->values = g_list_prepend
		(flattened_table->values, value);
}

void
stolen_g_hash_table_safe_for_each (GHashTable *hash_table,
				     GHFunc callback,
				     gpointer callback_data)
{
	FlattenedHashTable flattened;
	GList *p, *q;

	flattened.keys = NULL;
	flattened.values = NULL;

	g_hash_table_foreach (hash_table,
			      flatten_hash_table_element,
			      &flattened);

	for (p = flattened.keys, q = flattened.values;
	     p != NULL;
	     p = p->next, q = q->next) {
		(* callback) (p->data, q->data, callback_data);
	}

	g_list_free (flattened.keys);
	g_list_free (flattened.values);
}

gboolean
stolen_g_hash_table_remove_deep_custom (GHashTable *hash_table, gconstpointer key,
					  GFunc key_free_func, gpointer key_free_data,
					  GFunc value_free_func, gpointer value_free_data)
{
	gpointer key_in_table;
	gpointer value;

	/* It would sure be nice if we could do this with a single lookup.
	 */
	if (g_hash_table_lookup_extended (hash_table, key,
					  &key_in_table, &value)) {
		g_hash_table_remove (hash_table, key);
		if (key_free_func != NULL) {
			(* key_free_func) (key_in_table, key_free_data);
		}
		/* handle key == value, don't double free */
		if (value_free_func != NULL && value != key_in_table) {
			(* value_free_func) (value, value_free_data);
		}
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
stolen_g_hash_table_remove_deep (GHashTable *hash_table, gconstpointer key)
{
	return stolen_g_hash_table_remove_deep_custom
		(hash_table, key, (GFunc) g_free, NULL, (GFunc) g_free, NULL);
}

typedef struct {
	GFunc	  key_free_func;
	gpointer  key_free_data;
	GFunc	  value_free_func;
	gpointer  value_free_data;
} HashTableFreeFuncs;

static gboolean
destroy_deep_helper (gpointer key, gpointer value, gpointer data)
{
	HashTableFreeFuncs *free_funcs;

	free_funcs = (HashTableFreeFuncs *) data;
	
	if (free_funcs->key_free_func != NULL) {
		(* free_funcs->key_free_func) (key, free_funcs->key_free_data);
	}
	/* handle key == value, don't double free */
	if (free_funcs->value_free_func != NULL && value != key) {
		(* free_funcs->value_free_func) (value, free_funcs->value_free_data);
	}
	return TRUE;
}

void
stolen_g_hash_table_destroy_deep_custom (GHashTable *hash_table,
					   GFunc key_free_func, gpointer key_free_data,
					   GFunc value_free_func, gpointer value_free_data)
{
	HashTableFreeFuncs free_funcs;
	
	g_return_if_fail (hash_table != NULL);

	free_funcs.key_free_func = key_free_func;
	free_funcs.key_free_data = key_free_data;
	free_funcs.value_free_func = value_free_func;
	free_funcs.value_free_data = value_free_data;
	
	g_hash_table_foreach_remove (hash_table, destroy_deep_helper, &free_funcs);

	g_hash_table_destroy (hash_table);
}

void
stolen_g_hash_table_destroy_deep (GHashTable *hash_table)
{
	stolen_g_hash_table_destroy_deep_custom (hash_table, (GFunc) g_free, NULL, (GFunc) g_free, NULL);
}
