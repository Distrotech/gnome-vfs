#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <glib.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include "gnome-vfs-module.h"

typedef struct {
	char *trans_string;
	char *real_method_name;
	char *default_mime_type;
} ParsedArgs;

typedef struct {
	GnomeVFSMethod base_method;
	ParsedArgs pa;
	GnomeVFSMethod *real_method;
} TranslateMethod;

static void
tr_apply_default_mime_type(TranslateMethod * tm,
			   GnomeVFSFileInfo * file_info)
{
	if (!file_info->mime_type && tm->pa.default_mime_type) {
		file_info->mime_type = g_strdup(tm->pa.default_mime_type);
		file_info->valid_fields |=
		    GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}
}


static GnomeVFSURI *tr_uri_translate(TranslateMethod * tm,
				     const GnomeVFSURI * uri)
{
	GnomeVFSURI *retval;

	if (uri->method != (GnomeVFSMethod *) tm)
		return gnome_vfs_uri_ref((GnomeVFSURI *) uri);	/* Don't translate things that don't belong to us */

	/* Hack it all up to pieces */
	retval = gnome_vfs_uri_dup(uri);
	g_free(retval->text);
	retval->text =
	    g_strdup_printf(tm->pa.trans_string, uri->text, uri->text,
			    uri->text, uri->text, uri->text);
	g_free(retval->method_string);
	retval->method_string = g_strdup(tm->pa.real_method_name);
	retval->method = tm->real_method;

	return retval;
}

