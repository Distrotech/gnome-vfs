INCLUDES = \
	-I..					\
	-I$(srcdir)/..				\
	$(WARN_CFLAGS)				\
	$(BONOBO_CFLAGS)			\
	-Wno-unused				\
	-DVERSION=\""$(VERSION)"\"		\
	-DG_LOG_DOMAIN=\"GnomeMonikers\"

EXTRA_DIST = \
	GNOME_VFS_Moniker_std.oaf.in.in

moniker_LTLIBRARIES = libmoniker_gnome_vfs_std.la
monikerdir = $(libdir)/bonobo/monikers

GNOME_VFS_Moniker_std.oaf.in : $(srcdir)/GNOME_VFS_Moniker_std.oaf.in.in
	sed -e "s|\@MONIKER_LIBDIR\@|$(monikerdir)|" \
	 $(srcdir)/GNOME_VFS_Moniker_std.oaf.in.in > GNOME_VFS_Moniker_std.oaf.in

CLEANFILES+=GNOME_VFS_Moniker_std.oaf GNOME_VFS_Moniker_std.oaf.in

oafdir = $(datadir)/oaf
oaf_DATA = GNOME_VFS_Moniker_std.oaf

@XML_I18N_MERGE_OAF_RULE@

clean-local:
	-rm -f $(oaf_DATA)

libmoniker_gnome_vfs_std_la_SOURCES = \
	gnome-moniker-std.c		\
	gnome-moniker-std.h		\
	bonobo-stream-fs.h		\
	bonobo-stream-fs.c		\
	bonobo-storage-fs.h		\
	bonobo-storage-fs.c		\
	bonobo-moniker-extender-file.c	\
	bonobo-moniker-file.c		\
	bonobo-stream-vfs.h		\
	bonobo-stream-vfs.c		\
	bonobo-storage-vfs.h		\
	bonobo-storage-vfs.c		\
	bonobo-moniker-vfs.c

libmoniker_gnome_vfs_std_la_LIBADD = \
	$(BONOBO_LIBS)