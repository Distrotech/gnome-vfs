INCLUDES =					\
	-I$(top_srcdir)/libgnomevfs		\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	$(GNOME_INCLUDEDIR)

CPPFLAGS =					\
	-D_REENTRANT

module_flags = -export_dynamic -avoid-version

modulesdir = $(prefix)/lib/vfs/modules

modules_LTLIBRARIES = \
	libfile.la \
	libgzip.la

libfile_la_SOURCES = \
	file-method.c \
	file-method.h
libfile_la_LDFLAGS = $(module_flags)

libgzip_la_SOURCES = \
	gzip-method.c \
	gzip-method.h
libgzip_la_LDFLAGS = $(module_flags)
libgzip_la_LIBADD = -lz
