SUBDIRS = gnome-file-selection

INCLUDES =					\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	$(GNOME_INCLUDEDIR)			\
	-I$(top_srcdir)/libgnomevfs

noinst_PROGRAMS =				\
	test-async-pthread			\
	test-async-corba			\
	test-async-directory			\
	test-copy				\
	test-directory				\
	test-directory-visit			\
	test-sync

libraries =						\
	$(top_builddir)/libgnomevfs/libgnomevfs.la	\
	$(GNOME_LIBDIR)					\
	$(GNOMEUI_LIBS)					\
	$(GNOMEGNORBA_LIBS)

test-async_pthread.o: test-async.c
	$(COMPILE) -DWITH_PTHREAD -o $@ -c $<
test_async_pthread_SOURCES =
test_async_pthread_LDADD =						\
	test-async_pthread.o						\
	$(libraries)							\
	$(top_builddir)/libgnomevfs-pthread/libgnomevfs-pthread.la
test_async_pthread_LDFLAGS =

test-async_corba.o: test-async.c
	$(COMPILE) -DWITH_CORBA -o $@ -c $<
test_async_corba_SOURCES =
test_async_corba_LDADD =					\
	test-async_corba.o					\
	$(libraries)						\
	$(top_builddir)/libgnomevfs-corba/libgnomevfs-corba.la
test_async_corba_LDFLAGS =

test_copy_SOURCES = test-copy.c
test_copy_LDADD = $(libraries)
test_copy_LDFLAGS = 

test_directory_SOURCES = test-directory.c
test_directory_LDADD = $(libraries)
test_directory_LDFLAGS = 

test_directory_visit_SOURCES = test-directory-visit.c
test_directory_visit_LDADD = $(libraries)
test_directory_visit_LDFLAGS = 

test_async_directory_SOURCES = test-async-directory.c
test_async_directory_LDADD =					\
	$(libraries)						\
	$(top_builddir)/libgnomevfs-corba/libgnomevfs-corba.la
test_async_directory_LDFLAGS = 

test_sync_SOURCES = test-sync.c
test_sync_LDADD = $(libraries)
test_sync_LDFLAGS =

EXTRA_DIST =					\
	test-async.c 