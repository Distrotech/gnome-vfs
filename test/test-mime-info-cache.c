#include <unistd.h>
#include "gnome-vfs.h"
#include "gnome-vfs-mime-info-cache.h"
#include "gnome-vfs-mime-handlers.h"

void gnome_vfs_mime_info_reload (void);
gpointer foo (const char *mime_type);

void gnome_vfs_mime_info_reload (void)
{
}

gpointer foo (const char *mime_type) {
    GList *desktop_file_apps, *tmp; 

    gnome_vfs_init ();

    while (1) {
        g_print ("Default: %s\n",
                 gnome_vfs_mime_get_default_desktop_entry (mime_type));

        desktop_file_apps = gnome_vfs_mime_get_all_applications (mime_type);

        g_print ("All:\n");
        tmp = desktop_file_apps;
        while (tmp != NULL) {
            GnomeVFSMimeApplication *application;

            application = (GnomeVFSMimeApplication *) tmp->data;
            g_print ("%s: %s\n", application->id, application->name);
            tmp = tmp->next;
        }
        sleep (1);
    }
}

int main (int argc, char **argv)
{
    char *mime_type;
    int i;

    if (argc > 1) {
        mime_type = argv[1];
    } else {
        mime_type = "text/plain";
    }

    g_thread_init (NULL);

    i = 1;
    while (i--) {
        (void) g_thread_create ((GThreadFunc) foo, mime_type, FALSE, NULL);
    }

    while (TRUE);

    return 0;
}
