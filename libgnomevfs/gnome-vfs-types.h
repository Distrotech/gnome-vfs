/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-types.h - Types used by the GNOME Virtual File System.

   Copyright (C) 1999 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Ettore Perazzoli <ettore@gnu.org>
*/

#ifndef _GNOME_VFS_TYPES_H
#define _GNOME_VFS_TYPES_H

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <glibconfig.h>


/* Basic enumerations.  */

/* IMPORTANT NOTICE: If you add error types here, please also add the
   corresponsing descriptions in `gnome-vfs-result.c'.  Moreover, *always* add
   new values at the end of the list, and *never* remove values.  */
enum _GnomeVFSResult {
	GNOME_VFS_OK,
	GNOME_VFS_ERROR_NOTFOUND,
	GNOME_VFS_ERROR_GENERIC,
	GNOME_VFS_ERROR_INTERNAL,
	GNOME_VFS_ERROR_BADPARAMS,
	GNOME_VFS_ERROR_NOTSUPPORTED,
	GNOME_VFS_ERROR_IO,
	GNOME_VFS_ERROR_CORRUPTEDDATA,
	GNOME_VFS_ERROR_WRONGFORMAT,
	GNOME_VFS_ERROR_BADFILE,
	GNOME_VFS_ERROR_TOOBIG,
	GNOME_VFS_ERROR_NOSPACE,
	GNOME_VFS_ERROR_READONLY,
	GNOME_VFS_ERROR_INVALIDURI,
	GNOME_VFS_ERROR_NOTOPEN,
	GNOME_VFS_ERROR_INVALIDOPENMODE,
	GNOME_VFS_ERROR_ACCESSDENIED,
	GNOME_VFS_ERROR_TOOMANYOPENFILES,
	GNOME_VFS_ERROR_EOF,
	GNOME_VFS_ERROR_NOTADIRECTORY,
	GNOME_VFS_ERROR_INPROGRESS,
	GNOME_VFS_ERROR_INTERRUPTED,
	GNOME_VFS_ERROR_FILEEXISTS,
	GNOME_VFS_ERROR_LOOP,
	GNOME_VFS_ERROR_NOTPERMITTED,
	GNOME_VFS_ERROR_ISDIRECTORY,
	GNOME_VFS_ERROR_NOMEM,
	GNOME_VFS_ERROR_HOSTNOTFOUND,
	GNOME_VFS_ERROR_INVALIDHOSTNAME,
	GNOME_VFS_ERROR_HOSTHASNOADDRESS,
	GNOME_VFS_ERROR_LOGINFAILED,
	GNOME_VFS_ERROR_CANCELLED,
	GNOME_VFS_ERROR_DIRECTORYBUSY,
	GNOME_VFS_ERROR_DIRECTORYNOTEMPTY,
	GNOME_VFS_ERROR_TOOMANYLINKS,
	GNOME_VFS_ERROR_READONLYFS,
	GNOME_VFS_ERROR_NOTSAMEFS,
	GNOME_VFS_ERROR_NAMETOOLONG,
	GNOME_VFS_NUM_ERRORS
};
typedef enum _GnomeVFSResult GnomeVFSResult;

/* Open mode.  If you don't set `GNOME_VFS_OPEN_RANDOM', you have to access the
   file sequentially.  */
enum _GnomeVFSOpenMode {
	GNOME_VFS_OPEN_NONE = 0,
	GNOME_VFS_OPEN_READ = 1 << 0,
	GNOME_VFS_OPEN_WRITE = 1 << 1,
	GNOME_VFS_OPEN_RANDOM = 1 << 2
};
typedef enum _GnomeVFSOpenMode GnomeVFSOpenMode;

/* The file type.  */
enum _GnomeVFSFileType {
	GNOME_VFS_FILE_TYPE_UNKNOWN,
	GNOME_VFS_FILE_TYPE_REGULAR,
	GNOME_VFS_FILE_TYPE_DIRECTORY,
	GNOME_VFS_FILE_TYPE_FIFO,
	GNOME_VFS_FILE_TYPE_SOCKET,
	GNOME_VFS_FILE_TYPE_CHARDEVICE,
	GNOME_VFS_FILE_TYPE_BLOCKDEVICE,
	GNOME_VFS_FILE_TYPE_BROKENSYMLINK
};
typedef enum _GnomeVFSFileType GnomeVFSFileType;

