#include "mime-db.h"

int 
main (int argc, char **argv)
{	
	GList *mime_types, *it;

	init_mime_db();
	mime_types = gnome_vfs_get_mime_types ();
	for (it = mime_types; it != NULL; it = it->next) {
		g_print ("%s\n", (gchar*)it->data);
	}
	return 0;
}
