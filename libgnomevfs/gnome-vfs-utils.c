/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-utils.c - Private utility functions for the GNOME Virtual
   File System.

   Copyright (C) 1999 Free Software Foundation
   Copyright (C) 2000 Eazel, Inc.

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

   Authors: Ettore Perazzoli <ettore@comm2000.it>
   	    John Sullivan <sullivan@eazel.com> 
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


gchar*
gnome_vfs_file_size_to_string   (GnomeVFSFileSize bytes)
{
	if (bytes < (GnomeVFSFileSize) 1e3) {
		if (bytes == 1)
			return g_strdup (_("1 byte"));
		else
			return g_strdup_printf (_("%u bytes"),
						       (guint) bytes);
	} else {
		gdouble displayed_size;

		if (bytes < (GnomeVFSFileSize) 1e6) {
			displayed_size = (gdouble) bytes / 1.0e3;
			return g_strdup_printf (_("%.1fK"),
						       displayed_size);
		} else if (bytes < (GnomeVFSFileSize) 1e9) {
			displayed_size = (gdouble) bytes / 1.0e6;
			return g_strdup_printf (_("%.1fM"),
						       displayed_size);
		} else {
			displayed_size = (gdouble) bytes / 1.0e9;
			return g_strdup_printf (_("%.1fG"),
						       displayed_size);
		}
	}
}
