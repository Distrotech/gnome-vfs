SUBDIRS = extfs

INCLUDES =					\
	-I$(top_srcdir)/libgnomevfs		\
	-I$(top_builddir)/libgnomevfs		\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	$(GLIB_CFLAGS)				\
	$(XML_CFLAGS)	 			\
	$(LIBEFS_CFLAGS)			\
	$(GCONF_CFLAGS)				\
	$(GTK_CFLAGS)				\
	$(VFS_CFLAGS) $(WERROR)

CPPFLAGS =					\
	-D_GNU_SOURCE				\
	-D_REENTRANT				\
	-D_POSIX_PTHREAD_SEMANTICS		\
	-DPREFIX=\"$(prefix)\"

EXTRA_DIST =					\
	default-modules.conf			\
	efs-method.c                            \
	efs-method.h



###  Module setup

module_flags = -export_dynamic -avoid-version
modulesdir = $(libdir)/vfs/modules

modules_LTLIBRARIES =				\
	libextfs.la				\
	libfile.la				\
	libhttp.la				\
	libbzip2.la				\
	libgzip.la				\
	libftp.la				\
	libvfs-pipe.la				\
	libvfs-translate.la

modulesconfdir=$(sysconfdir)/vfs/modules
modulesconf_DATA =				\
	default-modules.conf


###  `extfs' method

libextfs_la_SOURCES = 				\
	extfs-method.c 				\
	extfs-method.h

libextfs_la_LDFLAGS = $(module_flags)


###  `file' method

libfile_la_SOURCES = 				\
	file-method.c 				\
	file-method.h

libfile_la_LDFLAGS = $(module_flags)


### `gzip' method

libgzip_la_SOURCES = 				\
	gzip-method.c 				\
	gzip-method.h
libgzip_la_LDFLAGS = $(module_flags)
libgzip_la_LIBADD = -lz


### `bzip2' method

libbzip2_la_SOURCES =				\
	bzip2-method.c				\
	bzip2-method.h
libbzip2_la_LDFLAGS = $(module_flags)
libbzip2_la_LIBADD = -lbz2


### `ftp' method

libftp_la_SOURCES = 				\
	ftp-method.c				\
	ftp-method.h

### `gconf' method

if HAVE_GCONF
gconfmoduledir = $(modulesdir)
gconfmodule_LTLIBRARIES = libgconf.la

libgconf_la_SOURCES =                           \
	gconf-method.c                          \
        gconf-method.h
libgconf_la_LIBADD = $(GCONF_LIBS) $(GTK_LIBS)
endif

libgconf_la_LDFLAGS = $(module_flags)


### `http' method

libhttp_la_SOURCES =				\
	http-method.c				\
	http-method.h
libhttp_la_LDFLAGS = $(module_flags)
libhttp_la_LIBADD  = $(GLIB_LIBS) $(XML_LIBS)

libvfs_pipe_la_SOURCES =			\
	pipe-method.c				\
	pipe-method.h
libvfs_pipe_la_LDFLAGS = $(module_flags)
libvfs_pipe_la_LIBADD  = $(GLIB_LIBS)

libvfs_translate_la_SOURCES =			\
	translate-method.c
libvfs_translate_la_LDFLAGS = $(module_flags)
libvfs_translate_la_LIBADD  = $(GLIB_LIBS)

#if HAVE_LIBEFS
#vefsmoduledir = $(modulesdir)
#vefsmodule_LTLIBRARIES = libvefs.la
#
#libvefs_la_SOURCES =	\
#	efs-method.c	\
#        efs-method.h
#libvefs_la_LIBADD = $(LIBEFS_LIBS) $(GTK_LIBS)
#endif
