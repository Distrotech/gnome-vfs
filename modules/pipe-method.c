#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include <gnome.h>
#include "gnome-vfs-module.h"
#include "module-shared.h"
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

	/* FIXME: Should do this even after a failure?  */
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

	if (read_val == EOF) {
		*bytes_read = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_read = read_val;
		return GNOME_VFS_OK;
	}
}

#if 0
static void
set_mime_type (GnomeVFSFileInfo *info,
	       const gchar *full_name,
	       GnomeVFSFileInfoOptions options,
	       struct stat *statbuf)
{
	const gchar *mime_type;

	if (options & GNOME_VFS_FILE_INFO_FASTMIMETYPE) {
		const gchar *mime_name;

		if ((options & GNOME_VFS_FILE_INFO_FOLLOWLINKS)
		    && info->type != GNOME_VFS_FILE_TYPE_BROKENSYMLINK
		    && info->symlink_name != NULL)
			mime_name = info->symlink_name;
		else
			mime_name = full_name;

		mime_type = gnome_mime_type_or_default (mime_name, NULL);

		if (mime_type == NULL)
			mime_type = gnome_vfs_mime_type_from_mode (statbuf->st_mode);
	} else {
		/* FIXME: This will also stat the file for us...  Which is
                   not good at all, as we already have the stat info when we
                   get here, but there is no other way to do this with the
                   current gnome-libs.  */
		/* FIXME: We actually *always* follow symlinks here.  It
                   needs fixing.  */
		mime_type = gnome_mime_type_from_magic (full_name);
	}

	info->mime_type = g_strdup (mime_type);
}
#endif

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod *method,
		  GnomeVFSURI *uri,
		  GnomeVFSFileInfo *file_info,
		  GnomeVFSFileInfoOptions options,
		  const GList *meta_keys,
		  GnomeVFSContext *context)
{
	file_info->name = g_strdup (uri->text);
	file_info->flags = GNOME_VFS_FILE_FLAGS_NONE;
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->permissions = GNOME_VFS_PERM_USER_READ|GNOME_VFS_PERM_GROUP_READ|GNOME_VFS_PERM_OTHER_READ;

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
	file_info->name = NULL;
	file_info->flags = GNOME_VFS_FILE_FLAGS_NONE;
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->permissions = GNOME_VFS_PERM_USER_READ|GNOME_VFS_PERM_GROUP_READ|GNOME_VFS_PERM_OTHER_READ;

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
  (gpointer)do_get_file_info, /* get_file_info */
  (gpointer)do_get_file_info_from_handle, /* get_file_info_from_handle */
  is_local, /* is_local */
  NULL, /* make_directory */
  NULL, /* remove_directory */
  NULL, /* move */
  NULL, /* unlink */
  NULL, /* check_same_fs */
  NULL, /* set_file_info */
  NULL /* truncate */
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
