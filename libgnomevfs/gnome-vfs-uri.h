/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-uri.h - URI handling for the GNOME Virtual File System.

   Copyright (C) 1999 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Ettore Perazzoli <ettore@comm2000.it>
*/

#ifndef _GNOME_VFS_URI_H
#define _GNOME_VFS_URI_H

GnomeVFSURI *gnome_vfs_uri_new         (const gchar *text_uri);
GnomeVFSURI *gnome_vfs_uri_ref         (GnomeVFSURI *uri);
void         gnome_vfs_uri_unref       (GnomeVFSURI *uri);
void         gnome_vfs_uri_destroy     (GnomeVFSURI *uri);
GnomeVFSURI *gnome_vfs_uri_append_text (const GnomeVFSURI *uri, ...);
gchar       *gnome_vfs_uri_to_string   (const GnomeVFSURI *uri);
GnomeVFSURI *gnome_vfs_uri_dup         (const GnomeVFSURI *uri);
gboolean     gnome_vfs_uri_is_local    (const GnomeVFSURI *uri);

#endif /* _GNOME_VFS_URI_H */
