/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* gnome-vfs-mime-info.c - GNOME mime-information implementation.

   Copyright (C) 1998 Miguel de Icaza
   Copyright (C) 2000, 2001 Eazel, Inc.
   All rights reserved.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors:
   Miguel De Icaza <miguel@helixcode.com>
   Mathieu Lacage <mathieu@eazel.com>
*/

/* How does intltools behave with namespaces ? ie is it able to translate
 * tags like <gnome:_tag> ?
 *
 * In the old gnome-vfs.keys.in, what does xmms-no-uris correspond to ?
 * In the old gnome-vfs.keys.in, there were different helpers for 
 * text/*, text/html, text/plain
 * depending on the user level
 *
 * Weird value for x-directory/normal, short_list_component
 * Same value for x-directory/vfolder-desktop
 *
 * audio/x-midi or audio/midi ?
 *
 * image/x-pcx => pas de description
 * application/mime-type-test
 * x-special/device-block
 * application/x-java-archive => application/x-jar
 * application/x-gdbm => pas de description
 * text/abiword => application/x-abiword
 * text/x-troff => application/x-troff
 * application/x-ogg => application/ogg
 * application/x-mswinurl => pas de description
 * application/x-ms-dos-executable
 * image/x-niff => pas de description
 * application/x-font-type1 => application/x-font ?
 * audio/x-ulaw => audio/basic
 * x-special/fifo
 * image/x-pict
 * image/x-dcm
 * application/x-rar-compressed => application/x-rar
 * application/x-flac
 * application/x-ipod-firmware
 * audio/x-ms-asx => video/x-ms-asf
 * audio/mpeg => audio/x-mp3 => WRONG!!
 * text/x-c++ => text/x-c++src
 * x-special/socket
 * text/x-sh => application/x-shellscript
 * application/x-ape
 * image/x-dib
 * x-directory/webdav
 * text/bib => text/x-bibtex
 * text/x-csh => application/x-csh
 * application/x-xbase
 * image/svg => image/svg+xml
 * application/x-tex => text/x-tex (both)
 * x-directory/vfolder-desktop
 * image/x-bmp => image/bmp
 * application/x-dbm => pas de description
 * application/x-gnome-theme-installed
 * x-special/device-char
 * image/x-sgi
 * application/x-mrproject
 * image/x-miff
 * application/x-kde-app-info
 * application/x-sc
 * application/x-gnome-theme
 * application/x-font-otf
 * x-directory/search
 * text/x-perl => application/x-perl (both)
 * application/x-frame
 * image/x-applix-graphic
 * image/x-djvu => pas de description
 * application/x-python-byte-code => application/x-python-bytecode
 * text/xmcd => pas de description
 * text/x-asm
 * image/x-fpx
 * video/msvideo => video/x-msvideo ?
 * application/vnd.corel-draw
 * application/x-backup
 * text/x-c => text/x-c++src
 * video/x-flc => video/x-flic
 * text/x-lyx => application/x-lyx
 * text/x-c-header => text/x-chdr
 * text/x-python => application/x-python
 * application/x-object-file => application/x-object ?
 * application/x-macbinary
 * image/x-icb
 * x-directory/normal
 * video/x-fli => video/x-flic
 * application/x-shared-library
 * application/x-java-byte-code => application/x-java ?
 * application/x-arc
 * audio/x-real-audio => audio/x-pn-realaudio (both)
 * application/x-smil => application/smil
 * text/x-troff-man
 * image/x-palm
 * text/x-ksysv-log => pas de description
 * image/x-sun-raster
 * application/x-wordperfect => application/wordperfect
 * image/x-dpx
 * text/x-po => text/x-gettext-translation
 * application/x-unix-archive
 * image/x-fits
 * x-directory/webdav-prefer-directory
 * audio/x-midi => audio/midi
 * text/tex => text/x-tex (text/tex seems to be in the wild though)
 * x-special/symlink
 * application/x-core-file => application/x-core
 * application/x-iso-image => application/x-cd-image
 * text/x-zsh
 * text/x-yacc
 * application/x-executable-binary
 * application/x-ksysv-package => pas de description
 * application/x-gnome-app-info
 * image/x-emf
 * application/qif => application/x-qw (both)
 * image/x-png => image/png (both)
 * image/x-gray
 */


