/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* inotify-helper.c - Gnome VFS Monitor based on inotify.

   Copyright (C) 2005 John McCutchan

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

   Authors: 
		 John McCutchan <ttb@tentacle.dhs.org>
*/

#include "config.h"
#include <glib.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "inotify-missing.h"
#include "inotify-path.h"

#define SCAN_MISSING_TIME 500 /* 2 Hz */

/* We put ih_sub_t's that are missing on this list */
static GList *missing_sub_list = NULL;
static gboolean im_scan_missing (gpointer user_data);
static void (*missing_cb)(ih_sub_t *sub) = NULL;

G_LOCK_EXTERN (inotify_lock);

/* inotify_lock must be held before calling */
void im_startup (void (*callback)(ih_sub_t *sub))
{
	gboolean initialized = FALSE;

	if (!initialized) {
		missing_cb = callback;
		g_timeout_add (SCAN_MISSING_TIME, im_scan_missing, NULL);
		initialized = TRUE;
	}
}

/* inotify_lock must be held before calling */
void im_add (ih_sub_t *sub)
{
	if (g_list_find (missing_sub_list, sub)) 
		return;

	missing_sub_list = g_list_prepend (missing_sub_list, sub);
}

/* inotify_lock must be held before calling */
void im_rm (ih_sub_t *sub)
{
	GList *link;

	link = g_list_find (missing_sub_list, sub);

	if (!link)
		return;

	missing_sub_list = g_list_remove_link (missing_sub_list, link);
}

/* Scans the list of missing subscriptions checking if they
 * are available yet.
 */
static gboolean im_scan_missing (gpointer user_data)
{
	GList *nolonger_missing = NULL;
	GList *l;

	G_LOCK(inotify_lock);

	for (l = missing_sub_list; l; l = l->next)
	{
		ih_sub_t *sub = l->data;
		gboolean not_m = FALSE;

		g_assert (sub);
		ih_sub_setup (sub);
		not_m = ip_start_watching (sub);

		if (not_m)
		{
			missing_cb (sub);
			/* We have to build a list of list nodes to remove from the
			* missing_sub_list. We do the removal outside of this loop.
			*/
			nolonger_missing = g_list_prepend (nolonger_missing, l);
		}
	}

	for (l = nolonger_missing; l ; l = l->next)
	{
		GList *llink = l->data;
		missing_sub_list = g_list_remove_link (missing_sub_list, llink);
	}

	g_list_free (nolonger_missing);

	G_UNLOCK(inotify_lock);
	return TRUE;

}

