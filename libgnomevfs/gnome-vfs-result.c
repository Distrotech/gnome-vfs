/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-error.c - Error handling for the GNOME Virtual File System.

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

   Author: Ettore Perazzoli <ettore@comm2000.it>
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


static gchar *status_strings[] = {
	/* GNOME_VFS_OK */			N_("No error"),
	/* GNOME_VFS_ERROR_NOTFOUND */		N_("File not found"),
	/* GNOME_VFS_ERROR_GENERIC */		N_("Generic error"),
	/* GNOME_VFS_ERROR_INTERNAL */		N_("Internal error"),
	/* GNOME_VFS_ERROR_BADPARAMS */		N_("Invalid parameters"),
	/* GNOME_VFS_ERROR_NOTSUPPORTED */	N_("Unsupported operation"),
	/* GNOME_VFS_ERROR_IO */		N_("I/O error"),
	/* GNOME_VFS_ERROR_CORRUPTEDDATA */	N_("Data corrupted"),
	/* GNOME_VFS_ERROR_WRONGFORMAT */	N_("Format not valid"),
	/* GNOME_VFS_ERROR_BADFILE */		N_("Bad file handle"),
	/* GNOME_VFS_ERROR_TOOBIG */		N_("File too big"),
	/* GNOME_VFS_ERROR_NOSPACE */		N_("No space left on device"),
	/* GNOME_VFS_ERROR_READONLY */		N_("Read-only file system"),
	/* GNOME_VFS_ERROR_INVALIDURI */	N_("Invalid URI"),
	/* GNOME_VFS_ERROR_NOTOPEN */		N_("File not open"),
	/* GNOME_VFS_ERROR_INVALIDOPENMODE */	N_("Open mode not valid"),
	/* GNOME_VFS_ERROR_ACCESSDENIED */	N_("Access denied"),
	/* GNOME_VFS_ERROR_TOOMANYOPENFILES */	N_("Too many open files"),
	/* GNOME_VFS_ERROR_EOF */		N_("End of file"),
	/* GNOME_VFS_ERROR_NOTADIRECTORY */	N_("Not a directory"),
	/* GNOME_VFS_ERROR_INPROGRESS */	N_("Operation in progress"),
	/* GNOME_VFS_ERROR_INTERRUPTED */	N_("Operation interrupted"),
	/* GNOME_VFS_ERROR_NOTPERMITTED */	N_("Operation not permitted")
};


/* FIXME: To be completed.  */
GnomeVFSResult
gnome_vfs_result_from_errno (void)
{
	switch (errno) {
	case EACCES:
		return GNOME_VFS_ERROR_ACCESSDENIED;
	case EBADF:
		return GNOME_VFS_ERROR_BADFILE;
	case EFBIG:
		return GNOME_VFS_ERROR_TOOBIG;
	case EIO:
		return GNOME_VFS_ERROR_IO;
	case EINTR:
		return GNOME_VFS_ERROR_INTERRUPTED;
	case EINVAL:
		return GNOME_VFS_ERROR_BADPARAMS;
	case EMFILE:
		return GNOME_VFS_ERROR_TOOMANYOPENFILES;
	case ENFILE:
		return GNOME_VFS_ERROR_TOOMANYOPENFILES;
	case ENOENT:
		return GNOME_VFS_ERROR_NOTFOUND;
	case ENOSPC:
		return GNOME_VFS_ERROR_NOSPACE;
	case ENOTDIR:
		return GNOME_VFS_ERROR_NOTADIRECTORY;
	case EPERM:
		return GNOME_VFS_ERROR_NOTPERMITTED;
	case EEXIST:
		return GNOME_VFS_ERROR_FILEEXISTS;
	default:
		return GNOME_VFS_ERROR_GENERIC;
	}
}


const gchar *
gnome_vfs_result_to_string (GnomeVFSResult error)
{
	if ((guint) error >= (guint) (sizeof (status_strings)
				      / sizeof (*status_strings)))
		return _("Unknown error");

	return _(status_strings[(guint) error]);
}