#include <config.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>

#include <libgnomevfs/gnome-vfs-private-utils.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#ifdef NEED_GNOMESUPPORT_H
#include "gnomesupport.h"
#endif

#define FAST_FILE_EOF -1
#define FAST_FILE_BUFFER_SIZE (1024 * 16)

#define MIME_SPEC_NAMESPACE (const xmlChar *)"http://www.freedesktop.org/standards/shared-mime-info"
#define GNOME_VFS_NAMESPACE (const xmlChar *)"http://www.gnome.org/gnome-vfs/mime/1.0"

typedef struct {
	guchar *ptr;
	guchar *buffer;
	int     length;
	FILE   *fh;
} FastFile;

static void
fast_file_close (FastFile *ffh)
{
	fclose (ffh->fh);
	g_free (ffh->buffer);
}

static gboolean
fast_file_open (FastFile *ffh, const char *filename)
{
	memset (ffh, 0, sizeof (FastFile));

	ffh->fh = fopen (filename, "r");
	if (ffh->fh == NULL) {
		return FALSE;
	}

	ffh->buffer = g_malloc (FAST_FILE_BUFFER_SIZE);
	ffh->ptr = ffh->buffer;

	ffh->length = fread (ffh->buffer, 1, FAST_FILE_BUFFER_SIZE, ffh->fh);

	if (ffh->length < 0) {
		fast_file_close (ffh);
		return FALSE;
	}

	return TRUE;
}

static inline int
fast_file_getc (FastFile *ffh)
{
	if (ffh->ptr < ffh->buffer + ffh->length)
		return *(ffh->ptr++);
	else {
		ffh->length = fread (ffh->buffer, 1, FAST_FILE_BUFFER_SIZE, ffh->fh);

		if (!ffh->length)
			return FAST_FILE_EOF;
		else {
			ffh->ptr = ffh->buffer;
			return *(ffh->ptr++);
		}
	}
}

typedef struct {
	char       *mime_type;
	GHashTable *keys;
} GnomeMimeContext;

/* Describes the directories we scan for information */
typedef struct {
	char *dirname;
	struct stat s;
	unsigned int valid : 1;
	unsigned int system_dir : 1;
	unsigned int force_reload : 1;
} mime_dir_source_t;


#define DELETED_KEY "deleted"
#define DELETED_VALUE "moilegrandvizir"

/* These ones are used to automatically reload mime info on demand */
static mime_dir_source_t gnome_mime_dir, user_mime_dir;
static time_t last_checked;

/* To initialize the module automatically */
static gboolean gnome_vfs_mime_inited = FALSE;

/* you will write back or reload the file if and only if this var' value is 0 */
static int gnome_vfs_is_frozen = 0;

static GList *current_lang = NULL;
/* we want to replace the previous key if the current key has a higher
   language level */
static int previous_key_lang_level = -1;

/*
 * A hash table containing all of the Mime records for specific
 * mime types (full description, like image/png)
 * It also contains a the generic types like image/
 * extracted from .keys files
 */
static GHashTable *specific_types;
/* user specific data */
static GHashTable *specific_types_user;


/*
 * A hash table containing all of the Mime records for all registered
 * mime types
 * extracted from .mime files
 */
static GHashTable *registered_types;
/* user specific data */
static GHashTable *registered_types_user;



/* Prototypes */
static const char *   gnome_vfs_mime_get_registered_mime_type_key (const char *mime_type,
								   const char *key);

void
_gnome_vfs_mime_info_mark_gnome_mime_dir_dirty (void)
{
	gnome_mime_dir.force_reload = TRUE;
}

