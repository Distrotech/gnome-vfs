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
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_INOTIFY_H
/* We don't actually include the libc header, because there has been
 * problems with libc versions that was built without inotify support.
 * Instead we use the local version.
 */
#include "local_inotify.h"
#elif defined (HAVE_LINUX_INOTIFY_H)
#include <linux/inotify.h>
#endif
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "inotify-kernel.h"
#include "inotify-helper.h"

#define IH_WD_MISSING -1
#define IH_INOTIFY_MASK (IN_MODIFY|IN_ATTRIB|IN_MOVED_FROM|IN_MOVED_TO|IN_DELETE|IN_CREATE|IN_DELETE_SELF|IN_UNMOUNT|IN_MOVE_SELF)
static gboolean		inotify_debug_enabled = TRUE;
#define IH_W if (inotify_debug_enabled) g_warning 
#define SCAN_MISSING_TIME 500 /* 2 Hz */

/* This structure represents a path we are interested in watching. */
typedef struct ih_watched_dir_s {
    	char *path;
    	/* TODO: We need to maintain a tree of watched directories
	 * so that we can deliver move/delete events to sub folders.
	 */
	struct ih_watched_dir_s *parent;
	GList *			 children;

	/* Inotify state */
	guint32 wd;

	/* If this path is missing OR unreadable, then we poll */
	time_t last_poll_time;
	time_t poll_interval;

	/* List of inotify_subs */
	GList *subs;
} ih_watched_dir_t;

static ih_watched_dir_t *ih_watched_dir_new (const char *path, int wd);
static void ih_watched_dir_free (ih_watched_dir_t *dir);
static void ih_event_callback (ik_event_t *event);

static void ih_is_missing (ih_watched_dir_t *dir);
static void ih_is_not_missing (ih_watched_dir_t *dir, int wd);
static gboolean ih_scan_missing (gpointer user_data);

/* We share this lock with inotify-kernel.c
 *
 * inotify-kernel.c only takes the lock when it reads events from
 * the kernel and when it processes those events
 * 
 * We take the lock in all public functions and when we are scanning
 * the missing list
 */
G_LOCK_DEFINE (inotify_lock);

/* This hash holds ih_watched_dir_t *'s */
static GHashTable *	path_hash = NULL;
/* This hash holds GLists of ih_watched_dir_t *'s
 * We need to hold a list because symbolic links can share
 * the same wd
 */
static GHashTable *	wd_hash = NULL;

/* We put ih_watched_dir's that are missing on this list */
static GList *		missing_list = NULL;

static GnomeVFSMonitorEventType ih_mask_to_EventType (guint32 mask);

/**
 * Initializes the inotify backend.  This must be called before
 * any other functions in this module.
 *
 * @returns TRUE if initialization succeeded, FALSE otherwise
 */
gboolean
inotify_helper_init (void)
{
	static gboolean initialized = FALSE;
	static gboolean result = FALSE;

	G_LOCK(inotify_lock);
	
	if (initialized == TRUE) {
		G_UNLOCK(inotify_lock);
		return result;
	}

	initialized = TRUE;

	result = ik_startup (ih_event_callback);
	if (!result) {
		g_warning( "Could not initialize inotify\n");
		G_UNLOCK(inotify_lock);
		return FALSE;
	}

	path_hash = g_hash_table_new(g_str_hash, g_str_equal);
	wd_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
	g_timeout_add (SCAN_MISSING_TIME, ih_scan_missing, NULL);

	IH_W ("started gnome-vfs inotify backend\n");

	G_UNLOCK(inotify_lock);
	return TRUE;
}

inotify_sub *
inotify_sub_new (GnomeVFSURI *uri, GnomeVFSMonitorType mon_type)
{
	inotify_sub *sub = NULL;

	sub = g_new0 (inotify_sub, 1);
	sub->type = mon_type;
	sub->uri = uri;
	gnome_vfs_uri_ref (uri);

	if (mon_type == GNOME_VFS_MONITOR_FILE)
	{
		/* If we are watching a file, we break the directory and filename apart */
		sub->dir = gnome_vfs_uri_extract_dirname (uri);
		sub->filename = gnome_vfs_uri_extract_short_name (uri);
	} else {
		sub->dir = g_strdup(gnome_vfs_uri_get_path (uri));
	}

	return sub;
}

