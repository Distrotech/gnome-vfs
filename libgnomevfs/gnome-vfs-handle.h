/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-handle.h - Handle object for GNOME VFS files.

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

#ifndef GNOME_VFS_HANDLE_H
#define GNOME_VFS_HANDLE_H

#include <libgnomevfs/gnome-vfs-context.h>
#include <libgnomevfs/gnome-vfs-file-size.h>
#include <libgnomevfs/gnome-vfs-file-info.h>

G_BEGIN_DECLS

typedef gpointer GnomeVFSMethodHandle;
typedef struct GnomeVFSHandle GnomeVFSHandle;

/* Open mode.  If you don't set `GNOME_VFS_OPEN_RANDOM', you have to access the
   file sequentially.  */
typedef enum {
        GNOME_VFS_OPEN_NONE = 0,
        GNOME_VFS_OPEN_READ = 1 << 0,
        GNOME_VFS_OPEN_WRITE = 1 << 1,
        GNOME_VFS_OPEN_RANDOM = 1 << 2
} GnomeVFSOpenMode;

/* This is used to specify the start position for seek operations.  */
typedef enum {
        GNOME_VFS_SEEK_START,
        GNOME_VFS_SEEK_CURRENT,
        GNOME_VFS_SEEK_END
} GnomeVFSSeekPosition;


G_END_DECLS

#endif /* GNOME_VFS_HANDLE_H */
