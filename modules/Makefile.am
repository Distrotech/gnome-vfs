NULL =
SUBDIRS = extfs

INCLUDES =					\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(MODULES_XML_GCONF_CFLAGS)		\
	$(MODULES_FILE_CFLAGS) 			\
	$(LIBEFS_CFLAGS)			\
	$(VFS_CFLAGS)				\
	-D_GNU_SOURCE				\
	-D_REENTRANT				\
	-D_POSIX_PTHREAD_SEMANTICS		\
	-DG_DISABLE_DEPRECATED			\
	-DDATADIR=\"$(datadir)\"		\
	-DSYSCONFDIR=\"$(sysconfdir)\"		\
	-DPREFIX=\"$(prefix)\"

EXTRA_DIST =					\
	default-modules.conf			\
	cdda-module.conf			\
	ssl-modules.conf

###  Module setup

if HAVE_CDDA
CDDA_LTLIBS = libcdda.la
else
CDDA_LTLIBS =
endif

module_flags = -export_dynamic -avoid-version
modulesdir = $(libdir)/gnome-vfs-2.0/modules

modules_LTLIBRARIES =				\
	$(CDDA_LTLIBS)				\
	libdesktop.la				\
	libextfs.la				\
	libfile.la				\
	libvfs-test.la				\
	$(HTTP_LTLIBS)				\
	libbzip2.la				\
	libgzip.la				\
	libftp.la				\
	libvfs-pipe.la				\
	libvfs-translate.la			\
	libssh.la				\
	$(NULL)

# Not currently supported
#	libnfs.la

modulesconfdir=$(sysconfdir)/gnome-vfs-2.0/modules
if HAVE_CDDA
CDDA_CONF = cdda-module.conf
endif
if HAVE_SSL
SSL_CONF = ssl-modules.conf
endif
modulesconf_DATA = default-modules.conf $(CDDA_CONF) $(SSL_CONF)

###  `cdda' method

libcdda_la_SOURCES = \
	cdda-method.c 				\
	cdda-cddb.c 				\
	cdda-cddb.h				\
	cdda-cdrom-extensions.h

libcdda_la_LDFLAGS = $(module_flags)
libcdda_la_LIBADD = -L/gnome/lib -lcdda_paranoia -lcdda_interface

###  `extfs' method

libextfs_la_SOURCES = extfs-method.c
libextfs_la_LDFLAGS = $(module_flags)

###  `file' method

libfile_la_SOURCES = file-method.c
libfile_la_LDFLAGS = $(module_flags)
libfile_la_LIBADD = $(FAM_LIBS) $(MODULES_FILE_LIBS)

###  `desktop' method

libdesktop_la_SOURCES = desktop-method.c
libdesktop_la_LDFLAGS = $(module_flags)

###  `ssh' method

libssh_la_SOURCES = ssh-method.c
libssh_la_LDFLAGS = $(module_flags)

###  `test' method

libvfs_test_la_SOURCES = test-method.c
libvfs_test_la_LDFLAGS = $(module_flags)
libvfs_test_la_LIBADD = $(MODULES_XML_LIBS)

### `gzip' method

libgzip_la_SOURCES = gzip-method.c
libgzip_la_LDFLAGS = $(module_flags)
libgzip_la_LIBADD = -lz

### `bzip2' method

libbzip2_la_SOURCES = bzip2-method.c
libbzip2_la_LDFLAGS = $(module_flags)
libbzip2_la_LIBADD = -lbz2

### `ftp' method

libftp_la_SOURCES = ftp-method.c
libftp_la_LDFLAGS = $(module_flags)

### `gconf' method

gconfmoduledir = $(modulesdir)
gconfmodule_LTLIBRARIES = libgconf.la

libgconf_la_SOURCES = gconf-method.c
libgconf_la_LIBADD = $(MODULES_GCONF_LIBS)
libgconf_la_LDFLAGS = $(module_flags)

### `nfs' method

#libnfs_la_SOURCES = 				\
#	nfs-method.c				\
#	nfs-method.h				\
#	nfs-method_mount_xdr.c			\
#	nfs-method_nfs_prot_xdr.c		\
#	nfs-method_mount.h			\
#	nfs-method_nfs_prot.h
#libnfs_la_LDFLAGS = $(module_flags)

### `http' method

HTTP_LTLIBS = libhttp.la

libhttp_la_SOURCES =				\
	http-method.c				\
	http-method.h				\
	http-authn.c				\
	http-authn.h				\
	http-cache.c				\
	http-cache.h				\
	$(NULL)
libhttp_la_LDFLAGS = $(module_flags)
libhttp_la_LIBADD  = $(MODULES_GCONF_LIBS)

libvfs_pipe_la_SOURCES = pipe-method.c
libvfs_pipe_la_LDFLAGS = $(module_flags)
libvfs_pipe_la_LIBADD  = $(MODULES_LIBS)

libvfs_translate_la_SOURCES = translate-method.c
libvfs_translate_la_LDFLAGS = $(module_flags)
libvfs_translate_la_LIBADD  = $(MODULES_LIBS)

#if HAVE_LIBEFS
#vefsmoduledir = $(modulesdir)
#vefsmodule_LTLIBRARIES = libvefs.la
#
#libvefs_la_SOURCES =	\
#	efs-method.c
#libvefs_la_LIBADD = $(LIBEFS_LIBS) $(MODULES_LIBS)
#endif
