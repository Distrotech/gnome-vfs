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

   Author: Ettore Perazzoli <ettore@gnu.org>
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <netdb.h>
#include <errno.h>

#include "gnome-vfs.h"
#include "gnome-vfs-private.h"


extern int h_errno;


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
	/* GNOME_VFS_ERROR_FILEEXISTS */	N_("File exists"),
	/* GNOME_VFS_ERROR_LOOP */		N_("Looping links encountered"),
	/* GNOME_VFS_ERROR_NOTPERMITTED */	N_("Operation not permitted"),
	/* GNOME_VFS_ERROR_ISDIRECTORY */       N_("Is a directory"),
        /* GNOME_VFS_ERROR_NOMEM */             N_("Not enough memory"),
	/* GNOME_VFS_ERROR_HOSTNOTFOUND */	N_("Host not found"),
	/* GNOME_VFS_ERROR_INVALIDHOSTNAME */	N_("Host name not valid"),
	/* GNOME_VFS_ERROR_HOSTHASNOADDRESS */  N_("Host has no address"),
	/* GNOME_VFS_ERROR_LOGINFAILED */	N_("Login failed"),
	/* GNOME_VFS_ERROR_CANCELLED */		N_("Operation cancelled"),
	/* GNOME_VFS_ERROR_DIRECTORYBUSY */     N_("Directory busy"),
	/* GNOME_VFS_ERROR_DIRECTORYNOTEMPTY */ N_("Directory not empty"),
	/* GNOME_VFS_ERROR_TOOMANYLINKS */	N_("Too many links"),
	/* GNOME_VFS_ERROR_READONLYFS */	N_("Read only file system"),
	/* GNOME_VFS_ERROR_NOTSAMEFS */		N_("Not on the same file system"),
	/* GNOME_VFS_ERROR_NAMETOOLONG */	N_("Name too long")
};


/* FIXME: To be completed.  */
GnomeVFSResult
gnome_vfs_result_from_errno (void)
{
	/* Please keep these in alphabetical order.  */
	switch (errno) {
	case E2BIG:     return GNOME_VFS_ERROR_TOOBIG;
	case EACCES:	return GNOME_VFS_ERROR_ACCESSDENIED;
	case EBUSY:	return GNOME_VFS_ERROR_DIRECTORYBUSY;
	case EBADF:	return GNOME_VFS_ERROR_BADFILE;
	case EEXIST:	return GNOME_VFS_ERROR_FILEEXISTS;
	case EFAULT:	return GNOME_VFS_ERROR_INTERNAL;
	case EFBIG:	return GNOME_VFS_ERROR_TOOBIG;
	case EINTR:	return GNOME_VFS_ERROR_INTERRUPTED;
	case EINVAL:	return GNOME_VFS_ERROR_BADPARAMS;
	case EIO:	return GNOME_VFS_ERROR_IO;
	case EISDIR:	return GNOME_VFS_ERROR_ISDIRECTORY;
	case ELOOP:	return GNOME_VFS_ERROR_LOOP;
	case EMFILE:	return GNOME_VFS_ERROR_TOOMANYOPENFILES;
	case EMLINK:	return GNOME_VFS_ERROR_TOOMANYLINKS;
	case ENFILE:	return GNOME_VFS_ERROR_TOOMANYOPENFILES;
	case ENOTEMPTY: return GNOME_VFS_ERROR_DIRECTORYNOTEMPTY;
	case ENOENT:	return GNOME_VFS_ERROR_NOTFOUND;
	case ENOMEM:	return GNOME_VFS_ERROR_NOMEM;
	case ENOSPC:	return GNOME_VFS_ERROR_NOSPACE;
	case ENOTDIR:	return GNOME_VFS_ERROR_NOTADIRECTORY;
	case EPERM:	return GNOME_VFS_ERROR_NOTPERMITTED;
	case EROFS:	return GNOME_VFS_ERROR_READONLYFS;
	case EXDEV:	return GNOME_VFS_ERROR_NOTSAMEFS;
	default:	return GNOME_VFS_ERROR_GENERIC;
	}
}


GnomeVFSResult
gnome_vfs_result_from_h_errno (void)
{
	switch (h_errno) {
	case HOST_NOT_FOUND:	return GNOME_VFS_ERROR_HOSTNOTFOUND;
	case NO_ADDRESS:	return GNOME_VFS_ERROR_HOSTHASNOADDRESS;
	case TRY_AGAIN:		/* FIXME? */
	case NO_RECOVERY:	/* FIXME? */
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
