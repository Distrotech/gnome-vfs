INCLUDES= \
	$(LIBGNOMEVFS_CFLAGS)			\
	$(LIBGNOMEVFSDAEMON_CFLAGS)		\
	$(VFS_CFLAGS)				\
	$(MONIKERS_CFLAGS)                      \
	-D_FILE_OFFSET_BITS=64			\
	-D_BSD_SOURCE				\
	-D_LARGEFILE64_SOURCE                   \
	-D_POSIX_PTHREAD_SEMANTICS		\
	-D_REENTRANT				\
	-DG_DISABLE_DEPRECATED			\
	-DGNOME_VFS_PREFIX=\"$(prefix)\"	\
	-DGNOME_VFS_DATADIR=\"$(datadir)\"	\
	-DGNOME_VFS_LIBDIR=\"$(libdir)\"	\
	-DGNOMEVFS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\" \
	-DGNOME_VFS_SYSCONFDIR=\"$(sysconfdir)\"\
	-DG_LOG_DOMAIN=\"libgnomevfs\"		\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	-I$(top_srcdir)/libgnomevfs             \
	-I$(top_builddir)/libgnomevfs           \
	$(NULL)

if OS_WIN32
else
libexec_PROGRAMS=gnome-vfs-daemon
endif

gnome_vfs_daemon_LDADD=$(MONIKERS_LIBS) $(LIBGNOMEVFSDAEMON_LIBS) $(top_builddir)/libgnomevfs/libgnomevfs-2.la $(top_builddir)/libgnomevfs/libgnomevfsdaemon-2.la

CORBA_SOURCE = \
	$(top_builddir)/libgnomevfs/GNOME_VFS_Daemon.h		\
	$(top_builddir)/libgnomevfs/GNOME_VFS_Daemon-common.c	\
	$(top_builddir)/libgnomevfs/GNOME_VFS_Daemon-stubs.c	\
	$(top_builddir)/libgnomevfs/GNOME_VFS_Daemon-skels.c


gnome_vfs_daemon_SOURCES = \
	gnome-vfs-daemon.c \
	gnome-vfs-daemon.h \
	gnome-vfs-async-daemon.c \
	gnome-vfs-async-daemon.h \
	gnome-vfs-daemon-dir-handle.h \
	gnome-vfs-daemon-dir-handle.c \
	gnome-vfs-daemon-handle.h \
	gnome-vfs-daemon-handle.c \
	$(CORBA_SOURCE)

BUILT_SOURCES = $(CORBA_SOURCE)

serverfile = GNOME_VFS_Daemon.server.in
serverdir = $(libdir)/bonobo/servers

install-data-hook :
	$(mkinstalldirs) $(DESTDIR)$(serverdir)
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $(srcdir)/$(serverfile) > $(DESTDIR)$(serverdir)/$(serverfile:.in=)


EXTRA_DIST = $(serverfile)

# We don't want the corba files in the tarball, but we can't delete
# them until now, because this directory also needs them during make dist
dist-hook:
	cd $(distdir)/../libgnomevfs/ ; rm -f $(CORBA_SOURCE)