void
_gnome_vfs_mime_info_mark_user_mime_dir_dirty (void)
{
	user_mime_dir.force_reload = TRUE;
}

static gboolean
does_string_contain_caps (const char *string)
{
	const char *temp_c;

	temp_c = string;
	while (*temp_c != '\0') {
		if (g_ascii_isupper (*temp_c)) {
			return TRUE;
		}
		temp_c++;
	}

	return FALSE;
}

static GnomeMimeContext *
context_new (GHashTable *hash_table, GString *str)
{
	GnomeMimeContext *context;
	char *mime_type;
	char last_char;

	mime_type = g_strdup (str->str);

	last_char = mime_type[strlen (mime_type) - 1];
	if (last_char == '*') {
		mime_type[strlen (mime_type) - 1] = '\0';
	}

	context = g_hash_table_lookup (hash_table, mime_type);

	if (context != NULL) {
		g_free (mime_type);
		return context;
	}

	/*	fprintf (stderr, "New context: '%s'\n", mime_type); */

	context = g_new (GnomeMimeContext, 1);
	context->mime_type = mime_type;
	context->keys = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);

	g_hash_table_insert (hash_table, context->mime_type, context);

	//	g_hash_table_foreach (specific_types, (GHFunc)print_type, NULL);

	return context;
}

static void
context_destroy (GnomeMimeContext *context)
{
	g_hash_table_destroy (context->keys);
	g_free (context->mime_type);
	g_free (context);
}

static void
context_destroy_and_unlink (GnomeMimeContext *context)
{
	/*
	 * Remove the context from our hash tables, we dont know
	 * where it is: so just remove it from both (it can
	 * only be in one).
	 */
	g_hash_table_remove (specific_types,        context->mime_type);
	g_hash_table_remove (registered_types,      context->mime_type);
	g_hash_table_remove (specific_types_user,   context->mime_type);
	g_hash_table_remove (registered_types_user, context->mime_type);

	context_destroy (context);
}

/* this gives us a number of the language in the current language list,
   the higher the number the "better" the translation */
static int
language_level (const char *langage)
{
	int i;
	GList *li;

	if (langage == NULL)
		return 0;

	for (i = 1, li = current_lang; li != NULL; i++, li = g_list_next (li)) {
		if (strcmp ((const char *) li->data, langage) == 0)
			return i;
	}

	return -1;
}


static void
context_add_key (GnomeMimeContext *context, char *key, char *lang, char *value)
{
	int lang_level;

	lang_level = language_level (lang);
	/* wrong language completely */
	if (lang_level < 0)
		return;

	/* if a previous key in the hash had a better lang_level don't do anything */
	if (lang_level > 0 &&
	    previous_key_lang_level > lang_level) {
		return;
	}

/*	fprintf (stderr, "Add key: '%s' '%s' '%s' %d\n", key, lang, value, lang_level); */

	g_hash_table_replace (context->keys, g_strdup (key), g_strdup (value));

	previous_key_lang_level = lang_level;
}

typedef enum {
	STATE_NONE,
	STATE_LANG,
	STATE_LOOKING_FOR_KEY,
	STATE_ON_MIME_TYPE,
	STATE_ON_KEY,
	STATE_ON_VALUE
} ParserState;

/* #define APPEND_CHAR(gstr,c) g_string_insert_c ((gstr), -1, (c)) */

#define APPEND_CHAR(gstr,c) \
	G_STMT_START {                                     \
		if (gstr->len + 1 < gstr->allocated_len) { \
			gstr->str [gstr->len++] = c;       \
			gstr->str [gstr->len] = '\0';      \
		} else {                                   \
			g_string_insert_c (gstr, -1, c);   \
		}                                          \
	} G_STMT_END

typedef enum {
	FORMAT_MIME,
	FORMAT_KEYS
} Format;

