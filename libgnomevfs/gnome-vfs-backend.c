#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gnome-vfs.h"
#include "gnome-vfs-backend.h"
#include <gmodule.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static GModule *gmod = NULL;
static gboolean (* gnome_vfs_backend_module_init)(gboolean deps_init);

static char backend_lower[128] = "";

const char *
gnome_vfs_backend_name(void)
{
	return (*backend_lower)?backend_lower:NULL;
}

void
gnome_vfs_backend_loadinit (gpointer app, gpointer modinfo)
{
	char *backend;
	char backend_filename[256];
	gboolean retval;

	/* Decide which backend module to load, based on
	   (a) environment variable
	   (b) default
	*/
	if (gmod)
		return;

	backend = getenv("GNOME_VFS_BACKEND");
	if (!backend)
		backend = GNOME_VFS_DEFAULT_BACKEND;

	strcpy(backend_lower, backend);
	g_strdown(backend_lower);

	g_snprintf(backend_filename, sizeof(backend_filename), "libgnomevfs-%s.so.0", backend_lower);

	gmod = g_module_open(backend_filename, G_MODULE_BIND_LAZY);
	if (!gmod)
	{
		g_error("Could not open %s: %s", backend_filename, g_module_error());
	}
	g_snprintf(backend_filename, sizeof(backend_filename), "gnome_vfs_%s_init", backend_lower);
	retval = g_module_symbol(gmod, backend_filename, (gpointer *)&gnome_vfs_backend_module_init);
	if (!retval)
	{
		g_module_close(gmod); gmod = NULL;
		g_error("Could not locate module initialization function: %s", g_module_error());
	}
}

gboolean
gnome_vfs_backend_init (gboolean deps_init)
{
	g_assert(gmod);
	g_assert(gnome_vfs_backend_init);

	gnome_vfs_backend_module_init(deps_init);

	return TRUE;
}

/* Yes, this is correct syntax for a function that returns a function pointer. 'man signal' for another example. */
static GnomeVFSResult
(*func_lookup(const char *func_name))()
{
	char cbuf[256];
	GnomeVFSResult (*retval)();

	g_snprintf(cbuf, sizeof(cbuf), "%s_%s", backend_lower, func_name);
	if (!g_module_symbol(gmod, cbuf, (gpointer *)&retval))
		retval = NULL;

	return retval;
}

#define GET_FUNC_PTR(func) if (!real_##func) { real_##func = func_lookup(#func); \
if (!real_##func) return GNOME_VFS_ERROR_INTERNAL; } \
return real_##func

GnomeVFSResult	 
gnome_vfs_async_open (GnomeVFSAsyncHandle **handle_return,
		      const gchar *text_uri,
		      GnomeVFSOpenMode open_mode,
		      GnomeVFSAsyncOpenCallback callback,
		      gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_open) (GnomeVFSAsyncHandle **handle_return,
					      const gchar *text_uri,
					      GnomeVFSOpenMode open_mode,
					      GnomeVFSAsyncOpenCallback callback,
					      gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_open) (handle_return, text_uri, open_mode, callback, callback_data);
}

GnomeVFSResult	 
gnome_vfs_async_open_uri (GnomeVFSAsyncHandle **handle_return,
			  GnomeVFSURI *uri,
			  GnomeVFSOpenMode open_mode,
			  GnomeVFSAsyncOpenCallback callback,
			  gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_open_uri) (GnomeVFSAsyncHandle **handle_return,
						  GnomeVFSURI *uri,
						  GnomeVFSOpenMode open_mode,
						  GnomeVFSAsyncOpenCallback callback,
						  gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_open_uri) (handle_return, uri, open_mode, callback, callback_data);
}

GnomeVFSResult
gnome_vfs_async_open_as_channel (GnomeVFSAsyncHandle **handle_return,
				 const gchar *text_uri,
				 GnomeVFSOpenMode open_mode,
				 guint advised_block_size,
				 GnomeVFSAsyncOpenAsChannelCallback callback,
				 gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_open_as_channel) (GnomeVFSAsyncHandle **handle_return,
							 const gchar *text_uri,
							 GnomeVFSOpenMode open_mode,
							 guint advised_block_size,
							 GnomeVFSAsyncOpenAsChannelCallback callback,
							 gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_open_as_channel) (handle_return, text_uri, open_mode, advised_block_size,
						       callback, callback_data);
}