/* File permissions.  These are the same as the Unix ones, but we wrap them
   into a nicer VFS-like enum.  */
enum _GnomeVFSFilePermissions {
	GNOME_VFS_PERM_USER_READ = S_IRUSR,
	GNOME_VFS_PERM_USER_WRITE = S_IWUSR,
	GNOME_VFS_PERM_USER_EXEC = S_IXUSR,
	GNOME_VFS_PERM_USER_ALL = S_IRUSR | S_IWUSR | S_IXUSR,
	GNOME_VFS_PERM_GROUP_READ = S_IRGRP,
	GNOME_VFS_PERM_GROUP_WRITE = S_IWGRP,
	GNOME_VFS_PERM_GROUP_EXEC = S_IXGRP,
	GNOME_VFS_PERM_GROUP_ALL = S_IRGRP | S_IWGRP | S_IXGRP,
	GNOME_VFS_PERM_OTHER_READ = S_IROTH,
	GNOME_VFS_PERM_OTHER_WRITE = S_IWOTH,
	GNOME_VFS_PERM_OTHER_EXEC = S_IXOTH,
	GNOME_VFS_PERM_OTHER_ALL = S_IROTH | S_IWOTH | S_IXOTH
};
typedef enum _GnomeVFSFilePermissions GnomeVFSFilePermissions;

/* This is used to specify the start position for seek operations.  */
enum _GnomeVFSSeekPosition {
	GNOME_VFS_SEEK_START,
	GNOME_VFS_SEEK_CURRENT,
	GNOME_VFS_SEEK_END
};
typedef enum _GnomeVFSSeekPosition GnomeVFSSeekPosition;


/* Basic types.  */

#ifdef G_HAVE_GINT64
typedef guint64 GnomeVFSFileSize;
typedef gint64  GnomeVFSFileOffset;
#else
typedef gulong GnomeVFSFileSize;
typedef glong  GnomeVFSFileOffset;
#endif

/* A file handle.  */
typedef struct _GnomeVFSHandle GnomeVFSHandle;

/* Structure describing an access method.  */
typedef struct _GnomeVFSMethod GnomeVFSMethod;

/* This describes a URI element.  */
struct _GnomeVFSURI {
	/* Reference count.  */
	guint ref_count;

	/* Text for the element: eg. some/path/name.  */
	gchar *text;

	/* Method string: eg. `gzip', `tar', `http'.  This is necessary as
	   one GnomeVFSMethod can be used for different method strings
	   (e.g. extfs handles zip, rar, zoo and several other ones).  */
	gchar *method_string;

	/* VFS method to access the element.  */
	GnomeVFSMethod *method;

	/* Pointer to the parent element, or NULL for toplevel elements.  */
	struct _GnomeVFSURI *parent;
};
typedef struct _GnomeVFSURI GnomeVFSURI;

/* This is the toplevel URI element.  A toplevel method implementations should
   cast the `GnomeVFSURI' argument to this type to get the additional host/auth
   information.  If any of the elements is 0, it is unspecified.  */
struct _GnomeVFSToplevelURI {
	/* Base object.  */
	GnomeVFSURI uri;

	/* Server location information.  */
	gchar *host_name;
	guint host_port;

	/* Authorization information.  */
	gchar *user_name;
	gchar *password;
};
typedef struct _GnomeVFSToplevelURI GnomeVFSToplevelURI;

/* This is used for hiding information when transforming the GnomeVFSURI into a
   string.  */
