#include <config.h>

#include <stdio.h>
#include <glib-object.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>

int
main (int argc, char **argv)
{
	GTimer *timer = g_timer_new ();

	g_type_init ();

	g_timer_start (timer);

	gnome_vfs_mime_info_reload ();

	fprintf (stderr, "mime parse took %g(ms)\n",
		 g_timer_elapsed (timer, NULL) * 500);

	return 0;
}
