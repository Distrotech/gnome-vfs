/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-private.h - Private header file for the GNOME Virtual
   File System.

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

   Author: Ettore Perazzoli <ettore@gnu.org> */

#ifndef _GNOME_VFS_PRIVATE_H
#define _GNOME_VFS_PRIVATE_H

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

BEGIN_GNOME_DECLS

#include "gnome-vfs-private-types.h"

#include "gnome-vfs-cancellation.h"
#include "gnome-vfs-configuration.h"
#include "gnome-vfs-handle.h"
#include "gnome-vfs-list-sort.h"
#include "gnome-vfs-method.h"
#include "gnome-vfs-parse-ls.h"
#include "gnome-vfs-private-ops.h"
#include "gnome-vfs-regexp-filter.h"
#include "gnome-vfs-seekable.h"
#include "gnome-vfs-shellpattern-filter.h"
#include "gnome-vfs-utils.h"

/* AIX requires this to be the first thing in the file.  */
#ifndef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#   endif
#  endif
# endif
#endif

#ifndef HAVE_GETDELIM
#include <stdio.h>
#include <unistd.h> /* ssize_t */
ssize_t getdelim (char **lineptr, size_t n, int terminator, FILE *stream);
#endif

END_GNOME_DECLS

#endif /* _GNOME_VFS_PRIVATE_H */
