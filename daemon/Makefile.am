bin_PROGRAMS=vfs-daemon 
noinst_PROGRAMS=test-authentication
noinst_LTLIBRARIES=libauthentication.la

INCLUDES=$(MONIKERS_CFLAGS)
LIBS=$(MONIKERS_LIBS) -lgnomevfs-2
#LIBS=$(MONIKERS_LIBS) -lgnomevfs-2 $(top_builddir)/mime/libmime.la

test_authentication_SOURCES = \
	test-authentication.c 

test_authentication_LDADD = libauthentication.la

libauthentication_la_SOURCES =         \
	gnome-vfs-client.c             \
	gnome-vfs-client.h             \
	gnome-vfs-authentication.c     \
	gnome-vfs-authentication.h     \
	$(CORBA_SOURCE)

vfs_daemon_SOURCES = \
	gnome-vfs-daemon.c \
	gnome-vfs-daemon.h \
	$(CORBA_SOURCE)

$(vfs_daemon_OBJECTS): GNOME_VFS_Daemon.h
$(test_authentication_OBJECTS): GNOME_VFS_Daemon.h

CORBA_SOURCE = \
	GNOME_VFS_Daemon.h		\
	GNOME_VFS_Daemon-common.c	\
	GNOME_VFS_Daemon-stubs.c	\
	GNOME_VFS_Daemon-skels.c

# Hack for now
BONOBO_ACTIVATION_IDL_DIR=`pkg-config --variable=idldir bonobo-activation-2.0`

idls = GNOME_VFS_Daemon.idl
idl_flags = -I $(BONOBO_ACTIVATION_IDL_DIR)

$(CORBA_SOURCE): $(idls) $(ORBIT_IDL)
	$(ORBIT_IDL) $(idl_flags) $(idls)

serverdir = $(libdir)/bonobo/servers
server_DATA = GNOME_VFS_Daemon.server

EXTRA_DIST = $(idls) $(server_DATA)

DONT_DIST_SOURCE = $(CORBA_SOURCE)

dist-hook:
	for file in $(DONT_DIST_SOURCE) ; do \
	    rm -f $(distdir)/$$file ; \
	done

