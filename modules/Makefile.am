INCLUDES =					\
	-I$(top_srcdir)/libgnomevfs		\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	$(GNOME_INCLUDEDIR)

CPPFLAGS =					\
	-D_REENTRANT

module_flags = -export_dynamic -avoid-version
modulesdir = $(prefix)/lib/vfs/modules

modules_LTLIBRARIES =				\
	libfile.la				\
	libgzip.la				\
	libftp.la

libfile_la_SOURCES = 				\
	file-method.c 				\
	file-method.h

libfile_la_LDFLAGS = $(module_flags)

libgzip_la_SOURCES = 				\
	gzip-method.c 				\
	gzip-method.h
libgzip_la_LDFLAGS = $(module_flags)
libgzip_la_LIBADD = -lz

libftp_la_SOURCES = 				\
	ftp-method.c				\
	ftp-method.h				\
	parse.c					\
	parse.h

if HAVE_GCONF
gconfmoduledir = $(modulesdir)
gconfmodule_LTLIBRARIES = libgconf.la

libgconf_la_SOURCES =                           \
	gconf-method.c                          \
        gconf-method.h
libgconf_la_LIBADD = $(GCONF_LIBS)
endif

libgconf_la_LDFLAGS = $(module_flags)


#libhttp_la_SOURCES =				\
#	http-method.c				\
#	http-method.h
#libhttp_la_LDFLAGS = $(module_flags)
#libhttp_la_LIBADD  = -lghttp
