/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*

   Copyright (C) 2001 Eazel, Inc

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

   Author: Michael Fleming <mfleming@eazel.com>
*/

#include <config.h>
#include "gnome-vfs-app-context.h"

#include "gnome-vfs-backend-private.h"
#include "gnome-vfs-module-api.h"
#include "gnome-vfs-utils.h"

#include <stdio.h>
#include <string.h>

#include <glib/ghash.h>
#include <glib/gstrfuncs.h>
#include <glib/gthread.h>

static void	dispatch_destroy_notify 		(GDestroyNotify notify_func,
			 				 gpointer user_data);

GStaticMutex app_context_mutex = G_STATIC_MUTEX_INIT;

GList *app_context_stack;	/* Primary thread access only */

typedef struct {
	GnomeVFSCallback callback;
	GDestroyNotify	destroy_notify;
	gboolean	dispatch_on_job_thread;
	gpointer user_data;
} CallbackInfo;

struct GnomeVFSAppContext {
	gboolean	pushed;

	GHashTable *	attributes;
			/* keys are strings (attribute names)
			 * values are strings (attribute values)
			 */

	GHashTable *	callbacks;
			/* Keys are strings (callback hook names)
			 * values are CallbackInfo's
			 */

	guint		refcount;

};

/*
 * FIXME
 * In the future, when we allow sync calls from any thread,
 * we should have a separate GnomeVFSAppContext stack per
 * thread.  In this case, the _set_ functions should be callable
 * only from the thread that initially created the app context
 * (or the destroy_notify thread dispatch disipline should be relaxed
 * on app contexts that don't belong to the primary thread)
 * 
 * Currently, we only allow gnome-vfs calls to be initiated
 * from the main thread
 */

GnomeVFSAppContext*
gnome_vfs_app_context_new (void)
{
	GnomeVFSAppContext* ret;

	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	ret = g_new0 (GnomeVFSAppContext, 1);

	ret->attributes = g_hash_table_new (g_str_hash, g_str_equal);
	ret->callbacks = g_hash_table_new (g_str_hash, g_str_equal);

	ret->refcount = 1;

	return ret;
}

static void /* GHFunc */
attributes_copy_conditional (gpointer key, gpointer value, gpointer user_data)
{
	char *value_string;
	GHashTable *destination;

	value_string = value;
	destination = user_data;

	if (NULL == g_hash_table_lookup (destination, key)) {
		g_hash_table_insert (destination, key, g_strdup (value_string));
	}
	
}

static void /* GHFunc */
callbacks_copy_conditional (gpointer key, gpointer value, gpointer user_data)
{
	CallbackInfo *orig_callback_info;
	CallbackInfo *new_callback_info;
	GHashTable *destination;

	orig_callback_info = (CallbackInfo *) value;
	destination = (GHashTable *) user_data;
	
	if (NULL == g_hash_table_lookup (destination, key)) {

		new_callback_info = g_new0 (CallbackInfo, 1);

		*new_callback_info = *orig_callback_info;

		/* destroy_notify is explictly not inherited since,
		 * after all, the parent is guarenteed to still be on
		 * the stack
		 */
		new_callback_info->destroy_notify = NULL;

		g_hash_table_insert (destination, key, new_callback_info);		
	}
}

static void
inherit_from_current (GnomeVFSAppContext* app_context)
{
	const GnomeVFSAppContext *current_app_context;

	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	current_app_context = gnome_vfs_app_context_peek_current ();

	if (current_app_context != NULL) {
		/* Copy all of the attributes that the new context doesn't override */
		g_hash_table_freeze (app_context->attributes);
		g_hash_table_foreach (current_app_context->attributes,
			attributes_copy_conditional, app_context->attributes);
		g_hash_table_thaw (app_context->attributes);

		g_hash_table_freeze (app_context->callbacks);
		g_hash_table_foreach (current_app_context->callbacks,
			callbacks_copy_conditional, app_context->callbacks);
		g_hash_table_thaw (app_context->callbacks);
	}
}

void
gnome_vfs_app_context_push_takesref (GnomeVFSAppContext* app_context)
{
	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	g_return_if_fail (app_context != NULL);

	inherit_from_current (app_context);

	app_context->pushed = TRUE;
	app_context_stack = g_list_prepend (app_context_stack, app_context);
}

void
gnome_vfs_app_context_push_override_takesref (GnomeVFSAppContext* app_context)
{
	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	g_return_if_fail (app_context != NULL);

	app_context->pushed = TRUE;
	app_context_stack = g_list_prepend (app_context_stack, app_context);
}

void
gnome_vfs_app_context_pop (void)
{
	GnomeVFSAppContext *prev_context;
	GList *link;
	
	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	g_return_if_fail (app_context_stack != NULL);

	link = app_context_stack;
	app_context_stack = g_list_remove_link (app_context_stack, link);
	prev_context = (GnomeVFSAppContext *)link->data;
	gnome_vfs_app_context_unref (prev_context);
	g_list_free (link);
}