GnomeVFSResult	 
gnome_vfs_async_create (GnomeVFSAsyncHandle **handle_return,
			const gchar *text_uri,
			GnomeVFSOpenMode open_mode,
			gboolean exclusive,
			guint perm,
			GnomeVFSAsyncOpenCallback callback,
			gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_create) (GnomeVFSAsyncHandle **handle_return,
						const gchar *text_uri,
						GnomeVFSOpenMode open_mode,
						gboolean exclusive,
						guint perm,
						GnomeVFSAsyncOpenCallback callback,
						gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_create) (handle_return, text_uri, open_mode, exclusive, perm, callback, callback_data);
}

GnomeVFSResult
gnome_vfs_async_create_as_channel (GnomeVFSAsyncHandle **handle_return,
				   const gchar *text_uri,
				   GnomeVFSOpenMode open_mode,
				   gboolean exclusive,
				   guint perm,
				   GnomeVFSAsyncOpenAsChannelCallback callback,
				   gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_create_as_channel) (GnomeVFSAsyncHandle **handle_return,
							   const gchar *text_uri,
							   GnomeVFSOpenMode open_mode,
							   gboolean exclusive,
							   guint perm,
							   GnomeVFSAsyncOpenAsChannelCallback callback,
							   gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_create_as_channel) (handle_return, text_uri, open_mode, exclusive, perm,
							 callback, callback_data);
}

GnomeVFSResult	 
gnome_vfs_async_close (GnomeVFSAsyncHandle *handle,
		       GnomeVFSAsyncCloseCallback callback,
		       gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_close) (GnomeVFSAsyncHandle *handle,
					       GnomeVFSAsyncCloseCallback callback,
					       gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_close) (handle, callback, callback_data);
}

GnomeVFSResult	 
gnome_vfs_async_read (GnomeVFSAsyncHandle *handle,
		      gpointer buffer,
		      guint bytes,
		      GnomeVFSAsyncReadCallback callback,
		      gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_read) (GnomeVFSAsyncHandle *handle,
					      gpointer buffer,
					      guint bytes,
					      GnomeVFSAsyncReadCallback callback,
					      gpointer callback_data) = NULL;
	GET_FUNC_PTR(gnome_vfs_async_read) (handle, buffer, bytes, callback, callback_data);
}

GnomeVFSResult	 
gnome_vfs_async_write (GnomeVFSAsyncHandle *handle,
		       gconstpointer buffer,
		       guint bytes,
		       GnomeVFSAsyncWriteCallback callback,
		       gpointer callback_data)
{
	static GnomeVFSResult	 
		(*real_gnome_vfs_async_write) (GnomeVFSAsyncHandle *handle,
					       gconstpointer buffer,
					       guint bytes,
					       GnomeVFSAsyncWriteCallback callback,
					       gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_write) (handle, buffer, bytes, callback, callback_data);
}

GnomeVFSResult   gnome_vfs_async_get_file_info  (GnomeVFSAsyncHandle **handle_return,
						 const char *text_uri,
						 GnomeVFSFileInfoOptions options,
						 const char *meta_keys[],
						 GnomeVFSAsyncGetFileInfoCallback callback,
						 gpointer callback_data)
{
  static GnomeVFSResult	 
    (*real_gnome_vfs_async_get_file_info) (GnomeVFSAsyncHandle **handle_return,
					   const char *text_uri,
					   GnomeVFSFileInfoOptions options,
					   const char *meta_keys[],
					   GnomeVFSAsyncGetFileInfoCallback callback,
					   gpointer callback_data) = NULL;

  GET_FUNC_PTR(gnome_vfs_async_get_file_info) (handle_return, text_uri, options, meta_keys, callback, callback_data);
}

GnomeVFSResult
gnome_vfs_async_load_directory_uri (GnomeVFSAsyncHandle **handle_return,
				    GnomeVFSURI *uri,
				    GnomeVFSFileInfoOptions options,
				    const gchar *meta_keys[],
				    GnomeVFSDirectorySortRule sort_rules[],
				    gboolean reverse_order,
				    GnomeVFSDirectoryFilterType filter_type,
				    GnomeVFSDirectoryFilterOptions filter_options,
				    const gchar *filter_pattern,
				    guint items_per_notification,
				    GnomeVFSAsyncDirectoryLoadCallback callback,
				    gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_load_directory_uri) (GnomeVFSAsyncHandle **handle_return,
							    GnomeVFSURI *uri,
							    GnomeVFSFileInfoOptions options,
							    const gchar *meta_keys[],
							    GnomeVFSDirectorySortRule sort_rules[],
							    gboolean reverse_order,
							    GnomeVFSDirectoryFilterType filter_type,
							    GnomeVFSDirectoryFilterOptions filter_options,
							    const gchar *filter_pattern,
							    guint items_per_notification,
							    GnomeVFSAsyncDirectoryLoadCallback callback,
							    gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_load_directory_uri) (handle_return, uri, options, meta_keys, sort_rules,
							  reverse_order, filter_type, filter_options,
							  filter_pattern, items_per_notification, callback,
							  callback_data);
}

