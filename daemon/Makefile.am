bin_PROGRAMS=vfs-daemon

INCLUDES= \
	$(LIBGNOMEVFS_CFLAGS)			\
	$(VFS_CFLAGS)				\
	$(MONIKERS_CFLAGS)                      \
	-D_FILE_OFFSET_BITS=64			\
	-D_BSD_SOURCE				\
	-D_GNU_SOURCE				\
	-D_LARGEFILE64_SOURCE                   \
	-D_POSIX_PTHREAD_SEMANTICS		\
	-D_REENTRANT				\
	-DG_DISABLE_DEPRECATED			\
	-DDATADIR=\"$(datadir)\"		\
	-DLIBDIR=\"$(libdir)\"			\
	-DPREFIX=\"$(prefix)\"			\
	-DGNOMEVFS_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\" \
	-DSYSCONFDIR=\"$(sysconfdir)\"		\
	-DG_LOG_DOMAIN=\"libgnomevfs\"		\
	-I$(top_srcdir)/libgnomevfs             \
	$(NULL)
vfs_daemon_LDADD=$(MONIKERS_LIBS) $(top_builddir)/libgnomevfs/libgnomevfs-2.la

vfs_daemon_SOURCES = \
	gnome-vfs-daemon.c \
	gnome-vfs-daemon.h \
	gnome-vfs-async-daemon.c \
	gnome-vfs-async-daemon.h \
	gnome-vfs-daemon-handle.h \
	gnome-vfs-daemon-handle.c \
	$(top_srcdir)/libgnomevfs/GNOME_VFS_Daemon.h		\
	$(top_srcdir)/libgnomevfs/GNOME_VFS_Daemon-common.c	\
	$(top_srcdir)/libgnomevfs/GNOME_VFS_Daemon-stubs.c	\
	$(top_srcdir)/libgnomevfs/GNOME_VFS_Daemon-skels.c

# Hack for now
BONOBO_ACTIVATION_IDL_DIR=`pkg-config --variable=idldir bonobo-activation-2.0`

idls = GNOME_VFS_Daemon.idl
idl_flags = -I $(BONOBO_ACTIVATION_IDL_DIR)

$(CORBA_SOURCE): $(idls) $(ORBIT_IDL)
	$(ORBIT_IDL) $(idl_flags) $(idls)

serverdir = $(libdir)/bonobo/servers
server_DATA = GNOME_VFS_Daemon.server

EXTRA_DIST = $(server_DATA)
