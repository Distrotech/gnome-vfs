lib_LTLIBRARIES = libgnomevfs-corba.la

INCLUDES =					\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	-I$(top_srcdir)/libgnomevfs		\
	-I$(top_builddir)/libgnomevfs-corba	\
	$(GNOME_INCLUDEDIR)

CPPFLAGS =					\
	-D_REENTRANT				\
	-DPREFIX=\"$(prefix)\"			\
	-DG_LOG_DOMAIN=\"GnomeVFS-CORBA\"

ORBIT_IDL = orbit-idl

IDL_FLAGS =

gnome_vfs_slave_built =				\
	gnome-vfs-slave.h			\
	gnome-vfs-slave-skels.c			\
	gnome-vfs-slave-stubs.c			\
	gnome-vfs-slave-common.c

$(gnome_vfs_slave_built): $(top_srcdir)/idl/gnome-vfs-slave.idl
	$(ORBIT_IDL) $(IDL_FLAGS) $(top_srcdir)/idl/gnome-vfs-slave.idl

noinst_HEADERS =				\
	gnome-vfs-corba.h			\
	gnome-vfs-slave-launch.h		\
	gnome-vfs-slave-notify.h		\
	gnome-vfs-slave-process.h

libgnomevfs_corba_la_SOURCES =			\
	gnome-vfs-async-ops.c			\
	gnome-vfs-corba.c			\
	gnome-vfs-slave-launch.c		\
	gnome-vfs-slave-notify.c		\
	gnome-vfs-slave-process.c		\
	$(gnome_vfs_slave_built)

gnome_vfs_slave_SOURCES =			\
	gnome-vfs-slave.c			\
	$(gnome_vfs_slave_built)
gnome_vfs_slave_LDADD =					\
	$(top_builddir)/libgnomevfs/libgnomevfs.la	\
	libgnomevfs-corba.la				\
	$(GNOME_LIBDIR)					\
	$(GNOMEUI_LIBS)					\
	$(GNOMEGNORBA_LIBS)

bin_PROGRAMS =					\
	gnome-vfs-slave

BUILT_SOURCES = $(gnome_vfs_slave_built)
