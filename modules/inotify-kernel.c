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

#include "config.h"

#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <glib.h>
#include "inotify-kernel.h"
#ifdef HAVE_SYS_INOTIFY_H
/* We don't actually include the libc header, because there has been
 * problems with libc versions that was built without inotify support.
 * Instead we use the local version.
 */
#include "local_inotify.h"
#include "local_inotify_syscalls.h"
#elif defined (HAVE_LINUX_INOTIFY_H)
#include <linux/inotify.h>
#include "local_inotify_syscalls.h"
#endif

/* Timings for pairing MOVED_TO / MOVED_FROM events */
#define PROCESS_EVENTS_TIME 33 /* milliseconds */
#define DEFAULT_HOLD_UNTIL_TIME 1000 /* 1 millisecond */
#define MOVE_HOLD_UNTIL_TIME 5000 /* 5 milliseconds */

static int inotify_instance_fd = -1;
static GQueue *events_to_process = NULL;
static GQueue *event_queue = NULL;
static GHashTable * cookie_hash = NULL;
static GIOChannel *inotify_read_ioc;
static void (*user_cb)(ik_event_t *event);

static gboolean ik_read_callback (gpointer user_data);
static gboolean ik_process_eq_callback (gpointer user_data);

typedef struct ik_event_internal {
	ik_event_t *event;
	gboolean seen;
	gboolean sent;
	GTimeVal hold_until;
	struct ik_event_internal *pair;
} ik_event_internal_t;

gboolean ik_startup (void (*cb)(ik_event_t *event))
{
	GSource *source;

	user_cb = cb;
	/* Ignore multi-calls */
	if (inotify_instance_fd >= 0)
		return TRUE;

	inotify_instance_fd = inotify_init ();

	if (inotify_instance_fd < 0) {
		return FALSE;
	}

	inotify_read_ioc = g_io_channel_unix_new(inotify_instance_fd);

	g_io_channel_set_encoding(inotify_read_ioc, NULL, NULL);
	g_io_channel_set_flags(inotify_read_ioc, G_IO_FLAG_NONBLOCK, NULL);
	source = g_io_create_watch(inotify_read_ioc, G_IO_IN | G_IO_HUP | G_IO_ERR);
	g_source_set_callback(source, ik_read_callback, NULL, NULL);
	g_source_attach(source, NULL);
	g_source_unref (source);

	g_timeout_add (PROCESS_EVENTS_TIME, ik_process_eq_callback, NULL);

	cookie_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
	event_queue = g_queue_new ();
	events_to_process = g_queue_new ();

	return TRUE;
}

static ik_event_internal_t *ik_event_internal_new (ik_event_t *event)
{
	ik_event_internal_t *internal_event = g_new0(ik_event_internal_t, 1);
	GTimeVal tv;

	g_assert (event);
	
	g_get_current_time (&tv);
	g_time_val_add (&tv, DEFAULT_HOLD_UNTIL_TIME);
	internal_event->event = event;
	internal_event->hold_until = tv;

	return internal_event;
}

static ik_event_t *ik_event_new (char *buffer)
{
   struct inotify_event *kevent = (struct inotify_event *)buffer;
   g_assert (buffer);
   ik_event_t *event = g_new0(ik_event_t,1);
   event->wd = kevent->wd;
   event->mask = kevent->mask;
   event->cookie = kevent->cookie;
   event->len = kevent->len;
   if (event->len)
      event->name = g_strdup(kevent->name);
   else
      event->name = g_strdup("");

   return event;
}

void ik_event_free (ik_event_t *event)
{
	if (event->paired)
		g_free (event->pair_name);
	g_free(event->name);
	g_free(event);
}

guint32 ik_watch (const char *path, guint32 mask, int *err)
{
   int wd = -1;

   g_assert (path != NULL);
   g_assert (inotify_instance_fd >= 0);

   wd = inotify_add_watch (inotify_instance_fd, path, mask);

   if (wd < 0)
   {
      int e = errno;
      // FIXME: debug msg failed to add watch
      if (err)
         *err = e;
      return wd;
   }

   g_assert (wd >= 0);
   return wd;
}

int ik_ignore(guint32 wd)
{
   g_assert (wd >= 0);
   g_assert (inotify_instance_fd >= 0);

   if (inotify_rm_watch (inotify_instance_fd, wd) < 0)
   {
      //int e = errno;
      // failed to rm watch
      return -1;
   }
   
   return 0;
}

