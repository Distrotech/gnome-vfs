NULL = 

INCLUDES =					\
	-DGNOME_VFS_CONFDIR=\"$(sysconfdir)\"	\
	-DGNOME_VFS_DATADIR=\"$(datadir)\"	\
	-DGNOME_VFS_PREFIX=\"$(prefix)\"	\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)			\
	-I$(top_builddir)/intl			\
	-I$(top_builddir)/libgnomevfs		\
	$(BONOBO_CFLAGS)			\
	$(GCONF_CFLAGS) 			\
	$(GLIB_CFLAGS)				\
	$(BONOBO_ACTIVATION__CFLAGS)		\
        $(OPENSSL_CFLAGS)                       \
	$(ORBIT_CFLAGS)				\
	$(VFS_CFLAGS)				\
	$(XML_CFLAGS)				\
	-D_GNU_SOURCE				\
	-D_REENTRANT				\
	-D_POSIX_PTHREAD_SEMANTICS		\
	-DLIBDIR=\"$(libdir)\"			\
	-DSYSCONFDIR=\"$(sysconfdir)\"		\
	-DG_LOG_DOMAIN=\"GnomeVFS\"		\
	$(NULL)

CORBA_SOURCE =					\
	gnome-vfs-metafile-server.h		\
	gnome-vfs-metafile-server-common.c	\
	gnome-vfs-metafile-server-stubs.c	\
	gnome-vfs-metafile-server-skels.c	\
	$(NULL)

idls = 								\
	$(top_srcdir)/idl/gnome-vfs-metafile-server.idl		\
	$(NULL)

idl_flags = -I @BONOBO_IDLDIR@ -D__GNOME_COMPILATION

$(CORBA_SOURCE): $(idls) $(ORBIT_IDL)
	$(ORBIT_IDL) $(top_srcdir)/idl/gnome-vfs-metafile-server.idl -I$(top_srcdir)/idl $(idl_flags)

lib_LTLIBRARIES = libgnomevfs.la

EXTRA_DIST =					\
	getdelim.c

libgnomevfs_la_LIBADD =				\
	$(BONOBO_LIBS)				\
	$(GCONF_LIBS) 				\
	$(GLIB_LIBS)				\
	$(BONOBO_ACTIVATION_LIBS)		\
	$(OPENSSL_LDFLAGS) 			\
	$(ORBIT_LIBS)				\
	$(XML_LIBS)				\
	$(NULL)

libgnomevfsincludedir = $(includedir)/libgnomevfs

libgnomevfsinclude_HEADERS =			\
	gnome-vfs-app-context.h			\
	gnome-vfs-application-registry.h	\
	gnome-vfs-async-ops.h			\
	gnome-vfs-backend.h			\
	gnome-vfs-callbacks.h			\
	gnome-vfs-cancellable-ops.h		\
	gnome-vfs-cancellation.h		\
	gnome-vfs-configuration.h		\
	gnome-vfs-context.h			\
	gnome-vfs-directory-filter.h		\
	gnome-vfs-directory.h			\
	gnome-vfs-file-info.h			\
	gnome-vfs-file-size.h			\
	gnome-vfs-find-directory.h		\
	gnome-vfs-handle.h			\
	gnome-vfs-helpers.h			\
	gnome-vfs-inet-connection.h		\
	gnome-vfs-init.h			\
	gnome-vfs-iobuf.h			\
	gnome-vfs-method.h			\
	gnome-vfs-mime-handlers.h		\
	gnome-vfs-mime-info.h			\
	gnome-vfs-mime-magic.h			\
	gnome-vfs-mime-monitor.h		\
	gnome-vfs-mime-sniff-buffer.h		\
	gnome-vfs-mime.h			\
	gnome-vfs-module-api.h			\
	gnome-vfs-module-shared.h		\
	gnome-vfs-module.h			\
	gnome-vfs-ops.h				\
	gnome-vfs-parse-ls.h			\
	gnome-vfs-private-utils.h		\
	gnome-vfs-private.h			\
	gnome-vfs-regexp-filter.h		\
	gnome-vfs-result.h			\
	gnome-vfs-seekable.h			\
	gnome-vfs-shellpattern-filter.h		\
	gnome-vfs-socket-buffer.h		\
	gnome-vfs-socket.h			\
	gnome-vfs-ssl-private.h			\
	gnome-vfs-ssl.h				\
	gnome-vfs-standard-callbacks.h		\
	gnome-vfs-transform.h			\
	gnome-vfs-types.h			\
	gnome-vfs-uri.h				\
	gnome-vfs-utils.h			\
	gnome-vfs-xfer.h			\
	gnome-vfs.h				\
	$(NULL)

libgnomevfs_la_SOURCES =			\
	$(CORBA_SOURCE)				\
	eel-cut-n-paste.c			\
	stolen-glib-extensions.c			\
	fnmatch.c				\
	gnome-vfs-app-context.c			\
	gnome-vfs-application-registry.c	\
	gnome-vfs-async-ops.c			\
	gnome-vfs-backend.c			\
	gnome-vfs-callbacks.c			\
	gnome-vfs-cancellable-ops.c	 	\
	gnome-vfs-cancellation.c		\
	gnome-vfs-configuration.c		\
	gnome-vfs-context.c			\
	gnome-vfs-directory-filter.c		\
	gnome-vfs-directory.c			\
	gnome-vfs-file-info.c			\
	gnome-vfs-find-directory.c		\
	gnome-vfs-handle.c			\
	gnome-vfs-helpers.c			\
	gnome-vfs-i18n.c			\
	gnome-vfs-inet-connection.c		\
	gnome-vfs-init.c			\
	gnome-vfs-iobuf.c			\
	gnome-vfs-metafile-backend.c		\
	gnome-vfs-method.c			\
	gnome-vfs-mime-handlers.c               \
	gnome-vfs-mime-info.c			\
	gnome-vfs-mime-magic.c			\
	gnome-vfs-mime-monitor.c		\
	gnome-vfs-mime-sniff-buffer.c		\
	gnome-vfs-mime.c			\
	gnome-vfs-module-api.c			\
	gnome-vfs-module-shared.c		\
	gnome-vfs-module.c			\
	gnome-vfs-ops.c				\
	gnome-vfs-parse-ls.c			\
	gnome-vfs-private-utils.c		\
	gnome-vfs-private.c			\
	gnome-vfs-process.c			\
	gnome-vfs-regexp-filter.c		\
	gnome-vfs-result.c			\
	gnome-vfs-seekable.c			\
	gnome-vfs-shellpattern-filter.c		\
	gnome-vfs-socket-buffer.c		\
	gnome-vfs-socket.c			\
	gnome-vfs-ssl.c				\
	gnome-vfs-transform.c			\
	gnome-vfs-uri.c				\
	gnome-vfs-utils.c			\
	gnome-vfs-xfer.c			\
	fnmatch.h				\
	gnome-vfs-mime-private.h		\
	gnome-vfs-mime-sniff-buffer-private.h	\
	gnome-vfs-process.h			\
	$(NULL)

noinst_HEADERS =				\
	gnome-vfs-backend-private.h

BUILT_SOURCES = 		\
	$(CORBA_SOURCE)		\
	$(NULL)

# Platform specific includes
libgnomevfsplatofrmincludedir = $(libdir)/vfs/include
libgnomevfsplatofrminclude_HEADERS =			\
	gnome-vfs-file-size.h
