#include "gnome-vfs-mime-info-cache.h"

void gnome_vfs_mime_info_reload (void)
{
}

int main (int argc, char **argv)
{
    GList *desktop_file_ids, *tmp; 
    char *mime_type;

    if (argc > 1) {
        mime_type = argv[1];
    } else {
        mime_type = "text/plain";
    }

    g_print ("Default: %s\n", gnome_vfs_mime_get_default_desktop_entry (mime_type));

    desktop_file_ids = gnome_vfs_mime_get_all_desktop_entries (mime_type);

    g_print ("All:\n");
    tmp = desktop_file_ids;
    while (tmp != NULL) {
        g_print ("%s\n", tmp->data);
        tmp = tmp->next;
    }

    return 0;
}
