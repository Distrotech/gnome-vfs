/* gnome-vfs-mime-info.h
 *
 * Copyright (C) 1998 Miguel de Icaza
 * Copyright (C) 2000 Eazel, Inc
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 */

#ifndef GNOME_VFS_MIME_INFO_H
#define GNOME_VFS_MIME_INFO_H

#include <libgnome/gnome-defs.h>
#include <glib.h>

BEGIN_GNOME_DECLS

const char *gnome_vfs_mime_get_value		  (const char *mime_type,
						   const char *key);
GList      *gnome_vfs_mime_get_keys		  (const char *mime_type);

const char *gnome_vfs_mime_program		  (const char* mime_type);
const char *gnome_vfs_mime_description		  (const char* mime_type);
const char *gnome_vfs_mime_test			  (const char* mime_type);
const char *gnome_vfs_mime_composetyped		  (const char* mime_type);
gboolean    gnome_vfs_mime_copiousoutput	  (const char* mime_type,
						   char *key);
gboolean    gnome_vfs_mime_needsterminal	  (const char* mime_type,
						   char *key);

gchar      *gnome_vfs_mime_program_name		  (const char *program_name);

void        gnome_vfs_mime_info_reload            (void);
void	    gnome_vfs_mime_info_clear		  (void);

END_GNOME_DECLS

#endif