void
inotify_sub_free (inotify_sub *sub)
{
	if (sub->filename)
		g_free (sub->filename);
	g_free (sub->dir);
	gnome_vfs_uri_unref (sub->uri);
	g_free (sub);
}

/**
 * Adds a subscription to be monitored.
 */
gboolean
inotify_helper_add (inotify_sub * sub)
{
	const char *path = sub->dir;
	ih_watched_dir_t *dir;
	int wd, err;
	
	G_LOCK(inotify_lock);
	
	/* FIXME: Implement symlink policy */

	dir = g_hash_table_lookup (path_hash, path);
	/* We are already watching this path, so just add the subscription */
	if (dir) 
	{
		IH_W( "inotify: subscribing to %s\n", path);
		dir->subs = g_list_prepend (dir->subs, sub);
		G_UNLOCK(inotify_lock);
		return TRUE;
	}

	/* This is a new path */
	wd = ik_watch (path, IH_INOTIFY_MASK, &err);
	if (wd < 0) 
	{
		IH_W( "inotify: could not add watch for %s\n", path);
		if (err == EACCES) {
			IH_W( "inotify: adding %s to missing list PERM\n", path);
		} else {
			IH_W( "inotify: adding %s to missing list MISSING\n", path);
		}

		/* Create a new watched directory */
		ih_watched_dir_t *dir = ih_watched_dir_new (path, IH_WD_MISSING);
		ih_is_missing (dir);
		g_hash_table_insert(path_hash, dir->path, dir);
	} else {
	    /* Get the current list of watched_dirs associated with this wd */
	    GList *dir_list = g_hash_table_lookup (wd_hash, GINT_TO_POINTER(wd));

	    /* Create a new watched directory */
	    ih_watched_dir_t *dir = ih_watched_dir_new (path, wd);

	    /* Add this subcription to it */
	    dir->subs = g_list_prepend (dir->subs, sub);

	    /* Insert the watched_dir into the path hash */
	    g_hash_table_insert(path_hash, dir->path, dir);

	    /* Add this watched_dir to the current wd's directory list */
	    dir_list = g_list_prepend (dir_list, dir);
	    g_hash_table_replace(wd_hash, GINT_TO_POINTER(dir->wd), dir_list);
	    IH_W("inotify: started watching %s\n", path);
	}

	G_UNLOCK(inotify_lock);
	return TRUE;
}

/**
 * Removes a subscription which was being monitored.
 */
gboolean
inotify_helper_cancel (inotify_sub * sub)
{
	const char *path = sub->dir;
	ih_watched_dir_t *dir;

	G_LOCK(inotify_lock);

	dir = g_hash_table_lookup (path_hash, path);
	g_assert (dir);
	if (!g_list_find (dir->subs, sub))
	{
		G_UNLOCK(inotify_lock);
		return TRUE;
	}

	IH_W("inotify: unsubscribing from %s\n", dir->path);
	dir->subs = g_list_remove_all (dir->subs, sub);
	/* No one is watching this path anymore */
	if (g_list_length (dir->subs) == 0)
	{
		GList *dir_list = NULL;

		if (dir->wd == IH_WD_MISSING)
		{
		    /* Remove from missing list */
		    missing_list = g_list_remove_all (missing_list, dir);
		} else {
		    g_assert (dir->wd >= 0);
		    dir_list = g_hash_table_lookup (wd_hash, GINT_TO_POINTER(dir->wd));
		    g_assert (g_list_length (dir_list) > 0);
		    dir_list = g_list_remove_all (dir_list, dir);
		    /* We can ignore this wd because no one is watching it */
		    if (g_list_length (dir_list) == 0) 
		    {
			g_hash_table_remove (wd_hash, GINT_TO_POINTER(dir->wd));
			ik_ignore (dir->path, dir->wd);
		    }
		}
		
		/* Clean up after this watched_dir */
		g_hash_table_remove (path_hash, dir->path);
		ih_watched_dir_free (dir);
		IH_W ("inotify: stopped watching for %s\n", path);
	}

	sub->cancelled = TRUE;

	G_UNLOCK(inotify_lock);
	return TRUE;
}

static ih_watched_dir_t *
ih_watched_dir_new (const char *path, int wd)
{
    	ih_watched_dir_t *dir = g_new0(ih_watched_dir_t, 1);

	dir->path = g_strdup(path);
	dir->wd = wd;

	return dir;
}