void
gnome_vfs_app_context_set_attribute (GnomeVFSAppContext* app_context,
                                     const char *attribute_name,
				     const char *value)
{
	char *inserted_key, *inserted_value;
	
	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	g_return_if_fail (app_context != NULL);
	g_return_if_fail (attribute_name != NULL);
	g_return_if_fail (app_context->pushed != TRUE);

	if (g_hash_table_lookup_extended (app_context->attributes, attribute_name, (gpointer *)&inserted_key, (gpointer *)&inserted_value)) {
		g_hash_table_remove (app_context->attributes, attribute_name);

		g_free (inserted_key);
		inserted_key = NULL;
		g_free (inserted_value);
		inserted_value = NULL;		
	}

	if (value != NULL) {
		g_hash_table_insert (app_context->attributes, g_strdup (attribute_name), g_strdup (value));
	}
}

void
gnome_vfs_app_context_set_callback (GnomeVFSAppContext* app_context,
				    const char *hook_name,
				    GnomeVFSCallback callback,
				    gpointer user_data)
{
	gnome_vfs_app_context_set_callback_full (app_context, hook_name, callback, user_data, FALSE, NULL);
}

/* A "TRUE" on dispatch_on_job_thread means that the callback
 * will arrive on a thread for async calls.
 * 
 * Note that currently these callbacks may arrive after a gnome_vfs_async_cancel
 * has been called on the job
 */
void
gnome_vfs_app_context_set_callback_full (GnomeVFSAppContext* app_context,
				         const char *hook_name,
				         GnomeVFSCallback callback,
				         gpointer user_data,
					 gboolean dispatch_on_job_thread,
				         GDestroyNotify notify)
{
	char *inserted_key;
	CallbackInfo *inserted_value;
	CallbackInfo *to_insert;
	
	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	g_return_if_fail (app_context != NULL);
	g_return_if_fail (hook_name != NULL);
	g_return_if_fail (app_context->pushed != TRUE);

	if (g_hash_table_lookup_extended (app_context->callbacks, hook_name, 
	    (gpointer *) &inserted_key, (gpointer *) &inserted_value)) {
		g_hash_table_remove (app_context->callbacks, hook_name);

		/* It would seem wierd to me if it didn't work this way
		 * but I can't imagine anyone relying on the feature
		 * of getting a destroy_notify callback when they over-write
		 * a callback hook on a context they haven't pushed yet
		 */
		if (inserted_value->destroy_notify != NULL) {
			inserted_value->destroy_notify (inserted_value->user_data);
		}
		
		g_free (inserted_key);
		inserted_key = NULL;
		g_free (inserted_value);
		inserted_value = NULL;
	}

	if (callback != NULL) {	
		to_insert = g_new0 (CallbackInfo, 1);

		to_insert->callback = callback;
		to_insert->destroy_notify = notify;
		to_insert->user_data = user_data;
		to_insert->dispatch_on_job_thread = dispatch_on_job_thread;

		g_hash_table_insert (app_context->callbacks, g_strdup (hook_name), to_insert);
	}
}

static GnomeVFSAppContext *
gnome_vfs_app_context_peek_current_internal (void)
{
	GnomeVFSAppContext* ret;

	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	if (app_context_stack == NULL) {
		/* There's always an app context, even if its empty */
		ret = gnome_vfs_app_context_new();
		gnome_vfs_app_context_push_override_takesref (ret);
	} else {
		ret = (GnomeVFSAppContext *)app_context_stack->data;
	}

	return ret;
}

const GnomeVFSAppContext*
gnome_vfs_app_context_peek_current (void)
{
	return gnome_vfs_app_context_peek_current_internal ();
}

GnomeVFSAppContext *
gnome_vfs_app_context_get_current (void)
{
	GnomeVFSAppContext* ret;

	GNOME_VFS_ASSERT_PRIMARY_THREAD;

	ret = gnome_vfs_app_context_peek_current_internal ();
	gnome_vfs_app_context_ref (ret);

	return ret;
}

/*********************************************************************/
/*********************************************************************/

void
gnome_vfs_app_context_ref (GnomeVFSAppContext* app_context)
{
	g_return_if_fail (app_context != NULL);

	g_static_mutex_lock (&app_context_mutex);
	app_context->refcount++;

	/* printf ("app_context_ref: %0x08x %d\n",(unsigned)app_context, app_context->refcount); */
	
	g_static_mutex_unlock (&app_context_mutex);
}

static void /* GHFunc */
hash_free_keys_values (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	g_free (value);
}

