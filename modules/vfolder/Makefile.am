NULL =

INCLUDES =					\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(MODULES_XML_GCONF_CFLAGS)		\
	$(MODULES_FILE_CFLAGS) 			\
	$(LIBEFS_CFLAGS)			\
	$(VFS_CFLAGS)				\
	-D_FILE_OFFSET_BITS=64			\
	-D_BSD_SOURCE				\
	-D_GNU_SOURCE				\
	-D_LARGEFILE64_SOURCE			\
	-D_POSIX_PTHREAD_SEMANTICS		\
	-D_REENTRANT				\
	-DG_DISABLE_DEPRECATED			\
	-DDATADIR=\"$(datadir)\"		\
	-DPREFIX=\"$(prefix)\"			\
	-DSYSCONFDIR=\"$(sysconfdir)\"		\
	-DG_LOG_DOMAIN=\"gnome-vfs-modules\"	\
	$(TESTS_INCLUDES)			\
	$(NULL)

EXTRA_DIST =					\
	applications.vfolder-info.in		\
	preferences.vfolder-info.in		\
	test-vfolder.vfolder-info.in		\
	test-vfolder-parent.vfolder-info.in	\
	test-vfolder-modules.conf

###  Module setup

module_flags = -export_dynamic -avoid-version
modulesdir = $(libdir)/gnome-vfs-2.0/modules

modules_LTLIBRARIES = libvfolder-desktop.la

###  `vfolder-desktop' method

libvfolder_desktop_la_SOURCES = \
	vfolder-info.c   	\
	vfolder-util.c   	\
	vfolder-util.h   	\
	vfolder-common.c 	\
	vfolder-common.h 	\
	vfolder-method.c
libvfolder_desktop_la_LDFLAGS = $(module_flags)
libvfolder_desktop_la_LIBADD = $(top_builddir)/libgnomevfs/libgnomevfs-2.la

###  Default .vfolder-info files

vfolderdir=$(sysconfdir)/gnome-vfs-2.0/vfolders
vfolder_DATA = 				\
	applications.vfolder-info 	\
	preferences.vfolder-info

# Evil, but I can't figure out how I would make autoconf do
# it right
applications.vfolder-info: applications.vfolder-info.in
	sed -e "s,\@datadir\@,$(datadir)," \
	    -e "s,\@sysconfdir\@,$(sysconfdir)," \
	    -e "s,\@prefix\@,$(prefix)," \
	    < $< > $@

preferences.vfolder-info: preferences.vfolder-info.in
	sed -e "s,\@datadir\@,$(datadir)," \
	    -e "s,\@sysconfdir\@,$(sysconfdir)," \
	    -e "s,\@prefix\@,$(prefix)," \
	    < $< > $@

test-vfolder.vfolder-info: test-vfolder.vfolder-info.in
	sed -e "s,\@top_builddir\@,$(top_builddir)," \
	    < $< > $@

test-vfolder-parent.vfolder-info: test-vfolder-parent.vfolder-info.in
	sed -e "s,\@top_builddir\@,$(top_builddir)," \
	    < $< > $@

###  test-vfolder regression test

noinst_PROGRAMS = test-vfolder
noinst_DATA = test-vfolder-parent.vfolder-info test-vfolder.vfolder-info

module_path = \
	$(top_builddir)/modules/vfolder/.libs:$(top_builddir)/modules/.libs

TESTS_INCLUDES = 							  \
	-DGNOME_VFS_MODULE_PATH=\"$(module_path)\" 			  \
	-DGNOME_VFS_MODULE_CONFIG_PATH=\"$(top_srcdir)/modules/vfolder\"

TESTS = test-vfolder

test_vfolder_SOURCES = test-vfolder.c
test_vfolder_LDADD = $(top_builddir)/libgnomevfs/libgnomevfs-2.la
