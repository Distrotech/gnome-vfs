

#ifndef VFOLDER_COMMON_H
#define VFOLDER_COMMON_H

#include <glib.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-method.h>

#include "vfolder-util.h"

G_BEGIN_DECLS

typedef struct _VFolderInfo VFolderInfo;
typedef struct _Query Query;

/* 
 * Entry API
 */
typedef struct {
	gint            refcnt;
	gint            allocs;

	VFolderInfo    *info;

	char           *displayname;
	char           *filename;
	VFolderMonitor *monitor;

	GnomeVFSURI    *uri;

	gboolean        user_private;
	GSList         *keywords;       /* GQuark */
	GSList         *added_keywords; /* GQuark */
} Entry;

Entry        *entry_new                  (VFolderInfo *info,
					  gchar       *filename,
					  gchar       *displayname,
					  gboolean     user_private);

void          entry_ref                  (Entry *entry);
void          entry_unref                (Entry *entry);

void          entry_alloc                (Entry *entry);
void          entry_dealloc              (Entry *entry);
gboolean      entry_is_allocated         (Entry *entry);

gboolean      entry_make_user_private    (Entry *entry);
gboolean      entry_is_user_private      (Entry *entry);

void          entry_set_dirty            (Entry *entry);

void          entry_set_filename         (Entry *entry, gchar *name);
gchar        *entry_get_filename         (Entry *entry);

void          entry_set_displayname      (Entry *entry, gchar *name);
gchar        *entry_get_displayname      (Entry *entry);

GnomeVFSURI  *entry_get_real_uri         (Entry *entry);

GSList       *entry_get_keywords         (Entry *entry);
void          entry_add_implicit_keyword (Entry *entry, GQuark keyword);

void          entry_quick_read_keys      (Entry  *entry,
					  gchar  *key1,
					  gchar **value1,
					  gchar  *key2,
					  gchar **value2);

/* 
 * Folder API
 */
typedef struct _Folder Folder;
struct _Folder {
	gint               refcnt;

	VFolderInfo       *info;

	char              *name;

	gchar             *extend_uri;
	VFolderMonitor    *entend_monitor;

	gboolean           has_user_private_subfolders;
	gboolean           hidden;
	gboolean           user_private;

	Folder            *parent;

	char              *desktop_file;     /* the .directory file */

	Query             *query;

	/* The following is for per file access */
	GHashTable        *excludes;         /* excluded by dirname/fileuri */
	GSList            *includes;         /* included by dirname/fileuri */
	GHashTable        *includes_ht;

	GSList            *subfolders;
	GHashTable        *subfolders_ht;

	/* Some flags */
	gboolean           read_only;
	gboolean           dont_show_if_empty;
	gboolean           only_unallocated; /* include only unallocated */

	/* lazily done, will run query only when it needs to */
	gboolean           up_to_date;
	gboolean           sorted;

	GSList            *entries;
	GHashTable        *entries_ht;
};

typedef struct {
	enum {
		FOLDER,
		DESKTOP_FILE,
		UNKNOWN_URI
	} type;
	
	Folder      *folder;
	Entry       *entry;
	GnomeVFSURI *uri;
} FolderChild;

Folder       *folder_new               (VFolderInfo *info,
					gchar       *name);

void          folder_ref               (Folder *folder);
void          folder_unref             (Folder *folder);

gboolean      folder_make_user_private (Folder *folder);
gboolean      folder_is_user_private   (Folder *folder);

void          folder_set_dirty         (Folder *folder);

void          folder_set_name          (Folder *folder, gchar *name);
gchar        *folder_get_name          (Folder *folder);

void          folder_set_query          (Folder *folder, Query *query);
Query        *folder_get_query          (Folder *folder);

void          folder_set_extend_uri    (Folder      *folder, 
					gchar       *uri);
gchar        *folder_get_extend_uri    (Folder      *folder);

void          folder_set_desktop_file  (Folder      *folder,
					gchar       *filename);
Entry        *folder_get_desktop_file  (Folder      *folder);

gboolean      folder_get_child         (Folder      *folder, 
					gchar       *name,
					FolderChild *child);

