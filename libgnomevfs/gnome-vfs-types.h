/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-types.h - Types used by the GNOME Virtual File System.

   Copyright (C) 1999, 2001 Free Software Foundation

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
           Seth Nickell <snickell@stanford.edu>
*/

#ifndef GNOME_VFS_TYPES_H
#define GNOME_VFS_TYPES_H

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <glib.h>

/* NOTE - this file is largely deprecated, types have been moved to their
   equivalent .h files, as per normal GNOME conventions. Types remain
   here only if they have no sensible home and/or if including them in specific
   .h files causes recursive header inclusion problems. */

/*
 * This defines GnomeVFSFileSize and GnomeVFSFileOffset
 *
 * It also defines GNOME_VFS_SIZE_IS_<type> and GNOME_VFS_OFFSET_IS_<type>
 * where type is INT, UNSIGNED_INT, LONG, UNSIGNED_LONG, LONG_LONG
 * or UNSIGNED_LONG_LONG.  Note that size is always unsigned and offset
 * is always signed.
 *
 * It also defines GNOME_VFS_SIZE_FORMAT_STR and GNOME_VFS_OFFSET_FORMAT_STR
 * which is the string representation to be used in printf style expressions.
 * This is without the %, so for example for long it would be "ld"
 */
#include <libgnomevfs/gnome-vfs-file-size.h>

/* see gnome-vfs-result.h for GnomeVFSResult */
#include <libgnomevfs/gnome-vfs-result.h>

/* Open mode.  If you don't set `GNOME_VFS_OPEN_RANDOM', you have to access the
   file sequentially.  */
typedef enum {
	GNOME_VFS_OPEN_NONE = 0,
	GNOME_VFS_OPEN_READ = 1 << 0,
	GNOME_VFS_OPEN_WRITE = 1 << 1,
	GNOME_VFS_OPEN_RANDOM = 1 << 2
} GnomeVFSOpenMode;

/* This is used to specify the start position for seek operations.  */
typedef enum {
	GNOME_VFS_SEEK_START,
	GNOME_VFS_SEEK_CURRENT,
	GNOME_VFS_SEEK_END
} GnomeVFSSeekPosition;

/* see gnome-vfs-file-info.h for GnomeVFSFileType */
/* see gnome-vfs-file-info.h for GnomeVFSFilePermissions */
/* see gnome-vfs-handle.h for GnomeVFSHandle */
/* see gnome-vfs-uri.h for GnomeVFSURI */
/* see gnome-vfs-uri.h for GnomeVFSToplevelURI */
/* see gnome-vfs-uri.h for GnomeVFSURIHideOptions */
/* see gnome-vfs-file-info.h for GnomeVFSFileFlags */
/* see gnome-vfs-file-info.h for GnomeVFSFileInfoFields */
/* see gnome-vfs-file-info.h for GnomeVFSFileInfo */
/* see gnome-vfs-file-info.h for GnomeVFSFileInfoOptions */
/* see gnome-vfs-file-info.h for GnomeVFSFileInfoMask */
/* see gnome-vfs-find-directory.h for GnomeVFSFindDirectoryKind */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryFilterType */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryFilterOptions */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryFilterNeeds */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryVisitOptions */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryFilter */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryFilterFunc */
/* see gnome-vfs-directory-filter.h for GnomeVFSDirectoryVisitFunc */
/* see gnome-vfs-xfer.h for GnomeVFSXferOptions */
/* see gnome-vfs-xfer.h for GnomeVFSXferProgressStatus */
/* see gnome-vfs-xfer.h for GnomeVFSXferOverwriteMode */
/* see gnome-vfs-xfer.h for GnomeVFSXferOverwriteAction */
/* see gnome-vfs-xfer.h for GnomeVFSXferErrorMode */
/* see gnome-vfs-xfer.h for GnomeVFSXferErrorAction */
/* see gnome-vfs-xfer.h for GnomeVFSXferPhase */
/* see gnome-vfs-xfer.h for GnomeVFSXferProgressInfo */
/* see gnome-vfs-xfer.h for GnomeVFSXferProgressCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncHandle */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncOpenCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncCreateCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncOpenAsChannelCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncCloseCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncReadCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncWriteCallback */
/* see gnome-vfs-file-info.h for GnomeVFSFileInfoResult */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncGetFileInfoCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncSetFileInfoCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncDirectoryLoadCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncXferProgressCallback */
/* see gnome-vfs-async-ops.h for GnomeVFSFindDirectoryResult */
/* see gnome-vfs-async-ops.h for GnomeVFSAsyncFindDirectoryCallback */
/* see gnome-vfs-callbacks.h for GnomeVFSStatusCallback */
/* see gnome-vfs-callbacks.h for GnomeVFSCallback */
/* see gnome-vfs-transform.h for GnomeVFSTransformInitFunc */
/* see gnome-vfs-transform.h for GnomeVFSTransformFunc */
/* see gnome-vfs-transform.h for GnomeVFSTransform */
/* see gnome-vfs-callbacks.h for GnomeVFSMessageCallbacks */
/* see gnome-vfs-iobuf.h for GnomeVFSIOBuf */
/* see gnome-vfs-inet-connection.h for GnomeVFSInetConnection */

/* Includes to provide compatibility with programs that
   still include gnome-vfs-types.h directly */
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-handle.h>
#include <libgnomevfs/gnome-vfs-find-directory.h>
#include <libgnomevfs/gnome-vfs-directory-filter.h>
#include <libgnomevfs/gnome-vfs-xfer.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-transform.h>
#include <libgnomevfs/gnome-vfs-callbacks.h>
#include <libgnomevfs/gnome-vfs-iobuf.h>
#include <libgnomevfs/gnome-vfs-inet-connection.h>

#endif /* _GNOME_VFS_TYPES_H */
