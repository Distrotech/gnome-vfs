/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-messages.c - Status message reporting for GNOME Virtual File
   System.

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

#include "gnome-vfs-messages.h"

typedef struct _Callback Callback;

struct _Callback {
        GnomeVFSStatusCallback callback_func;
        gpointer user_data;
        GDestroyNotify notify_func;
};

static Callback*
callback_new (GnomeVFSStatusCallback callback_func,
              gpointer user_data,
              GDestroyNotify notify_func)
{
        Callback *cb;
        
        cb = g_new(Callback, 1);

        cb->callback_func = callback_func;
        cb->user_data = user_data;
        cb->notify_func = notify_func;

        return cb;
}

static void
callback_destroy (Callback *cb)
{
        if (cb->notify_func != NULL) {
                (* cb->notify_func) (cb->user_data);
        }

        g_free(cb);
}

static void
callback_invoke (Callback *cb, const gchar* message)
{
        if (cb->callback_func) {
                (* cb->callback_func) (message, cb->user_data);
        }
}

struct _GnomeVFSMessageCallbacks {
        GSList *list;

};

GnomeVFSMessageCallbacks*
gnome_vfs_message_callbacks_new (void)
{
        GnomeVFSMessageCallbacks *cbs;

        cbs = g_new0(GnomeVFSMessageCallbacks, 1);

        return cbs;
}

void
gnome_vfs_message_callbacks_destroy (GnomeVFSMessageCallbacks *cbs)
{
        GSList *tmp;

        tmp = cbs->list;

        while (tmp != NULL) {
                Callback *cb;

                cb = tmp->data;

                callback_destroy (cb);
                
                tmp = g_slist_next (tmp);
        }
        
        g_slist_free (cbs->list);
        g_free(cbs);
}

void
gnome_vfs_message_callbacks_add (GnomeVFSMessageCallbacks *cbs,
                                 GnomeVFSStatusCallback    callback,
                                 gpointer                  user_data)
{
        gnome_vfs_message_callbacks_add_full (cbs, callback, user_data, NULL);
}

void
gnome_vfs_message_callbacks_add_full (GnomeVFSMessageCallbacks *cbs,
                                      GnomeVFSStatusCallback    callback,
                                      gpointer                  user_data,
                                      GDestroyNotify            notify)
{
        Callback *cb;

        cb = callback_new (callback, user_data, notify);
        
        cbs->list = g_slist_prepend (cbs->list, cb);
}

typedef gboolean (* MyGSListFilterFunc) (gpointer list_element, gpointer user_data);

static GSList*
my_g_slist_filter (GSList* list, MyGSListFilterFunc func, gpointer user_data)
{
        GSList *iter;
        GSList *retval;

        retval = NULL;
        iter = list;

        while (iter != NULL) {
                GSList *freeme;
                
                if ((*func)(iter->data, user_data)) {
                        retval = g_slist_prepend (retval, iter->data);
                }
                
                freeme = iter;
                iter = g_slist_next (iter);

                g_assert(freeme != NULL);

                /* Avoids using double the amount of space; glib can
                   recycle these nodes into the new list */
                g_slist_free_1 (freeme);
        }

        /* We assembled the nodes backward */
        retval = g_slist_reverse (retval);
        
        return retval;
}

static gboolean
callback_equal_predicate (gpointer callback, gpointer func)
{
        return (((Callback*)callback)->callback_func ==
                ((GnomeVFSStatusCallback)func));
}

static gboolean
data_equal_predicate (gpointer callback, gpointer data)
{
        return ((Callback*)callback)->user_data == data;
}

struct func_and_data {
        GnomeVFSStatusCallback func;
        gpointer data;
};

static gboolean
all_equal_predicate (gpointer callback, gpointer func_and_data)
{
        Callback *cb = callback;
        struct func_and_data* fd = func_and_data;

        return (cb->callback_func == fd->func && cb->user_data == fd->data);
}

void
gnome_vfs_message_callbacks_remove_by_func (GnomeVFSMessageCallbacks *cbs,
                                            GnomeVFSStatusCallback    callback)
{
        cbs->list = my_g_slist_filter (cbs->list, callback_equal_predicate, callback);
}

void
gnome_vfs_message_callbacks_remove_by_data (GnomeVFSMessageCallbacks *cbs,
                                            gpointer                  user_data)
{
        cbs->list = my_g_slist_filter (cbs->list, data_equal_predicate, user_data);
}

void
gnome_vfs_message_callbacks_remove (GnomeVFSMessageCallbacks *cbs,
                                    GnomeVFSStatusCallback    callback,
                                    gpointer                  user_data)
{
        struct func_and_data fd;

        fd.func = callback;
        fd.data = user_data;
        
        cbs->list = my_g_slist_filter (cbs->list, all_equal_predicate, &fd);
}

void
gnome_vfs_message_callbacks_emit (GnomeVFSMessageCallbacks *cbs,
                                  const gchar              *message)
{
        GSList *iter;

        iter = cbs->list;

        while (iter != NULL) {
                Callback *cb;

                cb = iter->data;

                callback_invoke (cb, message);
                
                iter = g_slist_next (iter);
        }
}

