

#ifndef VFOLDER_UTIL_H
#define VFOLDER_UTIL_H

#include <time.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-monitor.h>

G_BEGIN_DECLS

typedef struct {
	const gchar *scheme;
	gboolean     is_all_scheme;
	gboolean     ends_in_slash;
	gchar       *path;
	gchar       *file;
	GnomeVFSURI *uri;
} VFolderURI;

/* assumes vuri->path already set */
gboolean vfolder_uri_parse_internal (GnomeVFSURI *uri, VFolderURI *vuri);

#define VFOLDER_URI_PARSE(_uri, _vuri) {                                    \
	gchar *path;                                                        \
	path = gnome_vfs_unescape_string ((_uri)->text, G_DIR_SEPARATOR_S); \
	if (path != NULL) {                                                 \
		(_vuri)->path = g_alloca (strlen (path) + 1);               \
		strcpy ((_vuri)->path, path);                               \
		g_free (path);                                              \
	} else {                                                            \
		(_vuri)->path = NULL;                                       \
	}                                                                   \
	vfolder_uri_parse_internal ((_uri), (_vuri));                       \
}


typedef struct {
	GnomeVFSMonitorType     type;

	GnomeVFSMonitorHandle  *vfs_handle;

	time_t                  ctime;
	gchar                  *uri;

	gboolean                frozen;

	GnomeVFSMonitorCallback callback;
	gpointer                user_data;
} VFolderMonitor;


VFolderMonitor *vfolder_monitor_dir_new  (gchar                   *uri,
					  GnomeVFSMonitorCallback  callback,
					  gpointer                 user_data);
VFolderMonitor *vfolder_monitor_file_new (gchar                   *uri,
					  GnomeVFSMonitorCallback  callback,
					  gpointer                 user_data);
void            vfolder_monitor_emit     (gchar                   *uri,
					  GnomeVFSMonitorEventType event_type);
void            vfolder_monitor_freeze   (VFolderMonitor          *monitor);
void            vfolder_monitor_thaw     (VFolderMonitor          *monitor);
void            vfolder_monitor_cancel   (VFolderMonitor          *monitor);


GnomeVFSResult vfolder_make_directory_and_parents (gchar    *uri, 
						   gboolean  skip_filename,
						   guint     permissions);


gchar   *vfolder_timestamp_file_name   (gchar      *file);
gchar   *vfolder_untimestamp_file_name (gchar      *file);
gboolean vfolder_check_extension       (const char *name, 
					const char *ext_check);
gchar   *vfolder_escape_home           (gchar      *file);

gchar   *vfolder_build_uri             (const char *first_element,
					...);


G_END_DECLS

#endif /* VFOLDER_UTIL_H */
