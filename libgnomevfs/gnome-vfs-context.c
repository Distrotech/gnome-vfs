/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-context.c - context VFS modules can use to communicate with gnome-vfs proper

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

   Author: Havoc Pennington <hp@redhat.com> */

#include "gnome-vfs-context.h"
#include "gnome-vfs-cancellation.h"
#include "gnome-vfs-messages.h"

struct _GnomeVFSContext {
  GnomeVFSCancellation *cancellation;
  GnomeVFSMessageCallbacks *callbacks;
  gchar* redirect_uri;

};

GnomeVFSContext*
gnome_vfs_context_new (void)
{


}

void
gnome_vfs_context_ref (GnomeVFSContext *ctx)
{


}

void
gnome_vfs_context_unref (GnomeVFSContext *ctx)
{


}


GnomeVFSMessageCallbacks*
gnome_vfs_context_get_message_callbacks (GnomeVFSContext *ctx)
{

}

GnomeVFSCancellation*
gnome_vfs_context_get_cancellation (GnomeVFSContext *ctx)
{


}


gchar*
gnome_vfs_context_get_redirect_uri      (GnomeVFSContext *ctx)
{
  

}

void
gnome_vfs_context_set_redirect_uri      (GnomeVFSContext *ctx,
                                         const gchar     *uri)
{


}
