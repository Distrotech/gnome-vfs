noinst_LTLIBRARIES=libmime.la

INCLUDES=$(LIBGNOMEVFS_CFLAGS)
LIBS=$(LIBGNOMEVFS_LIBS) -lgnomevfs-2

noinst_PROGRAMS=test-mime convert-mime-info

libmime_la_SOURCES = 				\
	mime.c					\
	mime-db.c				\
	mime-db.h

test_mime_SOURCES = 				\
	test-mime.c

test_mime_LDADD = libmime.la

convert_mime_info_SOURCES = 			\
	convert-mime-info.c