enum _GnomeVFSURIHideOptions {
	GNOME_VFS_URI_HIDE_NONE = 0,
	GNOME_VFS_URI_HIDE_USER_NAME = 1 << 0,
	GNOME_VFS_URI_HIDE_PASSWORD = 1 << 1,
	GNOME_VFS_URI_HIDE_HOST_NAME = 1 << 2,
	GNOME_VFS_URI_HIDE_HOST_PORT = 1 << 3,
	GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD = 1 << 4
};
typedef enum _GnomeVFSURIHideOptions GnomeVFSURIHideOptions;


/* File information, a la stat(2).  */
/* FIXME: Private stuff.  */

struct _GnomeVFSFileMetadata {
	gchar *key;
	gpointer *value;
	guint value_size;
};
typedef struct _GnomeVFSFileMetadata GnomeVFSFileMetadata;

/* File flags.  */
enum _GnomeVFSFileFlags {
	GNOME_VFS_FILE_FLAGS_NONE = 0,
	/* Whether the file is a symlink.  */
	GNOME_VFS_FILE_FLAGS_SYMLINK = 1 << 0,
	/* Whether the file is on a local file system.  */
	GNOME_VFS_FILE_FLAGS_LOCAL = 1 << 1,
	/* Whether the file has the SUID bit set.  */
	GNOME_VFS_FILE_FLAGS_SUID = 1 << 2,
	/* Whether the file has the SGID bit set.  */
	GNOME_VFS_FILE_FLAGS_SGID = 1 << 3,
	/* Whether the file has the sticky bit set.  */
	GNOME_VFS_FILE_FLAGS_STICKY = 1 << 4
};
typedef enum _GnomeVFSFileFlags GnomeVFSFileFlags;

/* Flags indicating what fields in a GnomeVFSFileInfo struct are valid. 

   Name is always assumed valid (how else would you have gotten a
   FileInfo struct otherwise?) and metadata_list is ignored, since
   it shouldn't be accessed directly anyway.
 */

enum _GnomeVFSFileInfoFields {
	GNOME_VFS_FILE_INFO_FIELDS_NONE = 0,
	GNOME_VFS_FILE_INFO_FIELDS_TYPE = 1 << 0,
	GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS = 1 << 1,
	GNOME_VFS_FILE_INFO_FIELDS_FLAGS = 1 << 2,
	GNOME_VFS_FILE_INFO_FIELDS_DEVICE = 1 << 3,
	GNOME_VFS_FILE_INFO_FIELDS_INODE = 1 << 4,
	GNOME_VFS_FILE_INFO_FIELDS_LINK_COUNT = 1 << 5,
	GNOME_VFS_FILE_INFO_FIELDS_SIZE = 1 << 6,
	GNOME_VFS_FILE_INFO_FIELDS_BLOCK_COUNT = 1 << 7,
	GNOME_VFS_FILE_INFO_FIELDS_IO_BLOCK_SIZE = 1 << 8,
	GNOME_VFS_FILE_INFO_FIELDS_ATIME = 1 << 9,
	GNOME_VFS_FILE_INFO_FIELDS_MTIME = 1 << 10,
	GNOME_VFS_FILE_INFO_FIELDS_CTIME = 1 << 11,
	GNOME_VFS_FILE_INFO_FIELDS_SYMLINK_NAME = 1 << 12,
	GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE = 1 << 13
};
typedef enum _GnomeVFSFileInfoFields GnomeVFSFileInfoFields;

struct _GnomeVFSFileInfo {
	/* Base name of the file (no path).  */
	gchar *name;

	/* Fields which are actually valid in this strcture. */
	GnomeVFSFileInfoFields valid_fields;

	/* File type (i.e. regular, directory, block device...).  */
	GnomeVFSFileType type;

	/* File permissions.  */
	GnomeVFSFilePermissions permissions;

	/* Flags for this file.  */
	GnomeVFSFileFlags flags;

	/* This is only valid if `is_local' is TRUE (see below).  */
	dev_t device;
	ino_t inode;

	/* Link count.  */
	guint link_count;

	/* UID, GID.  */
	guint uid;
	guint gid;

	/* Size in bytes.  */
	GnomeVFSFileSize size;

	/* Size measured in units of 512-byte blocks.  */
	GnomeVFSFileSize block_count;

