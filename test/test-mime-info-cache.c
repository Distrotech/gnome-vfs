#include <unistd.h>
#include "gnome-vfs-mime-info-cache.h"

void gnome_vfs_mime_info_reload (void);
gpointer foo (const char *mime_type);

void gnome_vfs_mime_info_reload (void)
{
}

gpointer foo (const char *mime_type) {
    GList *desktop_file_ids, *tmp; 
    while (1) {
        g_print ("Default: %s\n",
                 gnome_vfs_mime_get_default_desktop_entry (mime_type));

        desktop_file_ids = gnome_vfs_mime_get_all_desktop_entries (mime_type);

        g_print ("All:\n");
        tmp = desktop_file_ids;
        while (tmp != NULL) {
            g_print ("%s\n", (char *) tmp->data);
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

    i = 200;
    while (i--) {
        (void) g_thread_create ((GThreadFunc) foo, mime_type, FALSE, NULL);
    }

    while (TRUE);

    return 0;
}
