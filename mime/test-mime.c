#include "mime-db.h"

int 
main (int argc, char **argv)
{	
	GList *mime_types, *it;

	init_mime_db();
	/*	mime_types = gnome_vfs_get_mime_types ();
	for (it = mime_types; it != NULL; it = it->next) {
		g_print ("%s\n", (gchar*)it->data);
		}*/
	g_print ("get_description (ogg): %s\n", gnome_vfs_mime_get_description ("application/ogg"));
	g_print ("get_user_attribute (text) %s\n", gnome_vfs_mime_get_user_attribute ("text/plain", "my_attribute"));
	gnome_vfs_mime_set_description ("application/ogg", "test");
	gnome_vfs_mime_set_user_attribute ("text/plain", "my_attribute", "hehe");

	g_print ("\n\n");

	g_print ("get_description (ogg) %s\n", gnome_vfs_mime_get_description ("application/ogg"));
	g_print ("get_user_attribute (text) %s\n", gnome_vfs_mime_get_user_attribute ("text/plain", "my_attribute"));
	g_print ("get_user_attribute (text) %s\n", gnome_vfs_mime_get_user_attribute ("text/plain", "my_att"));
	persist_user_changes ();
	return 0;
}
