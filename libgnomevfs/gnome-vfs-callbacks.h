/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-callbacks.h - various callback declarations

   Copyright (C) 2001 Free Software Foundation

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

   Author: Seth Nickell <snickell@stanford.edu>
*/

#ifndef GNOME_VFS_CALLBACKS_H
#define GNOME_VFS_CALLBACKS_H

#include <glib/gtypes.h>

G_BEGIN_DECLS

/* Used to report user-friendly status messages you might want to display. */
typedef void (* GnomeVFSStatusCallback) (const gchar  *message,
					 gpointer      callback_data);
typedef void (* GnomeVFSCallback)       (gpointer      user_data,
					 gconstpointer in,
					 gsize         in_size,
					 gpointer      out,
					 gsize         out_size);

typedef struct GnomeVFSMessageCallbacks GnomeVFSMessageCallbacks;

G_END_DECLS

#endif
