@XML_I18N_MERGE_KEYS_RULE@

KEYS_IN_FILES = gnome-vfs.keys.in
KEYS_FILES = $(KEYS_IN_FILES:.keys.in=.keys)

NULL=

mimedir = $(datadir)/mime-info

mime_DATA = \
	$(KEYS_FILES) \
	gnome-vfs.mime \
	$(NULL)

applicationsdir = $(datadir)/application-registry

applications_DATA = \
	gnome-vfs.applications \
	$(NULL)

sysconf_DATA = gnome-vfs-mime-magic

EXTRA_DIST = $(mime_DATA) $(sysconf_DATA) $(applications_DATA) $(KEYS_IN_FILES) $(KEYS_FILES)

#install-data-local:
#	$(top_builddir)/libgnomevfs/gnome-vfs-gen-mimedb $(DESTDIR)$(sysconfdir)/gnome-vfs-mime-magic
