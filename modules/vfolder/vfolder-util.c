
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>

#include "vfolder-util.h"
#include "vfolder-common.h"

/* assumes vuri->path already set */
gboolean
vfolder_uri_parse_internal (GnomeVFSURI *uri, VFolderURI *vuri)
{
	vuri->scheme = (gchar *) gnome_vfs_uri_get_scheme (uri);

	vuri->ends_in_slash = FALSE;

	if (strncmp (vuri->scheme, "all-", strlen ("all-")) == 0) {
		vuri->scheme += strlen ("all-");
		vuri->is_all_scheme = TRUE;
	} else
		vuri->is_all_scheme = FALSE;

	if (vuri->path != NULL) {
		int last_slash = strlen (vuri->path) - 1;
		char *first;

		/* Note: This handling of paths is somewhat evil, may need a
		 * bit of a rework */

		/* kill leading slashes, that is make sure there is
		 * only one */
		for (first = vuri->path; *first == '/'; first++)
			;
		if (first != vuri->path) {
			first--;
			vuri->path = first;
		}

		/* kill trailing slashes (leave first if all slashes) */
		while (last_slash > 0 && vuri->path [last_slash] == '/') {
			vuri->path [last_slash--] = '\0';
			vuri->ends_in_slash = TRUE;
		}

		/* get basename start */
		while (last_slash >= 0 && vuri->path [last_slash] != '/')
			last_slash--;

		if (last_slash > -1)
			vuri->file = vuri->path + last_slash + 1;
		else
			vuri->file = vuri->path;

		if (vuri->file[0] == '\0' &&
		    strcmp (vuri->path, "/") == 0) {
			vuri->file = NULL;
		}
	} else {
		vuri->ends_in_slash = TRUE;
		vuri->path = "/";
		vuri->file = NULL;
	}

	vuri->uri = uri;

	return TRUE;
}

gboolean
check_extension (const char *name, const char *ext_check)
{
	const char *ext;

	ext = strrchr (name, '.');
	if (ext && !strcmp (ext, ext_check))
		return TRUE;
	else
		return FALSE;
}

static void
monitor_callback_internal (GnomeVFSMonitorHandle *handle,
			   const gchar *monitor_uri,
			   const gchar *info_uri,
			   GnomeVFSMonitorEventType event_type,
			   gpointer user_data)
{
	VFolderMonitor *monitor = (VFolderMonitor *) handle;

	if (monitor->frozen)
		return;

	(*monitor->callback) (handle,
			      monitor_uri,
			      info_uri,
			      event_type,
			      monitor->user_data);
}

#define TIMEOUT_SECONDS 3

static GSList *stat_monitors = NULL;
G_LOCK_DEFINE_STATIC (stat_monitors);
static guint stat_timeout_tag = 0;

static time_t
ctime_for_uri (gchar *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	time_t ctime = 0;

	info = gnome_vfs_file_info_new ();
	
	result = gnome_vfs_get_file_info (uri,
					  info,
					  GNOME_VFS_FILE_INFO_DEFAULT);
	if (result == GNOME_VFS_OK) {
		ctime = info->ctime;
	}

	gnome_vfs_file_info_unref (info);

	return ctime;
}

static gboolean
monitor_timeout_cb (gpointer user_data)
{
        GSList *iter;

	G_LOCK (stat_monitors);
	for (iter = stat_monitors; iter; iter = iter->next) {
		VFolderMonitor *monitor = iter->data;
		time_t ctime;

		ctime = ctime_for_uri (monitor->uri);
		if (ctime == monitor->ctime)
			continue;

		(*monitor->callback) ((GnomeVFSMonitorHandle *) monitor,
				      monitor->uri,
				      monitor->uri,
				      ctime == 0 ?
				              GNOME_VFS_MONITOR_EVENT_DELETED :
				              GNOME_VFS_MONITOR_EVENT_CHANGED,
				      monitor->user_data);

		monitor->ctime = ctime;
	}
	G_UNLOCK (stat_monitors);

	return TRUE;
}

static VFolderMonitor *
monitor_start_internal (GnomeVFSMonitorType      type,
			gchar                   *uri,
			GnomeVFSMonitorCallback  callback,
			gpointer                 user_data)
{
	GnomeVFSResult result;
	VFolderMonitor *monitor;
	
	monitor = g_new0 (VFolderMonitor, 1);
	monitor->callback = callback;
	monitor->user_data = user_data;

	result = gnome_vfs_monitor_add (&monitor->vfs_handle, 
					uri,
					type,
					monitor_callback_internal,
					monitor);
	if (result == GNOME_VFS_ERROR_NOT_SUPPORTED) {
		monitor->uri = g_strdup (uri);
		monitor->ctime = ctime_for_uri (uri);

		G_LOCK (stat_monitors);
		if (stat_timeout_tag == 0) {
			stat_timeout_tag = 
				g_timeout_add (TIMEOUT_SECONDS * 1000,
					       monitor_timeout_cb,
					       NULL);
		}

		stat_monitors = g_slist_prepend (stat_monitors, monitor);
		G_UNLOCK (stat_monitors);
	}

	return monitor;
}

VFolderMonitor *
vfolder_monitor_directory_new (gchar                   *uri,
			       GnomeVFSMonitorCallback  callback,
			       gpointer                 user_data)
{
	return monitor_start_internal (GNOME_VFS_MONITOR_DIRECTORY, 
				       uri, 
				       callback,
				       user_data);
}

VFolderMonitor *
vfolder_monitor_file_new (gchar                   *uri,
			  GnomeVFSMonitorCallback  callback,
			  gpointer                 user_data)
{
	return monitor_start_internal (GNOME_VFS_MONITOR_FILE, 
				       uri, 
				       callback,
				       user_data);
}

void 
vfolder_monitor_freeze (VFolderMonitor *monitor)
{
	monitor->frozen = TRUE;
}

void 
vfolder_monitor_thaw (VFolderMonitor *monitor)
{
	monitor->frozen = FALSE;
}

void 
vfolder_monitor_cancel (VFolderMonitor *monitor)
{
	if (monitor->vfs_handle)
		gnome_vfs_monitor_cancel (monitor->vfs_handle);
	else {
		G_LOCK (stat_monitors);
		stat_monitors = g_slist_remove (stat_monitors, monitor);

		if (!stat_monitors) {
			g_source_remove (stat_timeout_tag);
			stat_timeout_tag = 0;
		}
		G_UNLOCK (stat_monitors);
	}

	g_free (monitor);
}
