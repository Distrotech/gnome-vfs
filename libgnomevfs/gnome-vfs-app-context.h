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

/*
 * A GnomeVFSAppContext allows the application to set certain attributes and register
 * for certain callbacks.
 * 
 * A GnomeVFSAppContext is inherited by all async jobs that are initiated while
 * the context is active.  (Thus, popping the context does not necessarally destroy it)
 * 
 * Note that once a GnomeVFSAppContext has been pushed onto the stack, it may not 
 * be futher modified.
 */

/* These interfaces are not part of the GNOME 1.4 platform.  They will be stable in GNOME 2.0 */

#ifndef GNOME_VFS_APP_CONTEXT_H
#define GNOME_VFS_APP_CONTEXT_H

#include <glib.h>

#include "gnome-vfs.h"
#include "gnome-vfs-types.h"

typedef struct GnomeVFSAppContext GnomeVFSAppContext;

/*
 * These calls are not thread-safe and may not be called by
 * modules under any circumstance; they are for applications only
 */
 
GnomeVFSAppContext* 	gnome_vfs_app_context_new		(void);

/* gnome_vfs_app_context_push_takesref and 
 * gnome_vfs_app_context_push_override_takesref steal the reference from the caller,
 * so you don't have to unref 
 */
  
/* Note: you cannot use any of the _set_ functions once an AppContext has been pushed */
void			gnome_vfs_app_context_push_takesref	(GnomeVFSAppContext* app_context);

/* By default, an app context inherits otherwise unset values from its parent on the stack 
 * An app_context that has been placed on the stack via push_override does not inherit any values
 * from the parent
 */ 
void			gnome_vfs_app_context_push_override_takesref (GnomeVFSAppContext* app_context);

void			gnome_vfs_app_context_pop		(void);

void			gnome_vfs_app_context_set_attribute	(GnomeVFSAppContext* app_context, const char *attribute_name,
								 const char *value);

void			gnome_vfs_app_context_set_callback	(GnomeVFSAppContext* app_context,
								 const char *hook_name,
								 GnomeVFSCallback callback,
								 gpointer user_data);

/* destroy notify's are guarenteed to be dispatched on the primary thread */
void			gnome_vfs_app_context_set_callback_full	(GnomeVFSAppContext* app_context,
								 const char *hook_name,
								 GnomeVFSCallback callback,
								 gpointer user_data,
								 gboolean dispatch_on_job_thread,
								 GDestroyNotify notify);

/* Note that you cannot modify an AppContext that's on the stack */
/* Note that this returns a borrowed ref -- you don't unref it.  It's guarenteed to be valid to the next pop */
const GnomeVFSAppContext* gnome_vfs_app_context_peek_current	(void);

/* This function does add a ref before returning */
GnomeVFSAppContext * 	gnome_vfs_app_context_get_current	(void);

/*
 * These functions are thread-safe and can be called
 * by either modules or applications
 */

void			gnome_vfs_app_context_ref		(GnomeVFSAppContext* app_context);

void			gnome_vfs_app_context_unref		(GnomeVFSAppContext* app_context);


char *			gnome_vfs_app_context_get_attribute	(const GnomeVFSAppContext* app_context,
								 const char *attribute_name);

GnomeVFSCallback	gnome_vfs_app_context_get_callback	(const GnomeVFSAppContext* app_context,
								 const char *hook_name,
								 /* OUT */ gpointer *user_data,
								 /* OUT */ gboolean *p_dispatch_on_job_thread);


#endif /* GNOME_VFS_APPLICATION_CONTEXT_H */
