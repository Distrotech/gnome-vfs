#include <libgnomevfs/gnome-vfs-uri.h>

/**
 *  Queries the GnomeVFS daemon for a password
 *  @uri: uri we want a password for (the username, password and fragment part
 *  of the URI will be ignored)
 *  @username: user we want a password for
 *  @password: newly allocated string which contains the password if the lookup
 *  was successful, NULL otherwise
 *
 *  Return Value: 0 if a password was found
 */
gint gnome_vfs_authn_get_password_uri (const GnomeVFSURI *uri, 
				       const gchar *username,
				       gchar **password);
/**
 * Tells the GnomeVFS daemon to record a password for an uri.
 * @uri: uri 
 * @username: 
 * @password:
 * Return Value: 0 if the password was successfully saved
 */
gint gnome_vfs_authn_set_password_uri (const GnomeVFSURI *uri, 
				       const gchar *username, 
				       const gchar *password);

/**
 *  Queries the GnomeVFS daemon for a password.
 *  @uri: uri we want a password for (the username, password and fragment part
 *  of the URI will be ignored)
 *  @username: user we want a password for
 *  @password: newly allocated string which contains the password if the lookup
 *  was successful, NULL otherwise
 *
 *  Return Value: 0 if a password was found
 */
gint gnome_vfs_authn_get_password (const gchar *uri, 
				   const gchar *username,
				   gchar **password);

/**
 * Tells the GnomeVFS daemon to record a password for an uri.
 * @uri: uri 
 * @username: 
 * @password:
 * Return Value: 0 if the password was successfully saved
 */
gint gnome_vfs_authn_set_password (const gchar *uri, 
				   const gchar *username, 
				   const gchar *password);

gint gnome_vfs_authn_get_password_ask_user (const gchar *uri, 
					    gchar **username,
					    gchar **password,
					    gboolean first_time);


void gnome_vfs_authn_cleanup (void);