#define load_mime_type_info_from(a,b) load_type_info_from ((a), (b), FORMAT_MIME)
#define load_mime_list_info_from(a,b) load_type_info_from ((a), (b), FORMAT_KEYS)

static void
load_type_info_from (const char *filename,
		     GHashTable *hash_table,
		     Format      format)
{
	GString *line;
	int column, c;
	ParserState state;
	FastFile mime_file;
	gboolean skip_line;
	GnomeMimeContext *context;
	int key, lang, last_str_end; /* offsets */

	if (!fast_file_open (&mime_file, filename)) {
		return;
	}

	skip_line = FALSE;
	column = -1;
	context = NULL;
	line = g_string_sized_new (120);
	key = lang = last_str_end = 0;
	state = STATE_NONE;

	while ((c = fast_file_getc (&mime_file)) != EOF) {
	handle_char:
		column++;
		if (c == '\r')
			continue;

		if (c == '#' && column == 0) {
			skip_line = TRUE;
			continue;
		}

		if (skip_line) {
			if (c == '\n') {
				skip_line = FALSE;
				column = -1;
				g_string_truncate (line, 0);
				key = lang = last_str_end = 0;
			}
			continue;
		}

		if (c == '\n') {
			skip_line = FALSE;
			column = -1;
			if (state == STATE_ON_MIME_TYPE) {
				/* setup for a new key */
				previous_key_lang_level = -1;
				context = context_new (hash_table, line);

			} else if (state == STATE_ON_VALUE) {
				APPEND_CHAR (line, '\0');
				context_add_key (context,
						 line->str + key,
						 lang ? line->str + lang : NULL,
						 line->str + last_str_end);
				key = lang = 0;
			}
			g_string_truncate (line, 0);
			last_str_end = 0;
			state = STATE_LOOKING_FOR_KEY;
			continue;
		}

		switch (state) {
		case STATE_NONE:
			if (c == ' ' || c == '\t') {
				break;
			} else if (c == ':') {
				skip_line = TRUE;
				break;
			} else {
				state = STATE_ON_MIME_TYPE;
			}
			/* fall down */

		case STATE_ON_MIME_TYPE:
			if (c == ':') {
				skip_line = TRUE;
				/* setup for a new key */
				previous_key_lang_level = -1;
				context = context_new (hash_table, line);
				state = STATE_LOOKING_FOR_KEY;
				break;
			}
			APPEND_CHAR (line, c);
			break;

		case STATE_LOOKING_FOR_KEY:
			if (c == '\t' || c == ' ') {
				break;
			}

			if (c == '[') {
				state = STATE_LANG;
				break;
			}

			if (column == 0) {
				state = STATE_ON_MIME_TYPE;
				APPEND_CHAR (line, c);
				break;
			}
			state = STATE_ON_KEY;
			/* falldown */

		case STATE_ON_KEY:
			if (c == '\\') {
				c = fast_file_getc (&mime_file);
				if (c == EOF) {
					break;
				}
			}
			if (c == '=') {
				key = last_str_end;
				APPEND_CHAR (line, '\0');
				last_str_end = line->len;
				state = STATE_ON_VALUE;
				break;
			}

			if (format == FORMAT_KEYS && c == ':') {
				key = last_str_end;
				APPEND_CHAR (line, '\0');
				last_str_end = line->len;

				/* Skip space after colon.  There should be one
				 * there.  That is how the file is defined. */
				c = fast_file_getc (&mime_file);
				if (c != ' ') {
					if (c == EOF) {
						break;
					} else {
						goto handle_char;
					}
				} else {
					column++;
				}

				state = STATE_ON_VALUE;
				break;
			}

			APPEND_CHAR (line, c);
			break;

		case STATE_ON_VALUE:
			APPEND_CHAR (line, c);
			break;

		case STATE_LANG:
			if (c == ']') {
				state = STATE_ON_KEY;

				lang = last_str_end;
				APPEND_CHAR (line, '\0');
				last_str_end = line->len;

				if (!line->str [0] ||
				    language_level (line->str + lang) < 0) {
					skip_line = TRUE;
					key = lang = last_str_end = 0;
					g_string_truncate (line, 0);
					state = STATE_LOOKING_FOR_KEY;
				}
			} else {
				APPEND_CHAR (line, c);
			}
			break;
		}
	}

	if (context != NULL) {
		if (key && line->str [0]) {
			APPEND_CHAR (line, '\0');
			context_add_key (context,
					 line->str + key,
					 lang ? line->str + lang : NULL,
					 line->str + last_str_end);
		} else {
			if (g_hash_table_size (context->keys) < 1) {
				context_destroy_and_unlink (context);
			}
		}
	}

	g_string_free (line, TRUE);

	previous_key_lang_level = -1;

	fast_file_close (&mime_file);
}

