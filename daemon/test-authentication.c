#include <libbonobo.h>
#include "gnome-vfs-authentication.h"

int main (int argc, char **argv)
{

	gchar *password;

	if (!bonobo_init (&argc, argv)) {
		g_error ("Failed to initialize");
	}

	printf ("GnomeVFS Authentication Daemon Test\n");
	printf ("===================================\n\n");
	
	gnome_vfs_authn_set_password ("ftp://ftp.toto.com", "teuf", "titi");
	gnome_vfs_authn_set_password ("ftp://ftp.toto.com", "joe", "tata");
	gnome_vfs_authn_set_password ("ftp://toto:lf@ftp.toto2.com", "teufbis", "tutu");
	gnome_vfs_authn_get_password ("ftp://ftp.toto.com", "teuf", &password);
	g_print ("Password: %s\n", password);
	gnome_vfs_authn_get_password ("ftp://ftp.toto.com", "joe", &password);
	g_print ("Password: %s\n", password);
	gnome_vfs_authn_get_password ("ftp://ftp.toto2.com", "teufbis", &password);
	g_print ("Password: %s\n", password);
	gnome_vfs_authn_cleanup ();

	bonobo_debug_shutdown ();
	
}
