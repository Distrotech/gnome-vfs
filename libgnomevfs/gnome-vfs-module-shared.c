#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _LARGEFILE64_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gnome.h>

#include "gnome-vfs-module.h"
#include "gnome-vfs-module-shared.h"

const gchar *
gnome_vfs_mime_type_from_mode (mode_t mode)
{
	const gchar *mime_type;

	/* FIXME bugzilla.eazel.com 1171:
	 * The following returned values are not valid MIME types.
	 * Should the returned types be "application/x-directory" 
	 * or "x-special/directory"?
	 */
	if (S_ISREG (mode))
		mime_type = NULL;
	else if (S_ISDIR (mode))
		mime_type = "x-special/directory";
	else if (S_ISCHR (mode))
		mime_type = "x-special/device-char";
	else if (S_ISBLK (mode))
		mime_type = "x-special/device-block";
	else if (S_ISFIFO (mode))
		mime_type = "x-special/fifo";
	else if (S_ISLNK (mode))
		mime_type = "x-special/symlink";
	else if (S_ISSOCK (mode))
		mime_type = "x-special/socket";
	else
		mime_type = NULL;

	return mime_type;
}

const char *
gnome_vfs_get_special_mime_type (GnomeVFSURI *uri)
{
	GnomeVFSResult error;
	GnomeVFSFileInfo info;

	/* Get file info and examine the type field to see if file is 
	 * one of the special kinds. 
	 */
	error = gnome_vfs_get_file_info_uri (uri, &info, GNOME_VFS_FILE_INFO_DEFAULT, NULL);
	if (error != GNOME_VFS_OK) {
		return NULL;
	}

	/* FIXME bugzilla.eazel.com 1171:
	 * The following returned values are not valid MIME types.
	 * Should the returned types be "application/x-directory" 
	 * or "x-special/directory"?
	 */
	switch (info.type) {
	case GNOME_VFS_FILE_TYPE_DIRECTORY:
		return "x-special/directory";
	case GNOME_VFS_FILE_TYPE_CHARACTER_DEVICE:
		return "x-special/device-char";
	case GNOME_VFS_FILE_TYPE_BLOCK_DEVICE:
		return "x-special/device-block";
	case GNOME_VFS_FILE_TYPE_FIFO:
		return "x-special/fifo";
	case GNOME_VFS_FILE_TYPE_SOCKET:
		return "x-special/socket";
	default:
		break;
	}

	return NULL;	
}

void
gnome_vfs_stat_to_file_info (GnomeVFSFileInfo *file_info,
			     const struct stat *statptr)
{
	if (S_ISDIR (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
	else if (S_ISCHR (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_CHARACTER_DEVICE;
	else if (S_ISBLK (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_BLOCK_DEVICE;
	else if (S_ISFIFO (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_FIFO;
	else if (S_ISSOCK (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_SOCKET;
	else if (S_ISREG (statptr->st_mode))
		file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	else
		file_info->type = GNOME_VFS_FILE_TYPE_UNKNOWN;

	file_info->permissions
		= statptr->st_mode & (S_IRUSR | S_IWUSR | S_IXUSR
				      | S_IRGRP | S_IWGRP | S_IXGRP
				      | S_IROTH | S_IWOTH | S_IXOTH
				      | S_ISUID | S_ISGID);

#ifdef S_ISVTX
	GNOME_VFS_FILE_INFO_SET_STICKY (file_info,
					(statptr->st_mode & S_ISVTX) ? TRUE
					                             : FALSE);
#else
	GNOME_VFS_FILE_INFO_SET_STICKY (file_info, FALSE);
#endif

	file_info->device = statptr->st_dev;
	file_info->inode = statptr->st_ino;

	file_info->link_count = statptr->st_nlink;

	file_info->uid = statptr->st_uid;
	file_info->gid = statptr->st_gid;

	file_info->size = statptr->st_size;
	file_info->block_count = statptr->st_blocks;
	file_info->io_block_size = statptr->st_blksize;

	file_info->atime = statptr->st_atime;
	file_info->ctime = statptr->st_ctime;
	file_info->mtime = statptr->st_mtime;

	file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE |
	  GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS | GNOME_VFS_FILE_INFO_FIELDS_FLAGS |
	  GNOME_VFS_FILE_INFO_FIELDS_DEVICE | GNOME_VFS_FILE_INFO_FIELDS_INODE |
	  GNOME_VFS_FILE_INFO_FIELDS_LINK_COUNT | GNOME_VFS_FILE_INFO_FIELDS_SIZE |
	  GNOME_VFS_FILE_INFO_FIELDS_BLOCK_COUNT | GNOME_VFS_FILE_INFO_FIELDS_IO_BLOCK_SIZE |
	  GNOME_VFS_FILE_INFO_FIELDS_ATIME | GNOME_VFS_FILE_INFO_FIELDS_MTIME |
	  GNOME_VFS_FILE_INFO_FIELDS_CTIME;
}


/* FIXME bugzilla.eazel.com 1120:
 * Thread safety.  */
GnomeVFSResult
gnome_vfs_set_meta (GnomeVFSFileInfo *info,
		    const gchar *file_name,
		    const gchar *meta_key)
{
	guint size;
	gchar *buffer;
	gint meta_result;

	g_return_val_if_fail (file_name != NULL, GNOME_VFS_ERROR_INTERNAL);
	g_return_val_if_fail (meta_key != NULL, GNOME_VFS_ERROR_INTERNAL);
	g_return_val_if_fail (info != NULL, GNOME_VFS_ERROR_INTERNAL);

	/* We use `metadata_get_fast' because we get the MIME type
           another way.  */
	meta_result = gnome_metadata_get_fast (file_name, meta_key, &
					       size, &buffer);

	if (meta_result == GNOME_METADATA_OK)
		gnome_vfs_file_info_set_metadata (info, meta_key,
						  buffer, size);
	else
		gnome_vfs_file_info_unset_metadata (info, meta_key);

	return GNOME_VFS_OK;
}

GnomeVFSResult
gnome_vfs_set_meta_for_list (GnomeVFSFileInfo *info,
			     const gchar *file_name,
			     const GList *meta_keys)
{
	GnomeVFSResult result;
	const GList *p;

	if (meta_keys == NULL)
		return GNOME_VFS_OK;

	for (p = meta_keys; p != NULL; p = p->next) {
		result = gnome_vfs_set_meta (info, file_name, p->data);
		if (result != GNOME_VFS_OK)
			break;
	}

	return result;
}