static void
mime_info_load (mime_dir_source_t *source)
{
	DIR *dir;
	struct dirent *dent;
	const int extlen = sizeof (".keys") - 1;
	char *filename;

	if (stat (source->dirname, &source->s) != -1)
		source->valid = TRUE;
	else
		source->valid = FALSE;

	if (source->system_dir) {
		filename = g_strconcat (source->dirname, "/gnome-vfs.keys", NULL);
		load_mime_type_info_from (filename, specific_types);
		g_free (filename);
	}
}

static void
load_mime_type_info (void)
{
	mime_info_load (&gnome_mime_dir);
	mime_info_load (&user_mime_dir);
	//	mime_list_load (&gnome_mime_dir);
	//	mime_list_load (&user_mime_dir);
}

static void
gnome_vfs_mime_init (void)
{
	/*
	 * The hash tables that store the mime keys.
	 */
	specific_types = g_hash_table_new (g_str_hash, g_str_equal);
	registered_types  = g_hash_table_new (g_str_hash, g_str_equal);

	specific_types_user = g_hash_table_new (g_str_hash, g_str_equal);
	registered_types_user  = g_hash_table_new (g_str_hash, g_str_equal);

	current_lang = gnome_vfs_i18n_get_language_list ("LC_MESSAGES");

	/*
	 * Setup the descriptors for the information loading
	 */
	gnome_mime_dir.dirname = g_strdup ("/opt/gnome23/share/mime-info");
	gnome_mime_dir.system_dir = TRUE;

	user_mime_dir.dirname  = g_strconcat
		(g_get_home_dir (), "/.gnome2/mime-info", NULL);
	user_mime_dir.system_dir = FALSE;

	/*
	 * Load
	 */
	load_mime_type_info ();

	last_checked = time (NULL);
	gnome_vfs_mime_inited = TRUE;
}

static gboolean
remove_keys (gpointer key, gpointer value, gpointer user_data)
{
	GnomeMimeContext *context = value;

	context_destroy (context);

	return TRUE;
}

static void
gnome_vfs_mime_info_clear (void)
{
	if (specific_types != NULL) {
		g_hash_table_foreach_remove (specific_types, remove_keys, NULL);
	}
	if (registered_types != NULL) {
		g_hash_table_foreach_remove (registered_types, remove_keys, NULL);
	}
	if (specific_types_user != NULL) {
		g_hash_table_foreach_remove (specific_types_user, remove_keys, NULL);
	}
	if (registered_types_user != NULL) {
		g_hash_table_foreach_remove (registered_types_user, remove_keys, NULL);
	}
}

/**
 * gnome_vfs_mime_info_reload:
 *
 * Reload the MIME database from disk and notify any listeners
 * holding active #GnomeVFSMIMEMonitor objects.
 **/
