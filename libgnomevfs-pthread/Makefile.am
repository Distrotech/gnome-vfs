lib_LTLIBRARIES = libgnomevfs-pthread.la

INCLUDES =					\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	-I$(top_srcdir)/libgnomevfs		\
	$(GNOME_INCLUDEDIR)

CPPFLAGS =					\
	-D_REENTRANT				\
	-DPREFIX=\"$(prefix)\"			\
	-DG_LOG_DOMAIN=\"GnomeVFS-pthread\"

noinst_HEADERS = 				\
	gnome-vfs-job-slave.h			\
	gnome-vfs-job.h

libgnomevfs_pthread_la_SOURCES =		\
	gnome-vfs-async-ops.c			\
	gnome-vfs-job-slave.c			\
	gnome-vfs-job.c
