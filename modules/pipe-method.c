#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include <gnome.h>
#include "gnome-vfs-mime.h"
#include "gnome-vfs-module.h"
#include "gnome-vfs-module-shared.h"
#include "pipe-method.h"

struct _FileHandle {
	GnomeVFSURI *uri;
	FILE *fh;
};
typedef struct _FileHandle FileHandle;

static FileHandle *
file_handle_new (GnomeVFSURI *uri,
		 FILE *fh)
{
	FileHandle *new;

	new = g_new (FileHandle, 1);

	new->uri = gnome_vfs_uri_ref (uri);
	new->fh = fh;

	return new;
}

static void
file_handle_destroy (FileHandle *handle)
{
	pclose(handle->fh);
	gnome_vfs_uri_unref (handle->uri);
	g_free (handle);
}

static void
set_default_file_info (GnomeVFSFileInfo *file_info,
		       GnomeVFSURI *uri)
{
        file_info->name = g_strdup (uri->text);
	file_info->flags = GNOME_VFS_FILE_FLAGS_NONE;
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->permissions = (GNOME_VFS_PERM_USER_READ
				  | GNOME_VFS_PERM_GROUP_READ
				  | GNOME_VFS_PERM_OTHER_READ);

	file_info->valid_fields = (GNOME_VFS_FILE_INFO_FIELDS_FLAGS
				   | GNOME_VFS_FILE_INFO_FIELDS_TYPE 
				   | GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS);
}

static GnomeVFSResult
do_open (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	FILE *fh;

	_GNOME_VFS_METHOD_PARAM_CHECK (method_handle != NULL);
	_GNOME_VFS_METHOD_PARAM_CHECK (uri != NULL);

	if (!(mode & GNOME_VFS_OPEN_READ))
		return GNOME_VFS_ERROR_INVALIDOPENMODE;

	fh = popen(uri->text, "r");

	if (!fh)
		return gnome_vfs_result_from_errno ();

	file_handle = file_handle_new (uri, fh);
	
	*method_handle = (GnomeVFSMethodHandle *) file_handle;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_close (GnomeVFSMethod *method,
	  GnomeVFSMethodHandle *method_handle,
	  GnomeVFSContext *context)
{
	FileHandle *file_handle;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	/* FIXME bugzilla.eazel.com 1134: Should do this even after a failure?  */
	file_handle_destroy (file_handle);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read (GnomeVFSMethod *method,
	 GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read,
	 GnomeVFSContext *context)
{
	FileHandle *file_handle;
	gint read_val;

	g_return_val_if_fail (method_handle != NULL, GNOME_VFS_ERROR_INTERNAL);

	file_handle = (FileHandle *) method_handle;

	read_val = fread (buffer, 1, num_bytes, file_handle->fh);

	if (read_val <= 0) {
		*bytes_read = 0;
		return GNOME_VFS_ERROR_EOF;
	} else {
		*bytes_read = read_val;
		return GNOME_VFS_OK;
	}
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
        set_default_file_info (file_info, uri);

        return GNOME_VFS_OK;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod *method,
			      GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys,
			      GnomeVFSContext *context)
{
        FileHandle *handle;

	handle = (FileHandle *) method_handle;

        set_default_file_info (file_info, handle->uri);

	return GNOME_VFS_OK;
}

static gboolean
is_local(GnomeVFSMethod *method,
	 const GnomeVFSURI *uri)
{
	return TRUE;
}

static GnomeVFSMethod method = {
	do_open, /* open */
	NULL, /* create */
	do_close, /* close */
	do_read, /* read */
	NULL, /* write */
	NULL, /* seek */
	NULL, /* tell */
	NULL, /* truncate_handle */
	NULL, /* open_directory */
	NULL, /* close_directory */
	NULL, /* read_directory */
	(gpointer) do_get_file_info, /* get_file_info */
	(gpointer) do_get_file_info_from_handle, /* get_file_info_from_handle */
	is_local, /* is_local */
	NULL, /* make_directory */
	NULL, /* remove_directory */
	NULL, /* move */
	NULL, /* unlink */
	NULL, /* check_same_fs */
	NULL, /* set_file_info */
	NULL, /* truncate */
	NULL /* find_directory */
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