#define MAX_PENDING_COUNT 5
#define PENDING_THRESHOLD(qsize) ((qsize) >> 1)
#define PENDING_MARGINAL_COST(p) ((unsigned int)(1 << (p)))
#define MAX_QUEUED_EVENTS 2048
#define AVERAGE_EVENT_SIZE sizeof (struct inotify_event) + 16
#define PENDING_PAUSE_MICROSECONDS 8000

static void ik_read_events (gsize *buffer_size_out, gchar **buffer_out)
{
	static int prev_pending = 0, pending_count = 0;
	static gchar *buffer = NULL;
	static gsize buffer_size;

	/* Initialize the buffer on our first read() */
	if (buffer == NULL)
	{
		buffer_size = AVERAGE_EVENT_SIZE;
		buffer_size *= MAX_QUEUED_EVENTS;
		buffer = g_malloc (buffer_size);

		if (!buffer) {
			*buffer_size_out = 0;
			*buffer_out = NULL;
			return;
		}
	}

	*buffer_size_out = 0;
	*buffer_out = NULL;

	while (pending_count < MAX_PENDING_COUNT) {
		unsigned int pending;

		if (ioctl (inotify_instance_fd, FIONREAD, &pending) == -1)
			break;

		pending /= AVERAGE_EVENT_SIZE;

		/* Don't wait if the number of pending events is too close
		* to the maximum queue size.
		*/
		if (pending > PENDING_THRESHOLD (MAX_QUEUED_EVENTS))
			break;

		/* With each successive iteration, the minimum rate for
		* further sleep doubles. */
		if (pending-prev_pending < PENDING_MARGINAL_COST(pending_count))
			break;

		prev_pending = pending;
		pending_count++;

		/* We sleep for a bit and try again */
		g_usleep (PENDING_PAUSE_MICROSECONDS);
	}

	memset(buffer, 0, buffer_size);

	if (g_io_channel_read_chars (inotify_read_ioc, (char *)buffer, buffer_size, buffer_size_out, NULL) != G_IO_STATUS_NORMAL) {
		// error reading
	}
	*buffer_out = buffer;

	prev_pending = 0;
	pending_count = 0;
}

static gboolean ik_read_callback(gpointer user_data)
{
	gchar *buffer;
	gsize buffer_size, buffer_i, events;

	ik_read_events (&buffer_size, &buffer);

	buffer_i = 0;
	events = 0;
	while (buffer_i < buffer_size)
	{
		struct inotify_event *event;
		gsize event_size;
		event = (struct inotify_event *)&buffer[buffer_i];
		event_size = sizeof(struct inotify_event) + event->len;
		g_queue_push_tail (events_to_process, ik_event_internal_new (ik_event_new (&buffer[buffer_i])));
		buffer_i += event_size;
		events++;
	}
	return TRUE;
}

static gboolean
g_timeval_lt(GTimeVal *val1, GTimeVal *val2)
{
	if (val1->tv_sec < val2->tv_sec)
		return TRUE;

	if (val1->tv_sec > val2->tv_sec)
		return FALSE;

	/* val1->tv_sec == val2->tv_sec */
	if (val1->tv_usec < val2->tv_usec)
		return TRUE;

	return FALSE;
}

static gboolean
g_timeval_eq(GTimeVal *val1, GTimeVal *val2)
{
	return (val1->tv_sec == val2->tv_sec) && (val1->tv_usec == val2->tv_usec);
}

static void
ik_pair_events (ik_event_internal_t *event1, ik_event_internal_t *event2)
{
    g_assert (event1 && event2);
    /* We should only be pairing events that have the same cookie */
    g_assert (event1->event->cookie == event2->event->cookie);
    /* We shouldn't pair an event that already is paired */
    g_assert (event1->pair == NULL && event2->pair == NULL);
    event1->pair = event2;
    event2->pair = event1;

    if (g_timeval_lt (&event1->hold_until, &event2->hold_until))
        event1->hold_until = event2->hold_until;

    event2->hold_until = event1->hold_until;
}

static void
ik_event_add_microseconds (ik_event_internal_t *event, glong ms)
{
    g_assert (event);
    g_time_val_add (&event->hold_until, ms);
}