	/* Optimal buffer size for reading/writing the file.  */
	guint io_block_size;

	/* Access, modification and change times.  */
	time_t atime;
	time_t mtime;
	time_t ctime;

	/* If the file is a symlink (see `flags'), this specifies the file the
           link points to.  */
	gchar *symlink_name;

	/* MIME type.  */
	gchar *mime_type;

	/* List of GnomeVFSFileMetadata elements, specifying the metadata for
           this file.  You should use the `gnome_vfs_file_info_get_metadata()'
           function to access the values.  Moreover, this does not contain all
           the metadata for the file, but rather only the data that has been
           requested in the call that returned this information.  */
	GList *metadata_list;

	guint refcount;
};
typedef struct _GnomeVFSFileInfo GnomeVFSFileInfo;

enum _GnomeVFSFileInfoOptions {
	GNOME_VFS_FILE_INFO_DEFAULT = 0, /* FIXME name sucks */
	GNOME_VFS_FILE_INFO_GETMIMETYPE = 1 << 0,
	GNOME_VFS_FILE_INFO_FASTMIMETYPE = 1 << 1,
	GNOME_VFS_FILE_INFO_FOLLOWLINKS = 1 << 2
};
typedef enum _GnomeVFSFileInfoOptions GnomeVFSFileInfoOptions;

enum _GnomeVFSSetFileInfoMask {
	GNOME_VFS_SET_FILE_INFO_NONE = 0,
	GNOME_VFS_SET_FILE_INFO_NAME = 1 << 0,
	GNOME_VFS_SET_FILE_INFO_PERMISSIONS = 1 << 1,
	GNOME_VFS_SET_FILE_INFO_OWNER = 1 << 2,
	GNOME_VFS_SET_FILE_INFO_TIME = 1 << 3
};
typedef enum _GnomeVFSSetFileInfoMask GnomeVFSSetFileInfoMask;


/* Directory stuff.  */

typedef struct _GnomeVFSDirectoryList GnomeVFSDirectoryList;
typedef gpointer GnomeVFSDirectoryListPosition;

#define GNOME_VFS_DIRECTORY_LIST_POSITION_NONE NULL

enum _GnomeVFSDirectorySortRule {
	GNOME_VFS_DIRECTORY_SORT_NONE,
	GNOME_VFS_DIRECTORY_SORT_DIRECTORYFIRST,
	GNOME_VFS_DIRECTORY_SORT_BYNAME,
	GNOME_VFS_DIRECTORY_SORT_BYNAME_IGNORECASE,
	GNOME_VFS_DIRECTORY_SORT_BYSIZE,
	GNOME_VFS_DIRECTORY_SORT_BYBLOCKCOUNT,
	GNOME_VFS_DIRECTORY_SORT_BYATIME,
	GNOME_VFS_DIRECTORY_SORT_BYMTIME,
	GNOME_VFS_DIRECTORY_SORT_BYCTIME,
	GNOME_VFS_DIRECTORY_SORT_BYMIMETYPE
};
typedef enum _GnomeVFSDirectorySortRule GnomeVFSDirectorySortRule;

enum _GnomeVFSDirectoryFilterType {
	GNOME_VFS_DIRECTORY_FILTER_NONE,
	GNOME_VFS_DIRECTORY_FILTER_SHELLPATTERN,
	GNOME_VFS_DIRECTORY_FILTER_REGEXP
};
typedef enum _GnomeVFSDirectoryFilterType GnomeVFSDirectoryFilterType;

enum _GnomeVFSDirectoryFilterOptions {
	GNOME_VFS_DIRECTORY_FILTER_DEFAULT = 0,
	GNOME_VFS_DIRECTORY_FILTER_NODIRS = 1 << 0,
	GNOME_VFS_DIRECTORY_FILTER_DIRSONLY = 1 << 1,
	GNOME_VFS_DIRECTORY_FILTER_NODOTFILES = 1 << 2,
	GNOME_VFS_DIRECTORY_FILTER_IGNORECASE = 1 << 3,
	GNOME_VFS_DIRECTORY_FILTER_EXTENDEDREGEXP =  1 << 4,
	GNOME_VFS_DIRECTORY_FILTER_NOSELFDIR = 1 << 5,
	GNOME_VFS_DIRECTORY_FILTER_NOPARENTDIR = 1 << 6
};
typedef enum _GnomeVFSDirectoryFilterOptions GnomeVFSDirectoryFilterOptions;

