#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include "gnome-vfs-daemon.h"

/* Global daemon */
static GnomeVfsDaemon *daemon = NULL;


struct KeyChainEntry {
	gchar *username;
	gchar *password;
	gboolean persist_to_disk;
	GTimeVal last_access;
};


/* Structure containing all know passwords */
static GHashTable *keychain = NULL;

BONOBO_CLASS_BOILERPLATE_FULL(
	GnomeVfsDaemon,
	gnome_vfs_daemon,
	GNOME_VFS_Daemon,
	BonoboObject,
	BONOBO_TYPE_OBJECT);


static gchar *
get_suitable_uri (const char *uri)
{
	GnomeVFSURI *parsed_uri;
	gchar *res;

	parsed_uri = gnome_vfs_uri_new (uri);	
	if (parsed_uri == NULL) {
		return NULL;
	}
	res = gnome_vfs_uri_to_string (parsed_uri,
				       GNOME_VFS_URI_HIDE_USER_NAME |
				       GNOME_VFS_URI_HIDE_PASSWORD |
				       GNOME_VFS_URI_HIDE_FRAGMENT_IDENTIFIER);
	gnome_vfs_uri_unref (parsed_uri);

	return res;
}

static struct KeyChainEntry *find_key_in_chain (const GList *keychain, 
						const gchar *username)
{
	const GList *l;
	for (l = keychain; l != NULL; l = l->next) {
		struct KeyChainEntry *entry = (struct KeyChainEntry*)l->data;

		if (strcmp (username, entry->username) == 0) {
			return entry;
		}
	}
	return NULL;
}

static void *keychain_entry_free (struct KeyChainEntry *entry)
{
	if (entry != NULL) {
		g_free (entry->username);
		g_free (entry->password);
	}
}

/* Look if we have info for the user username in the information contained
 * in keychain. If the user is already known, replace the corresponding
 * KeyChainEntry with the new one 
 */
static void add_key_to_chain (GList **keychain, struct KeyChainEntry *entry)
{
	GList *l;

	for (l = *keychain; l != NULL; l = l->next) {
		struct KeyChainEntry *cur_ent = (struct KeyChainEntry*)l->data;

		if (strcmp (entry->username, cur_ent->username) == 0) {
			*keychain = g_list_remove (*keychain, cur_ent);
			keychain_entry_free (cur_ent);
		}
	}
	*keychain = g_list_append (*keychain, entry);
}

static struct KeyChainEntry *keychain_entry_new (const gchar *username, 
						 const gchar *password)
{
	struct KeyChainEntry *entry;

	entry = g_new0 (struct KeyChainEntry, 1);

	if (entry != NULL) {
		entry->username = g_strdup (username);
		entry->password = g_strdup (password);
		entry->persist_to_disk = FALSE;
		g_get_current_time (&entry->last_access);
	}

	return entry;
}

static void
set_password (PortableServer_Servant servant,
	      const CORBA_char      *uri,
	      const CORBA_char      *user,
	      const CORBA_char      *passwd,
	      CORBA_Environment     *ev)
{
	gchar *uri_to_insert;
	
	struct KeyChainEntry *entry = keychain_entry_new (user, passwd);
	GList *entries;

	if (entry == NULL) {
		/* Fail with CORBA error */
	}
	
	uri_to_insert = get_suitable_uri (uri);
	if (uri_to_insert == NULL) {
		/* Return CORBA error */
	}
	entries = g_hash_table_lookup (keychain, uri_to_insert);

	if (entries != NULL) {
		add_key_to_chain (&entries, entry);
	} else {
		entries = g_list_append (NULL, entry);
	}
	g_hash_table_insert (keychain, uri_to_insert, entries);
}

static CORBA_string
get_password (PortableServer_Servant _servant,
	      const CORBA_char *uri,
	      const CORBA_char *user,
	      CORBA_Environment * ev)
{
	GList *entries;
	gchar *parsed_uri;
	gchar *password;

	parsed_uri = get_suitable_uri (uri);
	entries = g_hash_table_lookup (keychain, parsed_uri);
	g_free (parsed_uri);
	if (user != NULL) {
		struct KeyChainEntry *entry;

		entry = find_key_in_chain (entries, user);
		if (entry != NULL) {
			return CORBA_string_dup (entry->password);
		} 
	} else {
		struct KeyChainEntry *entry;
		
		entry = (struct KeyChainEntry *)entries->data;
		/* FIXME: username should also be returned... */
		return CORBA_string_dup (entry->password);
	}

	/* FIXME: return NULL */
	return CORBA_string_dup ("");
}

static void
remove_client (ORBitConnection *cnx,
	       GNOME_VFS_Client client)
{
	g_signal_handlers_disconnect_by_func (
		cnx, G_CALLBACK (remove_client), client);

	daemon->clients = g_list_remove (daemon->clients, client);
	CORBA_Object_release (client, NULL);

	if (!daemon->clients) {
		/* FIXME: timeout / be more clever here ... */
		g_print ("All clients dead, quitting ...\n");
		bonobo_main_quit ();
	}
}

static void
de_register_client (PortableServer_Servant servant,
		    const GNOME_VFS_Client client,
		    CORBA_Environment     *ev)
{
	remove_client (ORBit_small_get_connection (client), client);
}

static void
register_client (PortableServer_Servant servant,
		 const GNOME_VFS_Client client,
		 CORBA_Environment     *ev)
{
	ORBitConnection *cnx;
	
	cnx = ORBit_small_get_connection (client);
	if (!cnx) {
		g_warning ("client died already !");
		return;
	}

	g_signal_connect (cnx, "broken",
			  G_CALLBACK (remove_client),
			  client);

	daemon->clients = g_list_prepend (
		daemon->clients,
		CORBA_Object_duplicate (client, NULL));
}

static void
gnome_vfs_daemon_finalize (GObject *object)
{
	BONOBO_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gnome_vfs_daemon_instance_init (GnomeVfsDaemon *daemon)
{
}

static void
gnome_vfs_daemon_class_init (GnomeVfsDaemonClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_VFS_Daemon__epv *epv = &klass->epv;

	object_class->finalize = gnome_vfs_daemon_finalize;
	
	epv->getPassword  = get_password;
	epv->setPassword  = set_password;
	epv->registerClient   = register_client;
	epv->deRegisterClient = de_register_client;
	
	gnome_vfs_init ();
	keychain = g_hash_table_new (g_str_hash, g_str_equal);
}

static BonoboObject *
gnome_vfs_daemon_factory (BonoboGenericFactory *factory,
			  const char           *component_id,
			  gpointer              closure)
{
	if (!daemon) {
		daemon = g_object_new (GNOME_TYPE_VFS_DAEMON, NULL);
		bonobo_object_set_immortal (BONOBO_OBJECT (daemon), TRUE);
	}
	return bonobo_object_ref (daemon);
}

BONOBO_ACTIVATION_FACTORY("OAFIID:GNOME_VFS_Daemon_Factory",
			  "gnome-vfs-daemon", "0.1",
			  gnome_vfs_daemon_factory,
			  NULL);
