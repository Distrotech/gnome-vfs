/* test-mime.c - Test for the mime handler detection features of the GNOME
   Virtual File System Library

   Copyright (C) 2000 Eazel

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

   Author: Maciej Stachowiak <mjs@eazel.com>
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-mime-handlers.h"

#include <stdio.h>


static void
print_application (GnomeVFSMimeApplication *application)
{
        if (application == NULL) {
	        puts ("(none)");
	} else {
	        printf ("name: %s\ncommand: %s\ncan_open_multiple_files: %s\ncan_open_uris: %s\n", 
			application->name, application->command, 
			(application->can_open_multiple_files ? "TRUE" : "FALSE"),
			(application->can_open_uris ? "TRUE" : "FALSE"));
	}
}

static void
print_component (OAF_ServerInfo *component)
{
        if (component == NULL) {
	        puts ("(none)");
	} else {
	        printf ("iid: %s\n", component->iid);
	}
}

static void
print_action (GnomeVFSMimeAction *action)
{
        if (action == NULL) {
	        puts ("(none)");
	} else {
	  if (action->action_type == GNOME_VFS_MIME_ACTION_TYPE_APPLICATION) {
  	        puts ("type: application");
		print_application (action->action.application);
	  } else {
  	        puts ("type: component");
		print_component (action->action.component);
	  }
	}
}


int
main (int argc, char **argv)
{
        const char *type;  
	GnomeVFSMimeApplication *default_application;
	OAF_ServerInfo *default_component;
	GnomeVFSMimeAction *default_action;

	oaf_init (argc, argv);
	gnome_vfs_init ();

	if (argc != 2) {
		fprintf (stderr, "Usage: %s mime_type\n", *argv);
		return 1;
	}

	type = argv[1];
	
	default_action = gnome_vfs_mime_get_default_action (type);
	puts ("Default Component");
	print_action (default_action);
	puts ("");

	default_application = gnome_vfs_mime_get_default_application (type);
	puts("Default Application");
	print_application (default_application);
	puts ("");
		
	default_component = gnome_vfs_mime_get_default_component (type);
	puts("Default Component");
	print_component (default_component);
	puts ("");
	
	return 0;
}


