/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-job.h - Jobs for asynchronous operation of the GNOME
   Virtual File System (version for POSIX threads).

   Copyright (C) 1999 Free Software Foundation

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Ettore Perazzoli <ettore@gnu.org> */

#ifndef GNOME_VFS_JOB_PTHREAD_H
#define GNOME_VFS_JOB_PTHREAD_H

typedef struct GnomeVFSJob GnomeVFSJob;

#include "gnome-vfs-job-slave.h"

enum GnomeVFSJobType {
	GNOME_VFS_JOB_OPEN,
	GNOME_VFS_JOB_OPEN_AS_CHANNEL,
	GNOME_VFS_JOB_CREATE,
	GNOME_VFS_JOB_CREATE_AS_CHANNEL,
	GNOME_VFS_JOB_CLOSE,
	GNOME_VFS_JOB_READ,
	GNOME_VFS_JOB_WRITE,
	GNOME_VFS_JOB_LOAD_DIRECTORY,
	GNOME_VFS_JOB_XFER,
	GNOME_VFS_JOB_GET_FILE_INFO,
};
typedef enum GnomeVFSJobType GnomeVFSJobType;

struct GnomeVFSOpenJob {
	struct {
		GnomeVFSURI *uri;
		GnomeVFSOpenMode open_mode;
	} request;

	struct {
		GnomeVFSResult result;
	} notify;
};
typedef struct GnomeVFSOpenJob GnomeVFSOpenJob;

struct GnomeVFSOpenAsChannelJob {
	struct {
		GnomeVFSURI *uri;
		GnomeVFSOpenMode open_mode;
		guint advised_block_size;
	} request;

	struct {
		GnomeVFSResult result;
		GIOChannel *channel;
	} notify;
};
typedef struct GnomeVFSOpenAsChannelJob GnomeVFSOpenAsChannelJob;

struct GnomeVFSCreateJob {
	struct {
		GnomeVFSURI *uri;
		GnomeVFSOpenMode open_mode;
		gboolean exclusive;
		guint perm;
	} request;

	struct {
		GnomeVFSResult result;
	} notify;
};
typedef struct GnomeVFSCreateJob GnomeVFSCreateJob;

struct GnomeVFSCreateAsChannelJob {
	struct {
		GnomeVFSURI *uri;
		GnomeVFSOpenMode open_mode;
		gboolean exclusive;
		guint perm;
	} request;

	struct {
		GnomeVFSResult result;
		GIOChannel *channel;
	} notify;
};
typedef struct GnomeVFSCreateAsChannelJob GnomeVFSCreateAsChannelJob;

struct GnomeVFSCloseJob {
	struct {
	} request;

	struct {
		GnomeVFSResult result;
	} notify;
};
typedef struct GnomeVFSCloseJob GnomeVFSCloseJob;

struct GnomeVFSReadJob {
	struct {
		GnomeVFSFileSize num_bytes;
		gpointer buffer;
	} request;

	struct {
		GnomeVFSResult result;
		GnomeVFSFileSize bytes_read;
	} notify;
};
typedef struct GnomeVFSReadJob GnomeVFSReadJob;

struct GnomeVFSWriteJob {
	struct {
		GnomeVFSFileSize num_bytes;
		gconstpointer buffer;
	} request;

	struct {
		GnomeVFSResult result;
		GnomeVFSFileSize bytes_written;
	} notify;
};
typedef struct GnomeVFSWriteJob GnomeVFSWriteJob;

typedef struct {
	struct {
		GList *uris; /* GnomeVFSURI* */
		GnomeVFSFileInfoOptions options;
		gchar **meta_keys;
	} request;

	struct {
		GList *result_list; /* GnomeVFSGetFileInfoResult* */
	} notify;
} GnomeVFSGetFileInfoJob;



/* "Complex operations.  */

struct GnomeVFSLoadDirectoryJob {
	struct {
		GnomeVFSURI *uri;
		GnomeVFSFileInfoOptions options;
		gchar **meta_keys;
		GnomeVFSDirectorySortRule *sort_rules;
		gboolean reverse_order;
		GnomeVFSDirectoryFilterType filter_type;
		GnomeVFSDirectoryFilterOptions filter_options;
		gchar *filter_pattern;
		guint items_per_notification;
	} request;