static void
ih_watched_dir_free (ih_watched_dir_t * dir)
{
    	g_assert (g_list_length (dir->subs) == 0);
	g_free(dir->path);
	g_free(dir);
}

/* TODO: Figure out how we are going to send move events */
static void
ih_emit_one_event (ih_watched_dir_t *dir, ik_event_t *event, inotify_sub *sub)
{
	GnomeVFSMonitorEventType gevent;
	GnomeVFSURI *info_uri = NULL;
	gchar *fullpath = NULL;
	char *info_uri_str;

	g_assert (dir && event && sub);
	
	gevent = ih_mask_to_EventType (event->mask);
	if (gevent == -1) 
	{
	    return;
	}

	/* If we are looking for a particular file */
	if (sub->filename) 
	{
	    /* And this one isn't it, don't send anything */
	    if (strcmp(event->name, sub->filename))
		return;

	    fullpath = g_strdup_printf ("%s/%s", dir->path, event->name);
	} else {
	    /* We are sending events for a directory */
	    if (strlen (event->name) == 0)
		fullpath = g_strdup (dir->path);
	    else
		fullpath = g_strdup_printf ("%s/%s", dir->path, event->name);
	}

	info_uri_str = gnome_vfs_get_uri_from_local_path (fullpath);
	info_uri = gnome_vfs_uri_new (info_uri_str);
	g_free (info_uri_str);
	gnome_vfs_monitor_callback ((GnomeVFSMethodHandle *)sub, info_uri, gevent);
	gnome_vfs_uri_unref (info_uri);
	g_free(fullpath);
}

static void
ih_emit_event (ih_watched_dir_t *dir, ik_event_t *event)
{
	GList *l;

	if (!dir||!event)
		return;

	for (l = dir->subs; l; l = l->next) {
		inotify_sub *sub = l->data;
		ih_emit_one_event (dir, event, sub);
	}
}

static void ih_wd_delete (gpointer data, gpointer user_data)
{
    ih_watched_dir_t *dir = data;
	GList *l = NULL;

	for (l = dir->subs; l; l = l->next)
	{
		inotify_sub *sub = l->data;
		ik_event_t *event = ik_event_new_dummy (sub->filename, dir->wd, IN_DELETE_SELF);
		ih_emit_one_event (dir, event, sub);
		ik_event_free (event);
	}

    ih_is_missing (dir);
}

/* Called by the inotify-kernel layer for each event */
static void
ih_event_callback (ik_event_t *event)
{
   	GList *dir_list = NULL;
	GList *pair_dir_list = NULL;
	GList *l = NULL;

	dir_list = g_hash_table_lookup (wd_hash, GINT_TO_POINTER(event->wd));
	if (event->pair)
	    pair_dir_list = g_hash_table_lookup (wd_hash, GINT_TO_POINTER(event->pair->wd));
	
	/* We can ignore IN_IGNORED events */
	if (event->mask & IN_IGNORED)
		return;

	if (event->mask & IN_DELETE_SELF || event->mask & IN_MOVE_SELF) 
	{
	    /* When a wd is deleted we need to put all the paths associated with
	     * the wd on the missing list */
	    g_hash_table_remove (wd_hash, GINT_TO_POINTER(event->wd));
	    g_list_foreach (dir_list, ih_wd_delete, NULL);
	    g_list_free (dir_list);
	    ik_event_free (event);
	    return;
	}

	if (event->mask & IN_Q_OVERFLOW) 
	{
		/* At this point we have missed some events, and no longer have a consistent
		 * view of the filesystem.
		 */
		g_warning( "inotify: DANGER, queue over flowed! Events have been missed.\n");
		ik_event_free (event);
		return;
	}


	if (!(event->mask & IH_INOTIFY_MASK))
	{
	    g_warning( "inotify: unhandled event->mask = %d\n", event->mask);
	    ik_event_free (event);
	    return;
	}

	/* We need to send out events to all watched_dirs listening on this watch descriptor. */
	for (l = dir_list; l; l = l->next)
	{
	    ih_watched_dir_t *dir = l->data;
	    ih_emit_event (dir, event);
	}

	for (l = pair_dir_list; l; l = l->next)
	{
	    ih_watched_dir_t *dir = l->data;
	    ih_emit_event (dir, event->pair);
	}

	ik_event_free (event);
}

