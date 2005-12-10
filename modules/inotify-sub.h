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


#ifndef __INOTIFY_SUB_H
#define __INOTIFY_SUB_H

typedef struct {
	GnomeVFSURI *uri;
	GnomeVFSMonitorType type;
	char *path;
	char *dir;
	char *filename;
	guint32 extra_flags;
	gboolean cancelled;
	gboolean missing;
} ih_sub_t;

ih_sub_t	*ih_sub_new		(GnomeVFSURI *uri, GnomeVFSMonitorType);
void		 ih_sub_free 	 	(ih_sub_t *sub);
void		 ih_sub_setup		(ih_sub_t *sub);

#endif /* __INOTIFY_SUB_H */
