NULL=

keys_in_files = gnome-vfs.keys.in
keys_files = $(keys_in_files:.keys.in=.keys)
@INTLTOOL_KEYS_RULE@

mimedir = $(datadir)/mime-info
mime_DATA = $(keys_files) gnome-vfs.mime

applicationsdir = $(datadir)/application-registry
applications_DATA = gnome-vfs.applications

sysconf_DATA = gnome-vfs-mime-magic

EXTRA_DIST = $(keys_in_files) $(mime_DATA) $(sysconf_DATA) $(applications_DATA)
