#include <config.h>

#include <gmodule.h>
#include <libgnomevfs/gnome-vfs-app-context.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-standard-callbacks.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static gboolean authn_callback_called = FALSE;

/* For this test case to function, these two URI's should
 * require a username/password (set in authn_username, authn_password below)
 * and AUTHN_URI_CHILD should be a child of AUTHN_URI
 */
#define AUTHN_URI_CHILD "http://localhost/~mikef/protected/index.html"
#define AUTHN_URI "http://localhost/~mikef/protected/"

static const char *authn_username = "foo";
static const char *authn_password = "foo";

static void /* GnomeVFSCallback */
authn_callback (gpointer user_data, gconstpointer in, size_t in_size, gpointer out, size_t out_size)
{
	GnomeVFSCallbackSimpleAuthIn *in_real;
	GnomeVFSCallbackSimpleAuthOut *out_real;

	/* printf ("in authn_callback\n"); */
	
	g_return_if_fail (sizeof (GnomeVFSCallbackSimpleAuthIn) == in_size
		&& sizeof (GnomeVFSCallbackSimpleAuthOut) == out_size);

	g_return_if_fail (in != NULL);
	g_return_if_fail (out != NULL);

	in_real = (GnomeVFSCallbackSimpleAuthIn *)in;
	out_real = (GnomeVFSCallbackSimpleAuthOut *)out;

	/* printf ("in uri: %s realm: %s\n", in_real->uri, in_real->realm); */
	
	out_real->username = g_strdup (authn_username);
	out_real->password = g_strdup (authn_password);

	authn_callback_called = TRUE;
}

static gboolean destroy_notify_occurred = FALSE;

static void /*GDestroyNotify*/
app_context_destroy_notify (gpointer user_data)
{
	destroy_notify_occurred = TRUE;
}

static volatile gboolean open_callback_occurred = FALSE;

static GnomeVFSResult open_callback_result_expected = GNOME_VFS_OK;

static void /* GnomeVFSAsyncOpenCallback */
open_callback (GnomeVFSAsyncHandle *handle,
	       GnomeVFSResult result,
	       gpointer callback_data)
{
	g_assert (result == open_callback_result_expected);

	open_callback_occurred = TRUE;
}

static volatile gboolean close_callback_occurred = FALSE;

static void /* GnomeVFSAsyncOpenCallback */
close_callback (GnomeVFSAsyncHandle *handle,
		GnomeVFSResult result,
		gpointer callback_data)
{
	close_callback_occurred = TRUE;
}

static void
stop_after_log (const char *domain, GLogLevelFlags level, 
	const char *message, gpointer data)
{
	void (* saved_handler) (int);
	
	g_log_default_handler (domain, level, message, data);

	saved_handler = signal (SIGINT, SIG_IGN);
	raise (SIGINT);
	signal (SIGINT, saved_handler);
}

static void
make_asserts_break (const char *domain)
{
	g_log_set_handler
		(domain, 
		 (GLogLevelFlags) (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING),
		 stop_after_log, NULL);
}

static void (*flush_credentials_func)(void);