	struct {
		GnomeVFSResult result;
		GnomeVFSDirectoryList *list;
		guint entries_read;
	} notify;
};
typedef struct GnomeVFSLoadDirectoryJob GnomeVFSLoadDirectoryJob;

struct GnomeVFSXferJob {
	struct {
		gchar *source_directory_uri;
		GList *source_name_list;
		gchar *target_directory_uri;
		GList *target_name_list;
		GnomeVFSXferOptions xfer_options;
		GnomeVFSXferErrorMode error_mode;
		GnomeVFSXferOverwriteMode overwrite_mode;
		GnomeVFSXferProgressCallback progress_sync_callback;
		gpointer sync_callback_data;
	} request;

	struct {
		GnomeVFSXferProgressInfo *progress_info;
	} notify;

	struct {
		gint value;
	} notify_answer;
};
typedef struct GnomeVFSXferJob GnomeVFSXferJob;


/* FIXME: Move private stuff.  */
struct GnomeVFSJob {
	/* The slave thread that executes jobs (see module
           `gnome-vfs-job-slave.c'). */
	GnomeVFSJobSlave *slave;

	/* Handle being used for file access.  */
	GnomeVFSHandle *handle;

	/* Global lock for accessing data.  */
	GMutex *access_lock;

	/* Condition that is raised when a new job has been prepared.  As
           `GnomeVFSJob' can hold one job at a given time, the way to set up a
           new job is as follows: (a) lock `access_lock' (b) write job
           information into the struct (c) signal `execution_condition' (d)
           unlock `access_lock'.  */
	GCond *execution_condition;

	/* This condition is signalled when the master thread gets a
           notification and wants to acknowledge it.  */
	GCond *notify_ack_condition;

	/* Mutex associated with `notify_ack_condition'.  We cannot just use
           `access_lock', because we want to keep the lock in the slave thread
           until the job is really finished.  */
	GMutex *notify_ack_lock;

	/* Whether this struct is ready for containing a new job for
	   execution.  */
	gboolean is_empty;

	/* I/O channels used to wake up the master thread.  When the slave
           thread wants to notify the master thread that an operation has been
           done, it writes a character into `wakeup_channel_in' and the master
           thread detects this in the GLIB main loop by using a watch.  */
	GIOChannel *wakeup_channel_in;
	GIOChannel *wakeup_channel_out;

	/* Channel mutex to prevent more than one notification to be queued
           into the channel.  */
	GMutex *wakeup_channel_lock;

	/* ID of the job (e.g. open, create, close...).  */
	GnomeVFSJobType type;

	/* Whether this job wants the notification acknowledged.  */
	gboolean want_notify_ack;

	/* Pointer to the callback for this job.  */
	gpointer callback;

	/* Extra parameter to pass to the callback for this job.  */
	gpointer callback_data;

	/* Context.  */
	GnomeVFSContext *context;

	/* Job-specific information.  */
	union {
		GnomeVFSOpenJob open;
		GnomeVFSOpenAsChannelJob open_as_channel;
		GnomeVFSCreateJob create;
		GnomeVFSCreateAsChannelJob create_as_channel;
		GnomeVFSCloseJob close;
		GnomeVFSReadJob read;
		GnomeVFSWriteJob write;
		GnomeVFSLoadDirectoryJob load_directory;
		GnomeVFSXferJob xfer;
		GnomeVFSGetFileInfoJob get_file_info;
	} info;
};


GnomeVFSJob	*gnome_vfs_job_new	(void);
gboolean	 gnome_vfs_job_destroy	(GnomeVFSJob *job);
void		 gnome_vfs_job_prepare	(GnomeVFSJob *job);
void		 gnome_vfs_job_go	(GnomeVFSJob *job);
gboolean	 gnome_vfs_job_execute	(GnomeVFSJob *job);
GnomeVFSResult	 gnome_vfs_job_cancel	(GnomeVFSJob *job);

#endif /* GNOME_VFS_JOB_PTHREAD_H */
