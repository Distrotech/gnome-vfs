bin_PROGRAMS=vfs-daemon

INCLUDES=$(MONIKERS_CFLAGS) -I$(top_srcdir)/libgnomevfs
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
