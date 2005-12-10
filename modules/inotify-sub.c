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
#include <string.h>
#include <glib.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "inotify-sub.h"

ih_sub_t *
ih_sub_new (GnomeVFSURI *uri, GnomeVFSMonitorType mon_type)
{
	ih_sub_t *sub = NULL;

	sub = g_new0 (ih_sub_t, 1);
	sub->type = mon_type;
	sub->uri = uri;
	gnome_vfs_uri_ref (uri);
	sub->path = gnome_vfs_unescape_string (gnome_vfs_uri_get_path (uri), "/");
	if (!sub->path)
	{
		g_free (sub);
		gnome_vfs_uri_unref (uri);
		return NULL;
	}
/* TODO: WAITING for flag to be implemented
	if (mon_type & GNOME_VFS_DONT_FOLLOW_SYMLINK)
	{
		sub->extra_flags |= IN_DONT_FOLLOW;
	}
*/
	return sub;
}

void
ih_sub_free (ih_sub_t *sub)
{
	if (sub->filename)
		g_free (sub->filename);
	if (sub->dir)
	    g_free (sub->dir);
	g_free (sub->path);
	gnome_vfs_uri_unref (sub->uri);
	g_free (sub);
}

/* Determines if the subscription is watching
 * 1) A file
 * 2) A symlink with dont_follow_symlink flag set
 * 3) A directory
 *
 * Sets up internal subscription state:
 * sub->dir is the path that we want to monitor with inotify
 * and sub->filename is the filename when needed.
 *
 * XXX
 * This is racey because in the path could be deleted and recreated in
 * between the time we call this and the time we activate inotify on the
 * path.
 * XXX
 *
 * There are two cases where we need to call this:
 *
 * 1) The first time we add this subscription
 * 2) When the missing list reactivates this subscription
 */
void
ih_sub_setup (ih_sub_t *sub)
{
	gchar *t;
	size_t len;
	if (sub->dir)
		g_free (sub->dir);
	if (sub->filename)
		g_free (sub->filename);

	if (sub->type & GNOME_VFS_MONITOR_DIRECTORY)
	{
		sub->dir = gnome_vfs_unescape_string (gnome_vfs_uri_get_path (sub->uri), "/");
		/* Just put something in there */
		if (!sub->dir)
			sub->dir = g_strdup (gnome_vfs_uri_get_path (sub->uri));
	} else {
		t = gnome_vfs_uri_extract_dirname (sub->uri);
		sub->dir = gnome_vfs_unescape_string (t, "/");
		if (!sub->dir)
			sub->dir = t;
		else
			g_free (t);

		t = gnome_vfs_uri_extract_short_name (sub->uri);
		sub->filename = gnome_vfs_unescape_string (t, "/");
		if (!sub->filename)
			sub->filename = t;
		else
			g_free (t);
	}

	len = strlen (sub->dir);

	/* We need to strip a trailing slash
	 * to get the correct behaviour
	 * out of the kernel
	 */
	if (sub->dir[len] == '/')
		sub->dir[len] = '\0';
}
