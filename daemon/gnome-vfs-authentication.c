#include <bonobo-activation/bonobo-activation.h>
#include <libbonobo.h>
#include <libgnomevfs/gnome-vfs-standard-callbacks.h>
#include <libgnomevfs/gnome-vfs-module-callback-module-api.h>
#include "gnome-vfs-client.h"
#include "gnome-vfs-authentication.h"

static GnomeVfsClient    *client;
static GNOME_VFS_Daemon   daemon = CORBA_OBJECT_NIL;
static CORBA_Environment *ev, real_ev;


static void gnome_vfs_authn_init (void)
{
	client = g_object_new (GNOME_TYPE_VFS_CLIENT, NULL);

	CORBA_exception_init (&real_ev); ev = &real_ev;

	daemon = bonobo_activation_activate_from_id (
 		"OAFIID:GNOME_VFS_Daemon", 0, NULL, ev);

	if (!daemon) {
		g_warning ("Failed to activate the daemon '%s'",
			   bonobo_exception_get_text (ev));
		daemon = CORBA_OBJECT_NIL;
	}

	gnome_vfs_client_register (client, daemon, ev);
}


static GNOME_VFS_Daemon get_daemon ()
{
	if (daemon == CORBA_OBJECT_NIL) {
		gnome_vfs_authn_init();
	}
	return daemon;
}



gint gnome_vfs_authn_get_all_passwords (const gchar *uri, GList **passwords)
{
	GNOME_VFS_AuthInfoList* auth_info;
	GnomeVFSAuthToken *token;

	int i;

	if (!get_daemon ()) {
		*passwords = NULL;
		return -1;
	}

	auth_info = GNOME_VFS_Daemon_getAllPasswords (daemon, uri, ev);

	if (BONOBO_EX (ev)) {
		g_error (bonobo_exception_get_text (ev));
		*passwords = NULL;
		return -1;
	}

	for (i=0; i<auth_info->_length; i++) {
		token = g_new0 (GnomeVFSAuthToken, 1);
		token->username = g_strdup (auth_info->_buffer[i].username);
		token->password = g_strdup (auth_info->_buffer[i].password);
		*passwords = g_list_append (*passwords, token);
	}

	return 0;
}

gint gnome_vfs_authn_get_all_passwords_uri (const GnomeVFSURI *uri, 
					    GList **passwords)
{
	gchar *uri_str = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	gint result;

	if (uri_str == NULL) {
		*passwords = NULL;
		return -2;
	}
	result = gnome_vfs_authn_get_all_passwords (uri_str, passwords);
	g_free (uri_str);
	return result;
}


gint gnome_vfs_authn_get_password (const gchar *uri, 
				   const gchar *username,
				   gchar **password)
{
	GNOME_VFS_AuthInfoList* auth_info;
	
	if (!get_daemon ()) {
		*password = NULL;
		return -1;
	}

	g_print ("Looking up password for %s\n", uri);
	auth_info = GNOME_VFS_Daemon_getPassword (daemon, uri, username, ev);

	if (BONOBO_EX (ev)) {
		g_error (bonobo_exception_get_text (ev));
		*password = NULL;
		return -1;
	}

	if (auth_info->_length != 0) {
		*password = g_strdup (auth_info->_buffer[0].password);
	} else {
		*password = NULL;
	}

	return 0;
}


gint gnome_vfs_authn_get_password_uri (const GnomeVFSURI *uri, 
				       const gchar *username,
				       gchar **password)
{
	gchar *uri_str = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	gint result;

	if (uri_str == NULL) {
		*password = NULL;
		return -2;
	}
	result = gnome_vfs_authn_get_password (uri_str, username, password);
	g_free (uri_str);
	return result;
}


gint gnome_vfs_authn_set_password (const gchar *uri, 
				   const gchar *username, 
				   const gchar *password)
{
	/* Protect against empty username and/or password ? */

	if (!get_daemon ()) {
		return -1;
	}

	g_print ("%s URI: %s username: %s password: %s\n", G_GNUC_FUNCTION, 
		 uri, username, password);
	GNOME_VFS_Daemon_setPassword (daemon, uri, username, 
				      password,  ev);

	if (BONOBO_EX (ev)) {
		g_error (bonobo_exception_get_text (ev));
		return -1;
	}

	return 0;
}


gint gnome_vfs_authn_set_password_uri (const GnomeVFSURI *uri, 
				       const gchar *username,
				       const gchar *password)
{
	gchar *uri_str = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	gint result;

	if (uri_str == NULL) {
		return -2;
	}
	result = gnome_vfs_authn_set_password (uri_str, username, password);
	g_free (uri_str);
	return result;
}


gint gnome_vfs_authn_ask_password (const gchar *uri, 
				   gchar **username,
				   gchar **password,
				   gboolean first_time)
{
	GnomeVFSModuleCallbackAuthenticationIn in_args;
	GnomeVFSModuleCallbackAuthenticationOut out_args;
	gint res;

	memset (&in_args, 0, sizeof (in_args));
	in_args.uri = g_strdup_printf ("%s", uri);
	in_args.previous_attempt_failed = !first_time;

	memset (&out_args, 0, sizeof (out_args));
	g_print ("Invoking callback\n");
	res = gnome_vfs_module_callback_invoke (GNOME_VFS_MODULE_CALLBACK_AUTHENTICATION,
						&in_args, sizeof (in_args), 
						&out_args, sizeof (out_args));
	if (res == FALSE) {
		*password = NULL;
		*username = NULL;		
	} else {
		*username = out_args.username;
		*password = out_args.password;
	}
	return 0;
}

void gnome_vfs_authn_cleanup (void)
{	
	gnome_vfs_client_deregister (client, daemon, ev);

	bonobo_object_unref (client);
	CORBA_Object_release (daemon, NULL);
}