static void /* GHFunc */
hash_dispatch_destroy_notifies (gpointer key, gpointer value, gpointer user_data)
{
	CallbackInfo *callback_info;

	callback_info = (CallbackInfo *) value;

	if (callback_info->destroy_notify != NULL) {
		dispatch_destroy_notify (callback_info->destroy_notify, callback_info->user_data);
	}
}

void
gnome_vfs_app_context_unref (GnomeVFSAppContext* app_context)
{
	gboolean should_free;

	g_static_mutex_lock (&app_context_mutex);
	g_assert (app_context->refcount > 0);
	app_context->refcount--;

	/* printf ("app_context_unref: %0x08x %d\n",(unsigned)app_context, app_context->refcount); */

	should_free = (app_context->refcount == 0);
	g_static_mutex_unlock (&app_context_mutex);

	if (should_free) {
		g_hash_table_foreach (app_context->attributes, hash_free_keys_values, NULL);
		g_hash_table_foreach (app_context->callbacks, hash_dispatch_destroy_notifies, app_context);

		g_free (app_context);
	}
}

char *
gnome_vfs_app_context_get_attribute (const GnomeVFSAppContext* app_context,
				     const char *attribute_name)
{
	char *ret;
	
	g_return_val_if_fail (app_context != NULL, NULL);
	/* AppContext's cannot be shared between threads until they've been pushed
	 * on the stack
	 */
	g_assert (app_context->pushed || gnome_vfs_is_primary_thread());

	ret = g_hash_table_lookup (app_context->attributes, attribute_name);

	if (ret != NULL) {
		ret = g_strdup (ret);
	}

	return ret;
}

GnomeVFSCallback
gnome_vfs_app_context_get_callback (const GnomeVFSAppContext* app_context,
				    const char *hook_name,
				    gpointer *user_data,
				    gboolean *p_dispatch_on_job_thread)
{
	CallbackInfo *callback_info;

	GnomeVFSCallback ret;
	
	g_return_val_if_fail (app_context != NULL, NULL);
	/* AppContext's cannot be shared between threads until they've been pushed
	 * on the stack
	 */
	g_assert (app_context->pushed || gnome_vfs_is_primary_thread());

	callback_info = g_hash_table_lookup (app_context->callbacks, hook_name);

	if (callback_info == NULL) {
		ret = NULL;
		goto error;
	}
	
	ret = callback_info->callback;
		
	if (user_data != NULL) {
		*user_data = callback_info->user_data;
	}

	if (p_dispatch_on_job_thread != NULL) {
		*p_dispatch_on_job_thread = callback_info->dispatch_on_job_thread;
	}

error:	
	return ret;
}

/*********************************************************************/
/*********************************************************************/

/* Returns TRUE if there actually was a hook set for this hookname */
gboolean
gnome_vfs_callback_call_hook  (const char *hookname,
			       gconstpointer in, gsize in_size,
			       gpointer out, gsize out_size)
{
	GnomeVFSCallback callback;
	gpointer user_data;
	gboolean ret;
	gboolean dispatch_on_job_thread;
	
	callback = gnome_vfs_app_context_get_callback (
			gnome_vfs_context_peek_app_context (
				gnome_vfs_context_peek_current()),
			hookname,
			&user_data,
			&dispatch_on_job_thread);

	if (callback == NULL) {
		ret = FALSE;
	} else {
		ret = TRUE;

		if (gnome_vfs_is_primary_thread () || dispatch_on_job_thread) {
			callback (user_data, in, in_size, out, out_size);
		} else {
			gnome_vfs_backend_dispatch_callback (callback, user_data, 
							     in, in_size,
							     out, out_size);
		}
	}

	return ret;
}

typedef struct  {
	GDestroyNotify	func;
	gpointer	user_data;
} DestroyNotifyIn;

static void /* GnomeVFSCallback */
destroy_notify_callback (gpointer user_data, gconstpointer in, gsize in_size,
		         gpointer out, gsize out_size)
{
	DestroyNotifyIn *real_in;

	real_in = (DestroyNotifyIn *) in;
	g_return_if_fail (sizeof (DestroyNotifyIn) == in_size);
	
	real_in->func (real_in->user_data);
}

static void
dispatch_destroy_notify (GDestroyNotify notify_func,
			 gpointer user_data)
{
	DestroyNotifyIn in_arg;

	memset (&in_arg, 0, sizeof (DestroyNotifyIn));

	if (gnome_vfs_is_primary_thread ()) {
		notify_func (user_data);
	} else {
		in_arg.func = notify_func;
		in_arg.user_data = user_data;

		if (gnome_vfs_is_primary_thread ()) {
			destroy_notify_callback (user_data, &in_arg, sizeof (in_arg), NULL, 0);
		} else {
			gnome_vfs_backend_dispatch_callback (destroy_notify_callback, user_data, 
							     &in_arg, sizeof (in_arg),
							     NULL, 0);
		}
	}
}