void
gnome_vfs_mime_info_reload (void)
{
	if (!gnome_vfs_mime_inited) {
		gnome_vfs_mime_init ();
	}

	/* 1. Clean */
	gnome_vfs_mime_info_clear ();

	/* 2. Reload */
	load_mime_type_info ();

	/* 3. clear our force flags */
	gnome_mime_dir.force_reload = FALSE;
	user_mime_dir.force_reload = FALSE;

	/* 3. Tell anyone who cares */
	_gnome_vfs_mime_monitor_emit_data_changed (gnome_vfs_mime_monitor_get ());
}

void
print_list (gchar *list)
{
	gchar *tok;

	tok = strtok (list, ",");
	while (tok != NULL) {
		g_print ("\t\t%s\n", tok);
		tok = strtok (NULL, ",");
	}
}

void
save_list (gchar *list, xmlNodePtr node)
{
	gchar *tok;

	tok = strtok (list, ",");
	while (tok != NULL) {
		xmlNodePtr newnode;
		xmlNsPtr ns;

		ns = xmlSearchNs (node->doc, node, "gnome");
		if (ns == NULL) {
			g_print ("Uh Oh, namespace not found\n");
		}
		xmlAddChild (node, xmlNewText ("\t\t"));
		newnode = xmlNewChild (node, ns, "default-application", NULL);
		xmlAddChild (newnode, xmlNewText ("\n\t\t\t"));
		xmlNewTextChild (newnode, ns, "app-id", tok);
		xmlAddChild (newnode, xmlNewText ("\n\t\t\t"));
		xmlNewTextChild (newnode, ns, "relevance", "50");
		xmlAddChild (newnode, xmlNewText ("\n\t\t"));
		xmlAddChild (node, xmlNewText ("\n"));
		tok = strtok (NULL, ",");
	}
}

void
print_keys (gchar *key, gchar *value, xmlNodePtr node)
{
	if (strncmp ("short_list_", key, strlen ("short_list_")) == 0) {
		if ((strstr (key, "component") == NULL) 
		    && (strstr (key, "novice_user_level") != NULL)) {
			save_list (value, node);
		}
	} else if (strcmp ("description", key) != 0) {
		xmlNsPtr ns;

		ns = xmlSearchNs (node->doc, node, "gnome");
		if (ns == NULL) {
			g_print ("Uh Oh, namespace not found\n");
		}
		xmlAddChild (node, xmlNewText ("\t\t"));
		xmlNewTextChild (node, ns, key, value);
		xmlAddChild (node, xmlNewText ("\n"));
		//		g_print ("\t%s: %s\n", key, value);
	}
}

void
print_type (gchar *key, GnomeMimeContext *context, xmlNodePtr root)
{
	xmlNodePtr node;

	//	g_print ("%s\n", context->mime_type);
	node = xmlNewChild (root, NULL, "mime-type", NULL);
	xmlNewProp (node, "type", context->mime_type);
	xmlAddChild (node, xmlNewText ("\n"));
	g_hash_table_foreach (context->keys, (GHFunc)print_keys, node);
	xmlAddChild (node, xmlNewText ("\t"));
	xmlAddChild (root, xmlNewText ("\n\n\t"));
	//	g_print ("\n");
}

int
main (int argc, char **argv)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNsPtr mime_ns;
	xmlNsPtr gnome_ns;

	specific_types = NULL;

	gnome_vfs_mime_init ();

	xmlKeepBlanksDefault (0);
	
	doc = xmlNewDoc ("1.0");
	root = xmlNewDocNode (doc, NULL, "mime-info", "");
	mime_ns = xmlNewNs (root, MIME_SPEC_NAMESPACE, NULL);
	gnome_ns = xmlNewNs (root, GNOME_VFS_NAMESPACE, "gnome");
	xmlDocSetRootElement (doc, root);

	xmlAddChild (root, xmlNewText ("\n\n\t"));
	g_hash_table_foreach (specific_types, (GHFunc)print_type, 
			      doc->children);
	xmlAddChild (root, xmlNewText ("\n"));

	xmlSaveFormatFile ("gnome-vfs.xml", doc, 1);


	return 0;
}
