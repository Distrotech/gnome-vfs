SUBDIRS = gnome-file-selection

INCLUDES =					\
	-I$(top_srcdir)/intl			\
	-I$(top_builddir)/intl			\
	-I$(top_srcdir)/libgnomevfs		\
	$(GNOME_INCLUDEDIR)

noinst_PROGRAMS =				\
	test-async-cancel                       \
	test-async-directory_corba		\
	test-async-directory_pthread		\
	test-async_corba			\
	test-async_pthread			\
	test-channel_corba			\
	test-channel_pthread			\
	test-directory				\
	test-directory-visit			\
	test-info				\
	test-seek				\
	test-sync				\
	test-sync-write				\
	test-xfer

TESTS = test-async-cancel

libraries =						\
	$(top_builddir)/libgnomevfs/libgnomevfs.la	\
	$(GNOME_LIBDIR)					\
	$(GNOMEUI_LIBS)					\
	$(GNOMEGNORBA_LIBS)                             \
	$(GCONF_LIBS)

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

test-async-directory_pthread.o: test-async-directory.c
	$(COMPILE) -DWITH_PTHREAD -o $@ -c $<
test_async_directory_pthread_SOURCES =
test_async_directory_pthread_LDADD =					\
	test-async-directory_pthread.o					\
	$(libraries)							\
	$(top_builddir)/libgnomevfs-pthread/libgnomevfs-pthread.la
test_async_directory_pthread_LDFLAGS =

test-async-directory_corba.o: test-async-directory.c
	$(COMPILE) -DWITH_CORBA -o $@ -c $<
test_async_directory_corba_SOURCES =
test_async_directory_corba_LDADD =				\
	test-async-directory_corba.o				\
	$(libraries)						\
	$(top_builddir)/libgnomevfs-corba/libgnomevfs-corba.la
test_async_directory_corba_LDFLAGS =

test-channel_corba.o: test-channel.c
	$(COMPILE) -DWITH_CORBA -o $@ -c $<
test_channel_corba_SOURCES =
test_channel_corba_LDADD =					\
	test-channel_corba.o					\
	$(libraries)						\
	$(top_builddir)/libgnomevfs-corba/libgnomevfs-corba.la
test_channel_corba_LDFLAGS =

test-channel_pthread.o: test-channel.c
	$(COMPILE) -DWITH_PTHREAD -o $@ -c $<
test_channel_pthread_SOURCES =
test_channel_pthread_LDADD =					\
	test-channel_pthread.o					\
	$(libraries)						\
	$(top_builddir)/libgnomevfs-pthread/libgnomevfs-pthread.la
test_channel_pthread_LDFLAGS =

test_seek_SOURCES = test-seek.c
test_seek_LDADD = $(libraries)
test_seek_LDFLAGS =

test_info_SOURCES = test-info.c
test_info_LDADD = $(libraries)
test_info_LDFLAGS =

test_xfer_SOURCES = test-xfer.c
test_xfer_LDADD = $(libraries)
test_xfer_LDFLAGS =

test_directory_SOURCES = test-directory.c
test_directory_LDADD = $(libraries)
test_directory_LDFLAGS = 

test_directory_visit_SOURCES = test-directory-visit.c
test_directory_visit_LDADD = $(libraries)
test_directory_visit_LDFLAGS = 

test_sync_SOURCES = test-sync.c
test_sync_LDADD = $(libraries)
test_sync_LDFLAGS =

test_sync_write_SOURCES = test-sync-write.c
test_sync_write_LDADD = $(libraries)
test_sync_write_LDFLAGS =

test_async_cancel_SOURCES = test-async-cancel.c
test_async_cancel_LDADD = $(libraries)
test_async_cancel_LDFLAGS = 

EXTRA_DIST =					\
	test-async-directory.c			\
	test-async.c				\
	test-channel.c				\
	test-directory-visit.c			\
	test-directory.c			\
	test-seek.c				\
	test-sync.c				\
	test-sync-write.c			\
	test-xfer.c				\
	vfs-run.in