enum _GnomeVFSDirectoryFilterNeeds {
	GNOME_VFS_DIRECTORY_FILTER_NEEDS_NOTHING = 0,
	GNOME_VFS_DIRECTORY_FILTER_NEEDS_NAME = 1 << 0,
	GNOME_VFS_DIRECTORY_FILTER_NEEDS_TYPE = 1 << 1,
	GNOME_VFS_DIRECTORY_FILTER_NEEDS_STAT = 1 << 2,
	GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE = 1 << 3,
	GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA = 1 << 4
};
typedef enum _GnomeVFSDirectoryFilterNeeds GnomeVFSDirectoryFilterNeeds;

enum _GnomeVFSDirectoryVisitOptions {
	GNOME_VFS_DIRECTORY_VISIT_DEFAULT = 0,
	GNOME_VFS_DIRECTORY_VISIT_SAMEFS = 1 << 0,
	GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK = 1 << 1
};
typedef enum _GnomeVFSDirectoryVisitOptions GnomeVFSDirectoryVisitOptions;

typedef struct _GnomeVFSDirectoryFilter GnomeVFSDirectoryFilter;

typedef gboolean (* GnomeVFSDirectoryFilterFunc) (const GnomeVFSFileInfo *info,
						  gpointer data);
typedef gint     (* GnomeVFSDirectorySortFunc)   (const GnomeVFSFileInfo *a,
						  const GnomeVFSFileInfo *b,
						  gpointer data);
typedef gboolean (* GnomeVFSDirectoryVisitFunc)	 (const gchar *rel_path,
						  GnomeVFSFileInfo *info,
						  gboolean recursing_will_loop,
						  gpointer data,
						  gboolean *recurse);


/* Xfer options.  */
enum _GnomeVFSXferOptions {
	GNOME_VFS_XFER_DEFAULT = 0,
	GNOME_VFS_XFER_PRESERVE = 1 << 0,
	GNOME_VFS_XFER_FOLLOWLINKS = 1 << 1,
	GNOME_VFS_XFER_WITHPARENTS = 1 << 2,
	GNOME_VFS_XFER_RECURSIVE = 1 << 3,
	GNOME_VFS_XFER_SAMEFS = 1 << 4,
	GNOME_VFS_XFER_SPARSE_ALWAYS = 1 << 5,
	GNOME_VFS_XFER_SPARSE_NEVER = 1 << 6,
	GNOME_VFS_XFER_UPDATEMODE = 1 << 7,
	GNOME_VFS_XFER_REMOVESOURCE = 1 << 8
};
typedef enum _GnomeVFSXferOptions GnomeVFSXferOptions;

/* Progress status, to be reported to the caller of the transfer operation.  */
enum _GnomeVFSXferProgressStatus {
	GNOME_VFS_XFER_PROGRESS_STATUS_OK = 0,
	GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR = 1,
	GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE = 2
};
typedef enum _GnomeVFSXferProgressStatus GnomeVFSXferProgressStatus;

/* The different ways to deal with overwriting during a transfer operation.  */
enum _GnomeVFSXferOverwriteMode {
	/* Interrupt transferring with an error (GNOME_VFS_ERROR_FILEEXISTS).  */
	GNOME_VFS_XFER_OVERWRITE_MODE_ABORT = 0,
	/* Invoke the progress callback with a
	   `GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE' status code. */
	GNOME_VFS_XFER_OVERWRITE_MODE_QUERY = 1,
	/* Overwrite files silently.  */
	GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE = 2,
	/* Ignore files silently.  */
	GNOME_VFS_XFER_OVERWRITE_MODE_SKIP = 3
};
typedef enum _GnomeVFSXferOverwriteMode GnomeVFSXferOverwriteMode;