static gboolean
ik_event_ready (ik_event_internal_t *event)
{
    GTimeVal tv;
    g_assert (event);

    g_get_current_time (&tv);

    /* An event is ready if,
     *
     * it has no cookie -- there is nothing to be gained by holding it
     * or, it is already paired -- we don't need to hold it anymore
     * or, we have held it long enough
     */
    return event->event->cookie == 0 ||
           event->pair != NULL ||
           g_timeval_lt(&event->hold_until, &tv) || g_timeval_eq(&event->hold_until, &tv);
}

static void
ik_pair_moves (gpointer data, gpointer user_data)
{
    ik_event_internal_t *event = (ik_event_internal_t *)data;

    if (event->seen == TRUE || event->sent == TRUE)
        return;

    if (event->event->cookie != 0)
    {
		/* When we get a MOVED_FROM event we delay sending the event by
		 * MOVE_HOLD_UNTIL_TIME microseconds. We need to do this because a
		 * MOVED_TO pair _might_ be coming in the near future */
        if (event->event->mask & IN_MOVED_FROM) {
            g_hash_table_insert (cookie_hash, GINT_TO_POINTER(event->event->cookie), event);
            ik_event_add_microseconds (event, MOVE_HOLD_UNTIL_TIME);
        } else if (event->event->mask & IN_MOVED_TO) {
			/* We need to check if we are waiting for this MOVED_TO events cookie to pair it with
			 * a MOVED_FROM */
            ik_event_internal_t *match = NULL;
            match = g_hash_table_lookup (cookie_hash, GINT_TO_POINTER(event->event->cookie));
            if (match) {
                g_hash_table_remove (cookie_hash, GINT_TO_POINTER(event->event->cookie));
                ik_pair_events (match, event);
            }
        }
    }
    event->seen = TRUE;
}

static void
ik_process_events ()
{
    g_queue_foreach (events_to_process, ik_pair_moves, NULL);

    while (!g_queue_is_empty (events_to_process))
    {
        ik_event_internal_t *event = g_queue_peek_head (events_to_process);

        /* This must have been sent as part of a MOVED_TO/MOVED_FROM */
        if (event->sent)
		{
			/* Pop event */
			g_queue_pop_head (events_to_process);
			/* Free the internal event structure */
			g_free (event);
            continue;
		}

		/* The event isn't ready yet */
        if (!ik_event_ready (event)) {
            break;
        }

        /* Pop it */
        event = g_queue_pop_head (events_to_process);

        /* Check if this is a MOVED_FROM that is also sitting in the cookie_hash */
        if (event->event->cookie && event->pair == NULL &&
            g_hash_table_lookup (cookie_hash, GINT_TO_POINTER(event->event->cookie)))
        {
            g_hash_table_remove (cookie_hash, GINT_TO_POINTER(event->event->cookie));
        }

        if (event->pair) {
			/* We send out paired MOVED_FROM/MOVED_TO events in the same event buffer */
			g_assert (event->event->mask == IN_MOVED_FROM && event->pair->event->mask == IN_MOVED_TO);
			/* Copy the paired data */
			event->event->paired = TRUE;
			event->event->pair_wd = event->pair->event->wd;
			event->event->pair_mask = event->pair->event->mask;
			event->event->pair_len = event->pair->event->len;
			event->event->pair_name = event->pair->event->name;
            event->pair->sent = TRUE;
			/* The pairs ik_event_t is never sent to the user so
			 * we free it here */
			g_free (event->pair->event);
        } else if (event->event->cookie) {
			/* If we couldn't pair a MOVED_FROM and MOVED_TO together, we change
			 * the event masks */
			if (event->event->mask & IN_MOVED_FROM)
				event->event->mask = IN_DELETE|(event->event->mask & IN_ISDIR);
			if (event->event->mask & IN_MOVED_TO)
				event->event->mask = IN_CREATE|(event->event->mask & IN_ISDIR);
			/* Changeing MOVED_FROM to DELETE and MOVED_TO to create lets us make
			 * the gaurantee that you will never see a non-matched MOVE event */
		}

		/* Push the ik_event_t onto the event queue */
		g_queue_push_tail (event_queue, event->event);
		/* Free the internal event structure */
		g_free (event);

    }
}

gboolean ik_process_eq_callback (gpointer user_data)
{
    /* Try and move as many events to the event queue */
    ik_process_events ();

	while (!g_queue_is_empty (event_queue))
	{
		ik_event_t *event = g_queue_pop_head (event_queue);

		user_cb (event);
	}

	return TRUE;
}
