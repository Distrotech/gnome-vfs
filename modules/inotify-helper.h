/* inotify-helper.h - GNOME VFS Monitor using inotify

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

   Author: John McCutchan <ttb@tentacle.dhs.org>
*/


#ifndef __INOTIFY_HELPER_H
#define __INOTIFY_HELPER_H

#include <libgnomevfs/gnome-vfs-monitor-private.h>
#include <glib.h>

typedef struct inotify_sub {
	GnomeVFSURI *uri;
	char *path;
	gboolean cancelled;
	gboolean dir;
} inotify_sub;

inotify_sub	*inotify_sub_new		(GnomeVFSURI *uri, GnomeVFSMonitorType);
void		 inotify_sub_free		(inotify_sub *sub);

gboolean	 inotify_helper_init		(void);
gboolean	 inotify_helper_add		(inotify_sub *sub);
gboolean	 inotify_helper_remove		(inotify_sub *sub);

#endif /* __INOTIFY_HELPER_H */
