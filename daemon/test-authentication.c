#include <libbonobo.h>
#include "gnome-vfs-authentication.h"

int main (int argc, char **argv)
{

	gchar *password;
	GList *passwords = NULL;

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
	gnome_vfs_authn_get_password ("ftp://ftp.unkown.com", "teufbis", &password);
	if (password == NULL) {
		printf ("Password not found\n");
	}

	gnome_vfs_authn_get_all_passwords ("ftp://ftp.toto.com", &passwords);
	g_print ("Known passwords for ftp://ftp.toto.com:\n");
	for (passwords; passwords != NULL; passwords = passwords->next) {
		GnomeVFSAuthToken *auth = (GnomeVFSAuthToken *)passwords->data;
		g_print ("%s:%s\n", auth->username, auth->password);
	}

	
	gnome_vfs_authn_cleanup ();

	bonobo_debug_shutdown ();
	
}