/* This defines the actions to perform before a file is being overwritten
   (i.e., these are the answers that can be given to a replace query).  */
enum _GnomeVFSXferOverwriteAction {
	GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT = 0,
	GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE = 1,
	GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE_ALL = 2,
	GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP = 3,
	GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP_ALL = 4,
};
typedef enum _GnomeVFSXferOverwriteAction GnomeVFSXferOverwriteAction;

enum _GnomeVFSXferErrorMode {
	/* Interrupt transferring with an error (code returned is code of the
           operation that has caused the error).  */
	GNOME_VFS_XFER_ERROR_MODE_ABORT = 0,
	/* Invoke the progress callback with a
	   `GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR' status code. */
	GNOME_VFS_XFER_ERROR_MODE_QUERY = 1,
};
typedef enum _GnomeVFSXferErrorMode GnomeVFSXferErrorMode;

/* This defines the possible actions to be performed after an error has
   occurred.  */
enum _GnomeVFSXferErrorAction {
	/* Interrupt operation and return `GNOME_VFS_ERROR_INTERRUPTED'.  */
	GNOME_VFS_XFER_ERROR_ACTION_ABORT = 0,
	/* Try the same operation again.  */
	GNOME_VFS_XFER_ERROR_ACTION_RETRY = 1,
	/* Skip this file and continue normally.  */
	GNOME_VFS_XFER_ERROR_ACTION_SKIP = 2
};
typedef enum _GnomeVFSXferErrorAction GnomeVFSXferErrorAction;

/* This specifies the current phase in the transfer operation.  Phases whose
   comments are marked with `(*)' are always reported in "normal" (i.e. no
   error) condition; the other ones are only reported if an error happens in
   that specific phase.  */
enum _GnomeVFSXferPhase {
	/* Unknown phase */
	GNOME_VFS_XFER_PHASE_UNKNOWN,
	/* Collecting file list */
	GNOME_VFS_XFER_PHASE_COLLECTING,
	/* File list collected (*) */
	GNOME_VFS_XFER_PHASE_READYTOGO,
	/* Opening source file for reading */
	GNOME_VFS_XFER_PHASE_OPENSOURCE,
	/* Creating target file */
	GNOME_VFS_XFER_PHASE_OPENTARGET,
	/* Ready to transfer (more) data from source to target (*) */
	GNOME_VFS_XFER_PHASE_XFERRING,
	/* Reading data from source file */
	GNOME_VFS_XFER_PHASE_READSOURCE,
	/* Writing data to target file */
	GNOME_VFS_XFER_PHASE_WRITETARGET,
	/* Closing source file */
	GNOME_VFS_XFER_PHASE_CLOSESOURCE,
	/* Closing target file */
	GNOME_VFS_XFER_PHASE_CLOSETARGET,
	/* Deletin source file */
	GNOME_VFS_XFER_PHASE_DELETESOURCE,
	/* Setting attributes on target file */
	GNOME_VFS_XFER_PHASE_SETATTRIBUTES,
	/* Go to the next file (*) */
	GNOME_VFS_XFER_PHASE_FILECOMPLETED,
	/* Operation finished (*) */
	GNOME_VFS_XFER_PHASE_COMPLETED,
	GNOME_VFS_XFER_NUM_PHASES
};
typedef enum _GnomeVFSXferPhase GnomeVFSXferPhase;

/* Progress information for the transfer operation.  This is especially useful
   for interactive programs.  */
struct _GnomeVFSXferProgressInfo {
	/* Progress status (see above for a description).  */
	GnomeVFSXferProgressStatus status;

	/* VFS status code.  If `status' is
           `GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR', you should look at this
           member to know what has happened.  */
	GnomeVFSResult vfs_status;

	/* Current phase in the transferring process.  */
	GnomeVFSXferPhase phase;

	/* Source URI.  FIXME name?  */
	gchar *source_name;

	/* Destination URI.  FIXME name?  */
	gchar *target_name;

