#ifndef _FTP_METHOD_H
#define _FTP_METHOD_H

#include "gnome-vfs.h"

typedef struct {
	int  ref_count;
	char *hostname;
	char *username;
	char *password;
	char *current_directory;
	char *home;
	
	time_t time_stamp;

	guint  port;

	/*
	 * our connection to the remote end
	 */
	int  sock;
	int use_proxy;
	
	/*
	 * For AmiTCP systems
	 */
	unsigned int  remote_is_amiga:1;

	/*
	 * ftp server doesn't understand 
	 * "LIST -la <path>"; use "CWD <path>"/
	 * "LIST" instead
	 */
	unsigned int  strict_rfc959_list_cmd;

	/* Whether we have to use passive connections */
	int use_passive_connection;
	int is_binary;
	int failed_on_login;
	
	GHashTable *dcache;

	int __inode_counter;
	
	
	int cwd_defered;  /*
			   * current_directory was changed but CWD command hasn't
			   * been sent yet
			   */
} ftpfs_connection_t;

typedef struct {
	ftpfs_connection_t *conn;
	char               *path;
} ftpfs_uri_t;

typedef struct {
	int     ref_count;
	ftpfs_connection_t *conn;
	char   *name;
	char   *linkname;
	char   *local_filename;
	char   *remote_filename;
	int     local_is_temp:1;
	int     freshly_created:1;

	struct stat local_stat;
	struct stat s;
	struct stat *l_stat;

	enum {
		LS_NOLIN,	  /* Not using linear access at all */
		LS_LINEAR_CLOSED, /* Using linear access, but not open, yet */
		LS_LINEAR_OPEN    /* Using linear access, open */
	} linear_state;
	int data_sock;		/* For linear_ operations */
} ftpfs_direntry_t;

typedef struct {
	int     ref_count;
	time_t  timeout;	/* When this directory is no longer valid */
	char   *remote_path;
	GList  *file_list;

	enum {
		FTPFS_NO_SYMLINKS,
		FTPFS_UNRESOLVED_SYMLINKS,
		FTPFS_RESOLVED_SYMLINKS,
		FTPFS_RESOLVING_SYMLINKS
	} symlink_status;
} ftpfs_dir_t;

typedef struct {
	gboolean          has_changed;
	ftpfs_direntry_t *fe;
	int               local_handle;
} ftpfs_file_handle_t;

/*
 * Represents a directory listing being read.
 *
 * Used during opendir/readdir/closedir.
 */
typedef struct {
	ftpfs_uri_t       *uri;
	ftpfs_dir_t       *dir;
	GList             *pos;	/* Position in the directory listing */

	/*
	 * Options set during opendir
	 */
	const GList *meta_keys;
	GnomeVFSFileInfoOptions  options;
	const GnomeVFSDirectoryFilter *filter;
} ftpfs_dirent_t;

/* Flags for get_file_entry */
#define FTPFS_DO_RESOLVE_SYMLINK 1
#define FTPFS_DO_OPEN            2
#define FTPFS_DO_FREE_RESOURCE   4
#define FTPFS_DO_CREAT           8
#define FTPFS_DO_TRUNC          16
#endif
