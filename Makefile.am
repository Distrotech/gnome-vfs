SUBDIRS_GNOME=monikers doc schemas

if HAVE_GTK
GTK_SUBDIR=gtk
else
GTK_SUBDIR=
endif


SUBDIRS =					\
	libgnomevfs				\
	modules					\
	daemon					\
	$(GTK_SUBDIR)				\
	schemas					\
	devel-docs				\
	doc					\
	programs				\
	test					\
	monikers				\
	po

EXTRA_DIST =					\
	HACKING					\
	TODO					\
	autogen.sh				\
	gnome-vfs.spec				\
	gnome-vfs-2.0.pc.in			\
	gnome-vfs-module-2.0.pc.in		\
	gtk-doc.make				\
	intltool-extract.in			\
	intltool-merge.in			\
	intltool-update.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-vfs-2.0.pc gnome-vfs-module-2.0.pc

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

DISTCLEANFILES = intltool-extract intltool-merge intltool-update