static GnomeVFSResult
tr_do_open(GnomeVFSMethod * method,
	   GnomeVFSMethodHandle ** method_handle_return,
	   GnomeVFSURI * uri,
	   GnomeVFSOpenMode mode, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->open(tm->real_method, method_handle_return,
				  real_uri, mode, context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_create(GnomeVFSMethod * method,
	     GnomeVFSMethodHandle
	     ** method_handle_return,
	     GnomeVFSURI * uri,
	     GnomeVFSOpenMode mode,
	     gboolean exclusive, guint perm, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->create(tm->real_method, method_handle_return,
				    real_uri, mode, exclusive, perm,
				    context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_close(GnomeVFSMethod * method,
	    GnomeVFSMethodHandle * method_handle,
	    GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	return tm->real_method->close(tm->real_method, method_handle,
				      context);
}

static GnomeVFSResult
tr_do_read(GnomeVFSMethod * method,
	   GnomeVFSMethodHandle * method_handle,
	   gpointer buffer,
	   GnomeVFSFileSize num_bytes,
	   GnomeVFSFileSize * bytes_read_return, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	return tm->real_method->read(tm->real_method, method_handle,
				     buffer, num_bytes, bytes_read_return,
				     context);
}

static GnomeVFSResult
tr_do_write(GnomeVFSMethod * method,
	    GnomeVFSMethodHandle * method_handle,
	    gconstpointer buffer,
	    GnomeVFSFileSize num_bytes,
	    GnomeVFSFileSize * bytes_written_return,
	    GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	return tm->real_method->write(tm->real_method, method_handle,
				      buffer, num_bytes,
				      bytes_written_return, context);
}

static GnomeVFSResult
tr_do_seek(GnomeVFSMethod * method,
	   GnomeVFSMethodHandle * method_handle,
	   GnomeVFSSeekPosition whence,
	   GnomeVFSFileOffset offset, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	return tm->real_method->seek(tm->real_method, method_handle,
				     whence, offset, context);
}

static GnomeVFSResult
tr_do_tell(GnomeVFSMethod * method,
	   GnomeVFSMethodHandle * method_handle,
	   GnomeVFSFileOffset * offset_return)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	return tm->real_method->tell(tm->real_method, method_handle,
				     offset_return);
}

static GnomeVFSResult
tr_do_open_directory(GnomeVFSMethod * method,
		     GnomeVFSMethodHandle ** method_handle,
		     GnomeVFSURI * uri,
		     GnomeVFSFileInfoOptions options,
		     const GList * meta_keys,
		     const GnomeVFSDirectoryFilter * filter,
		     GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->open_directory(tm->real_method, method_handle,
					    real_uri, options, meta_keys,
					    filter, context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_close_directory(GnomeVFSMethod * method,
		      GnomeVFSMethodHandle * method_handle,
		      GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	return tm->real_method->close_directory(tm->real_method,
						method_handle, context);
}

static GnomeVFSResult
tr_do_read_directory(GnomeVFSMethod * method,
		     GnomeVFSMethodHandle * method_handle,
		     GnomeVFSFileInfo * file_info,
		     GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSResult retval;

	retval =
	    tm->real_method->read_directory(tm->real_method, method_handle,
					    file_info, context);

	tr_apply_default_mime_type(tm, file_info);

	return retval;
}

static GnomeVFSResult
tr_do_get_file_info(GnomeVFSMethod * method,
		    GnomeVFSURI * uri,
		    GnomeVFSFileInfo * file_info,
		    GnomeVFSFileInfoOptions options,
		    const GList * meta_keys, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->get_file_info(tm->real_method, real_uri,
					   file_info, options, meta_keys,
					   context);

	gnome_vfs_uri_unref(real_uri);

	tr_apply_default_mime_type(tm, file_info);

	return retval;
}

static GnomeVFSResult
tr_do_get_file_info_from_handle(GnomeVFSMethod * method,
				GnomeVFSMethodHandle * method_handle,
				GnomeVFSFileInfo * file_info,
				GnomeVFSFileInfoOptions options,
				const GList * meta_keys,
				GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSResult retval;

	retval =
	    tm->real_method->get_file_info_from_handle(tm->real_method,
						       method_handle,
						       file_info, options,
						       meta_keys, context);

	tr_apply_default_mime_type(tm, file_info);

	return retval;
}

static GnomeVFSResult
tr_do_truncate(GnomeVFSMethod * method,
	       GnomeVFSURI * uri,
	       GnomeVFSFileSize length, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->truncate(tm->real_method, real_uri, length,
				      context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_truncate_handle(GnomeVFSMethod * method,
		      GnomeVFSMethodHandle * method_handle,
		      GnomeVFSFileSize length, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;

	return tm->real_method->truncate_handle(tm->real_method,
						method_handle, length,
						context);
}

static gboolean
tr_do_is_local(GnomeVFSMethod * method, const GnomeVFSURI * uri)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval = tm->real_method->is_local(tm->real_method, real_uri);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_make_directory(GnomeVFSMethod * method,
		     GnomeVFSURI * uri,
		     guint perm, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->make_directory(tm->real_method, real_uri,
					    perm, context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_find_directory(GnomeVFSMethod * method,
		     GnomeVFSURI * near_uri,
		     GnomeVFSFindDirectoryKind kind,
		     GnomeVFSURI ** result_uri,
		     gboolean create_if_needed,
		     guint permissions, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, near_uri);

	retval =
	    tm->real_method->find_directory(tm->real_method, real_uri,
					    kind, result_uri,
					    create_if_needed, permissions,
					    context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}



static GnomeVFSResult
tr_do_remove_directory(GnomeVFSMethod * method,
		       GnomeVFSURI * uri, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->remove_directory(tm->real_method, real_uri,
					      context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_move(GnomeVFSMethod * method,
	   GnomeVFSURI * old_uri,
	   GnomeVFSURI * new_uri,
	   gboolean force_replace, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri_old, *real_uri_new;
	GnomeVFSResult retval;

	real_uri_old = tr_uri_translate(tm, old_uri);
	real_uri_new = tr_uri_translate(tm, new_uri);

	retval =
	    tm->real_method->move(tm->real_method, real_uri_old,
				  real_uri_new, force_replace, context);

	gnome_vfs_uri_unref(real_uri_old);
	gnome_vfs_uri_unref(real_uri_new);

	return retval;
}

static GnomeVFSResult
tr_do_unlink(GnomeVFSMethod * method,
	     GnomeVFSURI * uri, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri;
	GnomeVFSResult retval;

	real_uri = tr_uri_translate(tm, uri);

	retval =
	    tm->real_method->unlink(tm->real_method, real_uri, context);

	gnome_vfs_uri_unref(real_uri);

	return retval;
}

static GnomeVFSResult
tr_do_check_same_fs(GnomeVFSMethod * method,
		    GnomeVFSURI * a,
		    GnomeVFSURI * b,
		    gboolean * same_fs_return, GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri_a, *real_uri_b;
	GnomeVFSResult retval;

	real_uri_a = tr_uri_translate(tm, a);
	real_uri_b = tr_uri_translate(tm, b);

	retval =
	    tm->real_method->check_same_fs(tm->real_method, real_uri_a,
					   real_uri_b, same_fs_return,
					   context);

	gnome_vfs_uri_unref(real_uri_a);
	gnome_vfs_uri_unref(real_uri_b);

	return retval;
}

static GnomeVFSResult
tr_do_set_file_info(GnomeVFSMethod * method,
		    GnomeVFSURI * a,
		    const GnomeVFSFileInfo * info,
		    GnomeVFSSetFileInfoMask mask,
		    GnomeVFSContext * context)
{
	TranslateMethod *tm = (TranslateMethod *) method;
	GnomeVFSURI *real_uri_a;
	GnomeVFSResult retval;

	real_uri_a = tr_uri_translate(tm, a);

	retval =
	    tm->real_method->set_file_info(tm->real_method, real_uri_a,
					   info, mask, context);

	gnome_vfs_uri_unref(real_uri_a);

	return retval;
}

/******** from poptparse.c:
  Copyright (c) 1998  Red Hat Software

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
  AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

  Except as contained in this notice, the name of the X Consortium shall not be
  used in advertising or otherwise to promote the sale, use or other dealings
  in this Software without prior written authorization from the X Consortium.

*/

#define POPT_ARGV_ARRAY_GROW_DELTA 5
#define POPT_ARGV_ARRAY_GROW_DELTA 5
static int my_poptParseArgvString(char *buf, int *argcPtr, char ***argvPtr)
{
	char *src;
	char quote = '\0';
	int argvAlloced = POPT_ARGV_ARRAY_GROW_DELTA;
	char **argv = g_new(char *, argvAlloced);
	int argc = 0;
	char *s;

	s = alloca(strlen(buf) + 1);
	strcpy(s, buf);

	argv[argc] = buf;

	for (src = s; *src; src++) {
		if (quote == *src) {
			quote = '\0';
		} else if (quote) {
			if (*src == '\\') {
				src++;
				if (!*src) {
					g_free(argv);
					return -1;
				}
				if (*src != quote)
					*(buf++) = '\\';
			}
			*(buf++) = *src;
		} else if (isspace((unsigned char)*src)) {
			*buf = '\0';
			if (*argv[argc]) {
				buf++, argc++;
				if (argc == argvAlloced) {
					argvAlloced +=
					    POPT_ARGV_ARRAY_GROW_DELTA;
					argv =
					    g_realloc(argv,
						      sizeof(*argv) *
						      argvAlloced);
				}
				argv[argc] = buf;
			}
		} else
			switch (*src) {
			case '"':
			case '\'':
				quote = *src;
				break;
			case '\\':
				src++;
				if (!*src) {
					g_free(argv);
					return -1;
				}
				/*@fallthrough@ */
			default:
				*(buf++) = *src;
				break;
			}
	}

	*buf = '\0';
	if (strlen(argv[argc])) {
		argc++, buf++;
	}

	*argcPtr = argc;
	*argvPtr = argv;

	return 0;
}

static gboolean tr_args_parse(ParsedArgs * pa, const char *args)
{
	char **argv;
	int argc;
	char *tmp_args;
	int i;
	gboolean badargs = FALSE;

	memset(pa, 0, sizeof(ParsedArgs));

	tmp_args = alloca(strlen(args) + 1);
	strcpy(tmp_args, args);

	if (my_poptParseArgvString(tmp_args, &argc, &argv)) {
		g_warning("Failed to parse arguments: %s", args);
		return FALSE;
	}

	for (i = 0; i < argc; i++) {
#define CHECK_ARG() if((++i) >= argc) { badargs = TRUE; goto out; }
		if (g_strcasecmp(argv[i], "-pattern") == 0) {
			CHECK_ARG();
			pa->trans_string = g_strdup(argv[i]);
		} else if (g_strcasecmp(argv[i], "-real-method") == 0) {
			CHECK_ARG();
			pa->real_method_name = g_strdup(argv[i]);
		} else if (g_strcasecmp(argv[i], "-default-mime-type") == 0) {
			CHECK_ARG();
			pa->default_mime_type = g_strdup(argv[i]);
		} else {
			g_warning("Unknown option `%s'", argv[i]);
			badargs = TRUE;
			goto out;
		}
#undef CHECK_ARG
	}

	if (!pa->real_method_name) {
		g_warning("Need a -real-method option");
		badargs = TRUE;
	} else if (!pa->trans_string) {
		g_warning("Need a -pattern option");
		badargs = TRUE;
	}

      out:
	g_free(argv);
	return !badargs;
}

static GnomeVFSMethod base_vfs_method = {
	tr_do_open,
	tr_do_create,
	tr_do_close,
	tr_do_read,
	tr_do_write,
	tr_do_seek,
	tr_do_tell,
	tr_do_truncate_handle,
	tr_do_open_directory,
	tr_do_close_directory,
	tr_do_read_directory,
	tr_do_get_file_info,
	tr_do_get_file_info_from_handle,
	tr_do_is_local,
	tr_do_make_directory,
	tr_do_remove_directory,
	tr_do_move,
	tr_do_unlink,
	tr_do_check_same_fs,
	tr_do_set_file_info,
	tr_do_truncate,
	tr_do_find_directory,
	NULL
};

static void tr_args_free(ParsedArgs * pa)
{
	g_free(pa->real_method_name);
	g_free(pa->trans_string);
	g_free(pa->default_mime_type);
}

GnomeVFSMethod *vfs_module_init(const char *method_name, const char *args)
{
	TranslateMethod *retval;
	ParsedArgs pa;

	if (!tr_args_parse(&pa, args))
		return NULL;

	retval = g_new0(TranslateMethod, 1);
	retval->pa = pa;
	retval->real_method = gnome_vfs_method_get(pa.real_method_name);

	if (!retval->real_method) {
		tr_args_free(&retval->pa);
		g_free(retval);
		return NULL;
	}

	retval->base_method = base_vfs_method;

#define CHECK_NULL_METHOD(x) if(!retval->real_method->x) retval->base_method.x = NULL
	CHECK_NULL_METHOD(open);
	CHECK_NULL_METHOD(create);
	CHECK_NULL_METHOD(close);
	CHECK_NULL_METHOD(read);
	CHECK_NULL_METHOD(write);
	CHECK_NULL_METHOD(seek);
	CHECK_NULL_METHOD(tell);
	CHECK_NULL_METHOD(truncate);
	CHECK_NULL_METHOD(open_directory);
	CHECK_NULL_METHOD(close_directory);
	CHECK_NULL_METHOD(read_directory);
	CHECK_NULL_METHOD(get_file_info);
	CHECK_NULL_METHOD(get_file_info_from_handle);
	CHECK_NULL_METHOD(is_local);
	CHECK_NULL_METHOD(make_directory);
	CHECK_NULL_METHOD(remove_directory);
	CHECK_NULL_METHOD(move);
	CHECK_NULL_METHOD(unlink);
	CHECK_NULL_METHOD(check_same_fs);
	CHECK_NULL_METHOD(set_file_info);
	CHECK_NULL_METHOD(truncate_handle);
#undef CHECK_NULL_METHOD

	return (GnomeVFSMethod *) retval;
}

void vfs_module_shutdown(GnomeVFSMethod * method)
{
	TranslateMethod *tmethod = (TranslateMethod *) method;

	tr_args_free(&tmethod->pa);

	g_free(tmethod);
}