Entry        *folder_get_entry         (Folder *folder, gchar *filename);
const GSList *folder_list_entries      (Folder *folder);
void          folder_remove_entry      (Folder *folder, Entry *entry);
void          folder_add_entry         (Folder *folder, Entry *entry);

void          folder_add_include       (Folder *folder, gchar *file);
void          folder_remove_include    (Folder *folder, gchar *file);

void          folder_add_exclude       (Folder *folder, gchar *file);
void          folder_remove_exclude    (Folder *folder, gchar *file);

Folder       *folder_get_subfolder     (Folder *folder, gchar *name);
const GSList *folder_list_subfolders   (Folder *folder);
void          folder_remove_subfolder  (Folder *folder, Folder *sub);
void          folder_add_subfolder     (Folder *folder, Folder *sub);

/* 
 * Query API
 */
struct _Query {
	enum {
		QUERY_OR,
		QUERY_AND,
		QUERY_PARENT,
		QUERY_KEYWORD,
		QUERY_FILENAME
	} type;
	union {
		GSList   *queries;
		GQuark    keyword;
		gchar    *filename;
	} val;
	gboolean not;
};

Query    *query_new (int type);

void      query_free (Query *query);

gboolean  query_try_match (Query  *query,
			   Folder *folder,
			   Entry  *efile);

/* 
 * VFolderInfo API
 */
typedef struct {
	gchar          *uri;
	VFolderMonitor *monitor;
} ItemDir;

struct _VFolderInfo {
	GStaticRWLock rw_lock;

	char     *scheme;

	char           *filename;
	time_t          filename_last_write;
	VFolderMonitor *filename_monitor;

	/* deprecated */
	char           *desktop_dir; /* directory with .directorys */
	VFolderMonitor *desktop_dir_monitor;

	/* dir where user changes to items are stored */
	char           *write_dir; 
	VFolderMonitor *write_dir_monitor;

	/* Consider item dirs and mergedirs writeable?? */

	GSList   *item_dirs;

	/* old style dirs to merge in -- monitoring?? */
	GSList   *merge_dirs;

	/* if entries are valid, else
	 * they need to be (re)read */
	gboolean  entries_valid;

	GSList   *entries;

	/* entry hash by basename */
	GHashTable *entries_ht;

	/* The root folder */
	Folder *root;

	/* some flags */
	gboolean read_only;

	gboolean dirty;

	int inhibit_write;

	GSList *requested_monitors;

	/* ctime for folders */
	time_t modification_time;

	guint reread_queue;
};

#define VFOLDER_INFO_READ_LOCK(vi) \
	g_static_rw_lock_reader_lock (&(vi->rw_lock))
#define VFOLDER_INFO_READ_UNLOCK(vi) \
	g_static_rw_lock_reader_unlock (&(vi->rw_lock))

#define VFOLDER_INFO_WRITE_LOCK(vi) \
	g_static_rw_lock_writer_lock (&(vi->rw_lock))
#define VFOLDER_INFO_WRITE_UNLOCK(vi) \
	g_static_rw_lock_writer_unlock (&(vi->rw_lock))

VFolderInfo  *vfolder_info_locate           (const gchar *scheme);

void          vfolder_info_set_dirty        (VFolderInfo *info);

Folder       *vfolder_info_get_folder       (VFolderInfo *info, gchar *path);
Folder       *vfolder_info_get_parent       (VFolderInfo *info, gchar *path);
Entry        *vfolder_info_get_entry        (VFolderInfo *info, gchar *path);

GSList       *vfolder_info_list_all_entries (VFolderInfo *info);
void          vfolder_info_add_entry        (VFolderInfo *info, Entry *entry);
void          vfolder_info_remove_entry     (VFolderInfo *info, Entry *entry);

void          vfolder_info_emit_change      (VFolderInfo              *info,
					     char                     *uri,
					     GnomeVFSMonitorEventType  event_type);

void          vfolder_info_add_monitor      (VFolderInfo           *info,
					     gchar                 *uri,
					     GnomeVFSMethodHandle **handle);
void          vfolder_info_cancel_monitor   (GnomeVFSMethodHandle  *handle);

void          vfolder_info_destroy_all      (void);


G_END_DECLS

#endif /* VFOLDER_COMMON_H */
