INCLUDES =					\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	$(GNOME_INCLUDEDIR)

CPPFLAGS =					\
	-D_REENTRANT				\
	-DPREFIX=\"$(prefix)\"			\
	-DG_LOG_DOMAIN=\"GnomeVFS\"

lib_LTLIBRARIES = libgnomevfs.la

libgnomevfs_la_LIBADD =				\
	$(GLIB_LIBS)

libgnomevfsincludedir = $(includedir)/libgnomevfs
libgnomevfsinclude_HEADERS =			\
	gnome-vfs-async-ops.h			\
	gnome-vfs-constants.h			\
	gnome-vfs-directory.h			\
	gnome-vfs-directory-filter.h		\
	gnome-vfs-directory-list.h		\
	gnome-vfs-file-info.h			\
	gnome-vfs-init.h			\
	gnome-vfs-xfer.h			\
	gnome-vfs-ops.h				\
	gnome-vfs-result.h			\
	gnome-vfs-types.h			\
	gnome-vfs-uri.h				\
	gnome-vfs.h

noinst_HEADERS =				\
	gnome-vfs-handle.h			\
	gnome-vfs-list-sort.h			\
	gnome-vfs-method.h			\
	gnome-vfs-module.h			\
	gnome-vfs-private-types.h		\
	gnome-vfs-private.h			\
	gnome-vfs-process.h			\
	gnome-vfs-regexp-filter.h		\
	gnome-vfs-seekable.h			\
	gnome-vfs-shellpattern-filter.h		\
	gnome-vfs-utils.h			\
	fnmatch.h

libgnomevfs_la_SOURCES =			\
	gnome-vfs-directory.c			\
	gnome-vfs-directory-filter.c		\
	gnome-vfs-directory-list.c		\
	gnome-vfs-handle.c			\
	gnome-vfs-file-info.c			\
	gnome-vfs-init.c			\
	gnome-vfs-list-sort.c			\
	gnome-vfs-method.c			\
	gnome-vfs-xfer.c			\
	gnome-vfs-ops.c				\
	gnome-vfs-process.c			\
	gnome-vfs-result.c			\
	gnome-vfs-regexp-filter.c		\
	gnome-vfs-seekable.c			\
	gnome-vfs-shellpattern-filter.c		\
	gnome-vfs-uri.c				\
	gnome-vfs-utils.c			\
	fnmatch.c