	/* Index of file being copied. */
	gulong file_index;

	/* Total number of files to be copied.  */
	gulong files_total;

	/* Total number of bytes to be copied.  */
	GnomeVFSFileSize bytes_total;

	/* Total size of this file (in bytes).  */
	GnomeVFSFileSize file_size;

	/* Bytes copied for this file so far.  */
	GnomeVFSFileSize bytes_copied;

	/* Total amount of data copied from the beginning.  */
	GnomeVFSFileSize total_bytes_copied;
};
typedef struct _GnomeVFSXferProgressInfo GnomeVFSXferProgressInfo;

/* This is the prototype for functions called during a transfer operation to
   report progress.  If the return value is `FALSE' (0), the operation is
   interrupted immediately: the transfer function returns with the value of
   `vfs_status' if it is different from `GNOME_VFS_OK', or with
   `GNOME_VFS_ERROR_INTERRUPTED' otherwise.  The effect of other values depend
   on the value of `info->status':

   - If the status is `GNOME_VFS_XFER_PROGRESS_STATUS_OK', the transfer
     operation is resumed normally.

   - If the status is `GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR', the return
     value is interpreted as a `GnomeVFSXferErrorAction' and operation is
     interrupted, continued or retried accordingly.

   - If the status is `GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE', the return
     value is interpreted as a `GnomeVFSXferOverwriteAction'.  */

typedef gint (* GnomeVFSXferProgressCallback) 	(const GnomeVFSXferProgressInfo *info,
						 gpointer data);


/* Types for asynchronous operations.  */

typedef struct _GnomeVFSAsyncHandle GnomeVFSAsyncHandle;

typedef void	(* GnomeVFSAsyncOpenCallback)	(GnomeVFSAsyncHandle *handle,
						 GnomeVFSResult result,
						 gpointer callback_data);

typedef GnomeVFSAsyncOpenCallback GnomeVFSAsyncCreateCallback;

typedef void	(* GnomeVFSAsyncOpenAsChannelCallback)
						(GnomeVFSAsyncHandle *handle,
						 GIOChannel *channel,
						 GnomeVFSResult result,
						 gpointer callback_data);

typedef GnomeVFSAsyncOpenAsChannelCallback GnomeVFSAsyncCreateAsChannelCallback;

typedef void	(* GnomeVFSAsyncCloseCallback)	(GnomeVFSAsyncHandle *handle,
						 GnomeVFSResult result,
						 gpointer callback_data);

typedef void	(* GnomeVFSAsyncReadCallback)	(GnomeVFSAsyncHandle *handle,
						 GnomeVFSResult result,
						 gpointer buffer,
						 GnomeVFSFileSize bytes_requested,
						 GnomeVFSFileSize bytes_read,
						 gpointer callback_data);

typedef void	(* GnomeVFSAsyncWriteCallback)	(GnomeVFSAsyncHandle *handle,
						 GnomeVFSResult result,
						 gconstpointer buffer,
						 GnomeVFSFileSize bytes_requested,
						 GnomeVFSFileSize bytes_written,
						 gpointer callback_data);

typedef void    (* GnomeVFSAsyncGetFileInfoCallback)
                                                 (GnomeVFSAsyncHandle *handle,
						  GnomeVFSResult result,
						  GnomeVFSFileInfo *file_info,
						  gpointer callback_data);

typedef void	(* GnomeVFSAsyncDirectoryLoadCallback)
						(GnomeVFSAsyncHandle *handle,
						 GnomeVFSResult result,
						 GnomeVFSDirectoryList *list,
						 guint entries_read,
						 gpointer callback_data);

typedef gint    (* GnomeVFSAsyncXferProgressCallback)
						(GnomeVFSAsyncHandle *handle,
						 const GnomeVFSXferProgressInfo *info,
						 gpointer data);



/* Used to report user-friendly status messages you might want to display. */
typedef void    (* GnomeVFSStatusCallback)      (const gchar *message,
						 gpointer     callback_data);

#endif /* _GNOME_VFS_TYPES_H */