GnomeVFSResult
gnome_vfs_async_load_directory (GnomeVFSAsyncHandle **handle_return,
				const gchar *uri,
				GnomeVFSFileInfoOptions options,
				const gchar *meta_keys[],
				GnomeVFSDirectorySortRule sort_rules[],
				gboolean reverse_order,
				GnomeVFSDirectoryFilterType filter_type,
				GnomeVFSDirectoryFilterOptions filter_options,
				const gchar *filter_pattern,
				guint items_per_notification,
				GnomeVFSAsyncDirectoryLoadCallback callback,
				gpointer callback_data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_load_directory) (GnomeVFSAsyncHandle **handle_return,
							const gchar *uri,
							GnomeVFSFileInfoOptions options,
							const gchar *meta_keys[],
							GnomeVFSDirectorySortRule sort_rules[],
							gboolean reverse_order,
							GnomeVFSDirectoryFilterType filter_type,
							GnomeVFSDirectoryFilterOptions filter_options,
							const gchar *filter_pattern,
							guint items_per_notification,
							GnomeVFSAsyncDirectoryLoadCallback callback,
							gpointer callback_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_load_directory) (handle_return, uri, options, meta_keys, sort_rules, reverse_order,
						      filter_type, filter_options, filter_pattern, items_per_notification,
						      callback, callback_data);
}

GnomeVFSResult
gnome_vfs_async_xfer (GnomeVFSAsyncHandle **handle_return,
		      const gchar *source_dir,
		      const GList *source_name_list,
		      const gchar *target_dir,
		      const GList *target_name_list,
		      GnomeVFSXferOptions xfer_options,
		      GnomeVFSXferErrorMode error_mode,
		      GnomeVFSXferOverwriteMode overwrite_mode,
		      GnomeVFSAsyncXferProgressCallback progress_callback,
		      gpointer data)
{
	static GnomeVFSResult
		(*real_gnome_vfs_async_xfer) (GnomeVFSAsyncHandle **handle_return,
					      const gchar *source_dir,
					      const GList *source_name_list,
					      const gchar *target_dir,
					      const GList *target_name_list,
					      GnomeVFSXferOptions xfer_options,
					      GnomeVFSXferErrorMode error_mode,
					      GnomeVFSXferOverwriteMode overwrite_mode,
					      GnomeVFSAsyncXferProgressCallback progress_callback,
					      gpointer data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_xfer) (handle_return, source_dir, source_name_list, target_dir, target_name_list,
					    xfer_options, error_mode, overwrite_mode, progress_callback, data);
}

GnomeVFSResult
gnome_vfs_async_cancel (GnomeVFSAsyncHandle *handle)
{
	GnomeVFSResult
		(*real_gnome_vfs_async_cancel)(GnomeVFSAsyncHandle *handle) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_cancel)(handle);
}

guint
gnome_vfs_async_add_status_callback (GnomeVFSAsyncHandle *handle,
				     GnomeVFSStatusCallback callback,
				     gpointer user_data)
{
	static guint
		(*real_gnome_vfs_async_add_status_callback) (GnomeVFSAsyncHandle *handle,
							     GnomeVFSStatusCallback callback,
							     gpointer user_data) = NULL;

	GET_FUNC_PTR(gnome_vfs_async_add_status_callback) (handle, callback, user_data);
}

void
gnome_vfs_async_remove_status_callback (GnomeVFSAsyncHandle *handle,
					guint callback_id)
{
	static void
		(*real_gnome_vfs_async_remove_status_callback) (GnomeVFSAsyncHandle *handle,
								guint callback_id) = NULL;

	if (!real_gnome_vfs_async_remove_status_callback)
	{
		real_gnome_vfs_async_remove_status_callback = (void (*)())func_lookup("gnome_vfs_async_remove_status_callback");
		if (!real_gnome_vfs_async_remove_status_callback)
			return;
	}

	return real_gnome_vfs_async_remove_status_callback(handle, callback_id);
}
