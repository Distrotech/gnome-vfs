NULL=

mimedir = $(datadir)/mime-info

mime_DATA = \
	gnome-vfs.mime \
	gnome-vfs.keys \
	$(NULL)

sysconf_DATA = gnome-vfs-mime-magic

EXTRA_DIST = $(mime_DATA) $(sysconf_DATA)

install-data-local:
	$(top_builddir)/libgnomevfs/gnome-vfs-gen-mimedb $(DESTDIR)$(sysconfdir)/gnome-vfs-mime-magic
