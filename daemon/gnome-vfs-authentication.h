#ifndef GNOME_VFS_AUTHENTICATION_H

#define GNOME_VFS_AUTHENTICATION_H

#include <libgnomevfs/gnome-vfs-uri.h>

/* Contains authentication information for a given uri */
/* Might need to be extended later */
struct GnomeVFSAuthToken {
	gchar *username;
	gchar *password;
};

typedef struct GnomeVFSAuthToken GnomeVFSAuthToken;

/** 
 *  Queries the GnomeVFS daemon for all known authentication entries
 *  for a given URI
 *  @uri: uri we want to get authentication info from (username, password and
 *  fragment part will be ignored)
 *  @passwords: list of GnomeVFSAuthToken
 *
 *  Return Value: 0 if everything is successful
 */
gint gnome_vfs_authn_get_all_passwords (const gchar *uri, GList **passwords);

/**
 *  Same as gnome_vfs_autn_get_all_passwords except it takes an URI
 */
gint gnome_vfs_authn_get_all_passwords_uri (const GnomeVFSURI *uri, 
					    GList **passwords);

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

gint gnome_vfs_authn_ask_password (const gchar *uri, 
				   gchar **username,
				   gchar **password,
				   gboolean first_time);


void gnome_vfs_authn_cleanup (void);

#endif /* GNOME_VFS_AUTHENTICATION_H */
