noinst_LTLIBRARIES = libgnomevfs-pthread.la

INCLUDES =					\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(LIBGNOMEVFS_CFLAGS)			\
	$(VFS_CFLAGS)				\
	-D_GNU_SOURCE				\
	-D_REENTRANT				\
	-DG_DISABLE_DEPRECATED			\
	-DPREFIX=\"$(prefix)\"			\
	-DG_LOG_DOMAIN=\"libgnomevfs\"

libgnomevfs_pthread_la_SOURCES =		\
	gnome-vfs-async-job-map.c		\
	gnome-vfs-async-job-map.h		\
	gnome-vfs-async-ops.c			\
	gnome-vfs-job.c				\
	gnome-vfs-job.h				\
	gnome-vfs-job-slave.c			\
	gnome-vfs-job-slave.h			\
	gnome-vfs-pthread.c			\
	gnome-vfs-pthread.h			\
	gnome-vfs-job-queue.c			\
	gnome-vfs-job-queue.h			\
	gnome-vfs-thread-pool.c			\
	gnome-vfs-thread-pool.h
