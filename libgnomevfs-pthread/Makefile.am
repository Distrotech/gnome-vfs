lib_LTLIBRARIES = libgnomevfs-pthread.la

INCLUDES =					\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(LIBGNOMEVFS_CFLAGS)			\
	$(VFS_CFLAGS)				\
	-D_GNU_SOURCE				\
	-D_REENTRANT				\
	-DG_DISABLE_DEPRECATED			\
	-DPREFIX=\"$(prefix)\"			\
	-DG_LOG_DOMAIN=\"GnomeVFS-pthread\"

noinst_HEADERS = 				\
	gnome-vfs-job-slave.h			\
	gnome-vfs-job.h				\
	gnome-vfs-async-job-map.h		\
	gnome-vfs-thread-pool.h			\
	gnome-vfs-pthread.h

libgnomevfs_pthread_la_SOURCES =		\
	gnome-vfs-pthread.c			\
	gnome-vfs-thread-pool.c			\
	gnome-vfs-async-job-map.c		\
	gnome-vfs-async-ops.c			\
	gnome-vfs-job-slave.c			\
	gnome-vfs-job.c
