/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-module-api.h - functions that a module can use, but not an applicatoin

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

   Author: Michael Fleming <mfleming@eazel.com> */

#ifndef GNOME_VFS_MODULE_API
#define GNOME_VFS_MODULE_API

#include <glib.h>

gboolean gnome_vfs_callback_call_hook (const char    *hookname,
				       gconstpointer  in,
				       gsize          in_size,
				       gpointer       out,
				       gsize          out_size);

#endif /* GNOME_VFS_MODULE_API */