int
main (int argc, char **argv)
{
        GnomeVFSAppContext *app_context;
        GnomeVFSCallback callback;
        gpointer user_data;
        GnomeVFSHandle *handle;
        GnomeVFSResult result;
	GnomeVFSAsyncHandle *async_handle;
	char *module_path;
	char *authn_uri, *authn_uri_child;
	GModule *module;
	guint i;

	make_asserts_break ("GLib");
	make_asserts_break ("GnomeVFS");

	if (argc == 2) {
		authn_uri = argv[1];
		authn_uri_child = g_strdup_printf("%s/./", authn_uri);
	} else if (argc == 3) {
		authn_uri = argv[1];
		authn_uri_child = argv[2];
	} else {
		authn_uri = AUTHN_URI;
		authn_uri_child = AUTHN_URI_CHILD;
	}

	gnome_vfs_init ();

	/* Load http module so we can snag the test hook */
	module_path = g_module_build_path (MODULES_PATH, "http");
	module = g_module_open (module_path, G_MODULE_BIND_LAZY);
	g_free (module_path);
	module_path = NULL;

	if (module == NULL) {
		fprintf (stderr, "Couldn't load http module \n");
		exit (-1);
	}

	g_module_symbol (module, "http_authn_test_flush_credentials", (gpointer *) &flush_credentials_func);

	if (flush_credentials_func == NULL) {
		fprintf (stderr, "Couldn't find http_authn_test_flush_credentials\n");
		exit (-1);
	}

	/* Test 1: Attempt to access a URI requiring authn w/o a callback registered */

	result = gnome_vfs_open (&handle, authn_uri, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_ERROR_ACCESS_DENIED);
	handle = NULL;

	/* Set up app context */

	app_context = gnome_vfs_app_context_new ();

	g_assert (app_context != NULL);

	gnome_vfs_app_context_set_callback (app_context, GNOME_VFS_HOOKNAME_BASIC_AUTH, authn_callback, NULL);

	gnome_vfs_app_context_push_takesref (app_context);

	g_assert (app_context == gnome_vfs_app_context_get_current());

	callback = gnome_vfs_app_context_get_callback (gnome_vfs_app_context_get_current(), "random-crap", &user_data, NULL);
	g_assert (callback == NULL);

	callback = gnome_vfs_app_context_get_callback (gnome_vfs_app_context_get_current(), GNOME_VFS_HOOKNAME_BASIC_AUTH, &user_data, NULL);
	g_assert (callback == authn_callback);
	g_assert (user_data == NULL);

	/* Test 2: Attempt an async open that requires http authentication */

	authn_callback_called = FALSE;
	
	open_callback_occurred = FALSE;
	open_callback_result_expected = GNOME_VFS_OK;

	gnome_vfs_async_open (
		&async_handle, 
		authn_uri,
		GNOME_VFS_OPEN_READ,
		open_callback,
		NULL);

	while (!open_callback_occurred) {
		g_main_context_iteration (NULL, TRUE);
	}

	close_callback_occurred = FALSE;
	gnome_vfs_async_close (async_handle, close_callback, NULL);

	while (!close_callback_occurred) {
		g_main_context_iteration (NULL, TRUE);
	}

	g_assert (authn_callback_called);


	/* Test 3: Attempt a sync call to the same location;
	 * credentials should be stored so the authn_callback function
	 * should not be called
	 */
	
	authn_callback_called = FALSE;
	result = gnome_vfs_open (&handle, authn_uri, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_OK);
	gnome_vfs_close (handle);
	handle = NULL;
	/* The credentials should be in the cache, so we shouldn't have been called */
	g_assert (authn_callback_called == FALSE);

	/* Test 4: Attempt a sync call to something deeper in the namespace.
	 * which should work without a callback too
	 */

	authn_callback_called = FALSE;
	result = gnome_vfs_open (&handle, authn_uri_child, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_OK);
	gnome_vfs_close (handle);
	handle = NULL;
	/* The credentials should be in the cache, so we shouldn't have been called */
	g_assert (authn_callback_called == FALSE);

	/* Test 5: clear the credential store and try again in reverse order */

	flush_credentials_func();

	authn_callback_called = FALSE;
	result = gnome_vfs_open (&handle, authn_uri_child, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_OK);
	gnome_vfs_close (handle);
	handle = NULL;
	g_assert (authn_callback_called == TRUE);

	/* Test 6: Try something higher in the namespace, which should
	 * cause the callback to happen again
	 */

	authn_callback_called = FALSE;
	result = gnome_vfs_open (&handle, authn_uri, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_OK);
	gnome_vfs_close (handle);
	handle = NULL;
	g_assert (authn_callback_called == TRUE);

	/* Test 7: Try same URL as in test 4, make sure callback doesn't get called */

	authn_callback_called = FALSE;
	result = gnome_vfs_open (&handle, authn_uri_child, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_OK);
	gnome_vfs_close (handle);
	handle = NULL;
	g_assert (authn_callback_called == FALSE);

	/* Test 8: clear the credential store ensure that passing a username as NULL
	 * cancels the operation, resulting in a ACCESS_DENIED error */

	flush_credentials_func();

	authn_username = NULL;

	authn_callback_called = FALSE;
	result = gnome_vfs_open (&handle, authn_uri_child, GNOME_VFS_OPEN_READ);
	g_assert (result == GNOME_VFS_ERROR_ACCESS_DENIED);
	handle = NULL;
	g_assert (authn_callback_called == TRUE);


	/* Test 9: exercise the "destroy notify" functionality of the app context */
	/* Note that job doesn't end until a "close" is called, so the inherited
	 * app context isn't released until then
	 */

	flush_credentials_func();
	authn_username = "foo";

	app_context = gnome_vfs_app_context_new ();

	gnome_vfs_app_context_set_callback_full (
		app_context, GNOME_VFS_HOOKNAME_BASIC_AUTH, authn_callback, 
		NULL, FALSE, app_context_destroy_notify);

	gnome_vfs_app_context_push_takesref (app_context);

	authn_callback_called = FALSE;
	
	open_callback_occurred = FALSE;
	open_callback_result_expected = GNOME_VFS_OK;

	destroy_notify_occurred = FALSE;

	gnome_vfs_async_open (
		&async_handle, 
		authn_uri,
		GNOME_VFS_OPEN_READ,
		open_callback,
		NULL);

	gnome_vfs_app_context_pop ();

	g_assert (!destroy_notify_occurred);

	while (!open_callback_occurred) {
		g_main_context_iteration (NULL, TRUE);
	}

	close_callback_occurred = FALSE;
	gnome_vfs_async_close (async_handle, close_callback, NULL);

	while (!close_callback_occurred) {
		g_main_context_iteration (NULL, TRUE);
	}

	for (i = 0 ; i<100 ; i++) {
		g_main_context_iteration (NULL, FALSE);
		usleep (10);
	}

	g_assert (authn_callback_called);
	g_assert (destroy_notify_occurred);

	gnome_vfs_shutdown ();

	return 0;
}
