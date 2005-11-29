/*
	Copyright (C) 2005 John McCutchan

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License version 2 for more details.
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software Foundation,
	Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef __INOTIFY_KERNEL_H
#define __INOTIFY_KERNEL_H

typedef struct ik_event_s {
    guint32 wd;
    guint32 mask;
    guint32 cookie;
    guint32 len;
    char *  name;
	struct ik_event_s *pair;
} ik_event_t;

gboolean ik_startup (void (*cb)(ik_event_t *event));
ik_event_t *ik_event_new_dummy (const char *name, guint32 wd, guint32 mask);
void ik_event_free (ik_event_t *event);

guint32 ik_watch(const char *path, guint32 mask, int *err);
int ik_ignore(const char *path, guint32 wd);

#endif