/* Called when we are asked to watch a directory
 * that doesn't exist
 */
static void ih_is_missing (ih_watched_dir_t *dir)
{
    /* We can't re-add something to the missing list */
    g_assert (g_list_find (missing_list, dir) == NULL);
    dir->wd = IH_WD_MISSING;
    missing_list = g_list_prepend (missing_list, dir);
}

/* Sends CREATE events for a directory that was just created */
static void ih_send_not_missing_events (ih_watched_dir_t *dir)
{
    GList *l = NULL;


    for (l = dir->subs; l; l = l->next)
    {
	inotify_sub *sub = l->data;
	ik_event_t *event = NULL;

	if (sub->filename) 
	{
	    char *fullpath = NULL;
	    fullpath = g_strdup_printf ("%s/%s", dir->path, event->name);
	    if (g_file_test (fullpath, G_FILE_TEST_EXISTS))
	    {
		event = ik_event_new_dummy (sub->filename, dir->wd, IN_CREATE);
	    }
	    g_free (fullpath);
	} else {
	    event = ik_event_new_dummy ("", dir->wd, IN_CREATE|IN_ISDIR);
	}

	ih_emit_one_event (dir, event, sub);
	ik_event_free (event);

    }
}

/* Called by ih_scan_missing when it discovers that a directory
 * is no longer missing.
 */
static void ih_is_not_missing (ih_watched_dir_t *dir, int wd)
{
    dir->wd = wd;
    /* Get the current list of watched_dirs associated with this wd */
    GList *dir_list = g_hash_table_lookup (wd_hash, GINT_TO_POINTER(wd));
    /* Add this watched_dir to the current wd's directory list */
    dir_list = g_list_prepend (dir_list, dir);
    g_hash_table_replace(wd_hash, GINT_TO_POINTER(dir->wd), dir_list);
    ih_send_not_missing_events (dir);
}

/* Scans the list of missing directories checking if they
 * are available yet.
 */
static gboolean ih_scan_missing (gpointer user_data)
{
    GList *nolonger_missing = NULL;
    time_t now = time(NULL);
    GList *l;

    G_LOCK(inotify_lock);

    for (l = missing_list; l; l = l->next)
    {
	ih_watched_dir_t *dir = l->data;
	int wd,err;

	g_assert (dir);

	if (now - dir->last_poll_time < dir->poll_interval)
	    continue;

	dir->last_poll_time = now;
	wd = ik_watch (dir->path, IH_INOTIFY_MASK, &err);
	if (wd >= 0)
	{
	    ih_is_not_missing (dir, wd);
	    /* We have to build a list of list nodes to remove from the
	     * missing_list. We do the removal outside of this loop.
	     */
	    nolonger_missing = g_list_prepend (nolonger_missing, l);
	}
    }

    for (l = nolonger_missing; l ; l = l->next)
    {
	GList *llink = l->data;
	missing_list = g_list_remove_link (missing_list, llink);
    }

    g_list_free (nolonger_missing);
    G_UNLOCK(inotify_lock);
    return TRUE;

}

/* Transforms a inotify event to a gnome-vfs event. */
static GnomeVFSMonitorEventType
ih_mask_to_EventType (guint32 mask)
{
	mask &= ~IN_ISDIR;
	switch (mask)
	{
	case IN_MODIFY:
		return GNOME_VFS_MONITOR_EVENT_CHANGED;
	break;
	case IN_ATTRIB:
		return GNOME_VFS_MONITOR_EVENT_METADATA_CHANGED;
	break;
	case IN_MOVE_SELF:
	case IN_MOVED_FROM:
	case IN_DELETE:
	case IN_DELETE_SELF:
		return GNOME_VFS_MONITOR_EVENT_DELETED;
	break;
	case IN_CREATE:
	case IN_MOVED_TO:
		return GNOME_VFS_MONITOR_EVENT_CREATED;
	break;
	case IN_Q_OVERFLOW:
	case IN_OPEN:
	case IN_CLOSE_WRITE:
	case IN_CLOSE_NOWRITE:
	case IN_UNMOUNT:
	case IN_ACCESS:
	case IN_IGNORED:
	default:
		return -1;
	break;
	}
}

