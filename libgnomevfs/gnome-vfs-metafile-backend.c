/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-*/

/* gnome-vfs-metafile-backend.c - server side of GNOME_VFS::Metafile
 *
 * Copyright (C) 2001 Eazel, Inc.
 * Copyright (C) 2001 Free Software Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include "gnome-vfs-metafile-backend.h"

#include "eel-cut-n-paste.h"
#include <gobject/gobject.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-helpers.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <stdlib.h>
#include <string.h>

#define METAFILE_XML_VERSION "1.0"

#define METAFILE_PERMISSIONS (GNOME_VFS_PERM_USER_READ | GNOME_VFS_PERM_USER_WRITE \
			      | GNOME_VFS_PERM_GROUP_READ | GNOME_VFS_PERM_GROUP_WRITE \
			      | GNOME_VFS_PERM_OTHER_READ | GNOME_VFS_PERM_OTHER_WRITE)

#define METAFILES_DIRECTORY_NAME "metafiles"

#define METAFILES_DIRECTORY_PERMISSIONS \
	(GNOME_VFS_PERM_USER_ALL \
         | GNOME_VFS_PERM_GROUP_ALL \
	 | GNOME_VFS_PERM_OTHER_ALL)

static void gnome_vfs_metafile_init       (GnomeVFSMetafile      *metafile);
static void gnome_vfs_metafile_class_init (GnomeVFSMetafileClass *klass);

static void destroy (BonoboObject *metafile);

static CORBA_boolean corba_is_read (PortableServer_Servant  servant,
				    CORBA_Environment      *ev);

static CORBA_char *corba_get		      (PortableServer_Servant  servant,
					       const CORBA_char       *file_name,
					       const CORBA_char       *key,
					       const CORBA_char       *default_value,
					       CORBA_Environment      *ev);
static GNOME_VFS_MetadataList *corba_get_list (PortableServer_Servant  servant,
					       const CORBA_char      *file_name,
					       const CORBA_char      *list_key,
					       const CORBA_char      *list_subkey,
					       CORBA_Environment     *ev);

static void corba_set      (PortableServer_Servant  servant,
			    const CORBA_char       *file_name,
			    const CORBA_char       *key,
			    const CORBA_char       *default_value,
			    const CORBA_char       *metadata,
			    CORBA_Environment      *ev);
static void corba_set_list (PortableServer_Servant       servant,
			    const CORBA_char            *file_name,
			    const CORBA_char            *list_key,
			    const CORBA_char            *list_subkey,
			    const GNOME_VFS_MetadataList *list,
			    CORBA_Environment           *ev);
					       
static void corba_copy             (PortableServer_Servant   servant,
				    const CORBA_char        *source_file_name,
				    const CORBA_char        *destination_directory_uri,
				    const CORBA_char        *destination_file_name,
				    CORBA_Environment       *ev);
static void corba_remove           (PortableServer_Servant  servant,
				    const CORBA_char       *file_name,
				    CORBA_Environment      *ev);
static void corba_rename           (PortableServer_Servant  servant,
				    const CORBA_char       *old_file_name,
				    const CORBA_char       *new_file_name,
				    CORBA_Environment      *ev);
static void corba_rename_directory (PortableServer_Servant  servant,
				    const CORBA_char       *new_directory_uri,
				    CORBA_Environment      *ev);

static void corba_register_monitor   (PortableServer_Servant          servant,
				      const GNOME_VFS_MetafileMonitor  monitor,
				      CORBA_Environment              *ev);
static void corba_unregister_monitor (PortableServer_Servant          servant,
				      const GNOME_VFS_MetafileMonitor  monitor,
				      CORBA_Environment              *ev);

static char    *get_file_metadata      (GnomeVFSMetafile *metafile,
					const char       *file_name,
					const char       *key,
					const char       *default_metadata);
static GList   *get_file_metadata_list (GnomeVFSMetafile *metafile,
					const char       *file_name,
					const char       *list_key,
					const char       *list_subkey);
static gboolean set_file_metadata      (GnomeVFSMetafile *metafile,
					const char       *file_name,
					const char       *key,
					const char       *default_metadata,
					const char       *metadata);
static gboolean set_file_metadata_list (GnomeVFSMetafile *metafile,
					const char       *file_name,
					const char       *list_key,
					const char       *list_subkey,
					GList *list);
					
static void rename_file_metadata (GnomeVFSMetafile *metafile,
				  const char       *old_file_name,
				  const char       *new_file_name);
static void copy_file_metadata   (GnomeVFSMetafile *source_metafile,
				  const char       *source_file_name,
				  GnomeVFSMetafile *destination_metafile,
				  const char       *destination_file_name);
static void remove_file_metadata (GnomeVFSMetafile *metafile,
				  const char       *file_name);

static void call_metafile_changed_for_one_file (GnomeVFSMetafile   *metafile,
						 const CORBA_char  *file_name);

static void metafile_read_restart            (GnomeVFSMetafile *metafile);
static void metafile_read_start              (GnomeVFSMetafile *metafile);
static void metafile_write_start             (GnomeVFSMetafile *metafile);
static void directory_request_write_metafile (GnomeVFSMetafile *metafile);
static void metafile_free_metadata	     (GnomeVFSMetafile *metafile);
static void metafile_read_cancel	     (GnomeVFSMetafile *metafile);
static void async_read_cancel                (GnomeVFSMetafile *metafile);

static void gnome_vfs_metafile_set_metafile_contents (GnomeVFSMetafile *metafile,
					             xmlDocPtr metafile_contents);
char  *gnome_vfs_metafile_make_uri_canonical (const char *uri);

BONOBO_TYPE_FUNC_FULL (GnomeVFSMetafile, GNOME_VFS_Metafile, BONOBO_OBJECT_TYPE, gnome_vfs_metafile)

typedef struct MetafileReadState {
	gboolean use_public_metafile;
	GnomeVFSXReadFileHandle *handle;
	GnomeVFSAsyncHandle *get_file_info_handle;
} MetafileReadState;

typedef struct MetafileWriteState {
	gboolean use_public_metafile;
	GnomeVFSAsyncHandle *handle;
	xmlChar *buffer;
	GnomeVFSFileSize size;
	gboolean write_again;
} MetafileWriteState;

struct GnomeVFSMetafileDetails {
	gboolean is_read;
	
	xmlDoc *xml;
	GHashTable *node_hash;
	GHashTable *changes;

	/* State for reading and writing metadata. */
	MetafileReadState *read_state;
	guint write_idle_id;
	MetafileWriteState *write_state;
	
	GList *monitors;

	GnomeVFSURI *private_vfs_uri;
	GnomeVFSURI *public_vfs_uri;

	char *directory_uri;
	GnomeVFSURI *directory_vfs_uri;
};

static GHashTable *metafiles;

static void
gnome_vfs_metafile_class_init (GnomeVFSMetafileClass *klass)
{
	BONOBO_OBJECT_CLASS (klass)->destroy = destroy;

	klass->epv.is_read            = corba_is_read;
	klass->epv.get                = corba_get;
	klass->epv.get_list           = corba_get_list;
	klass->epv.set                = corba_set;
	klass->epv.set_list           = corba_set_list;
	klass->epv.copy               = corba_copy;
	klass->epv.remove             = corba_remove;
	klass->epv.rename             = corba_rename;
	klass->epv.rename_directory   = corba_rename_directory;
	klass->epv.register_monitor   = corba_register_monitor;
	klass->epv.unregister_monitor = corba_unregister_monitor;
}

static void
gnome_vfs_metafile_init (GnomeVFSMetafile *metafile)
{
	metafile->details = g_new0 (GnomeVFSMetafileDetails, 1);
	
	metafile->details->node_hash = g_hash_table_new (g_str_hash, g_str_equal);	
}

static void
destroy (BonoboObject *object)
{
	GnomeVFSMetafile  *metafile;

	metafile  = GNOME_VFS_METAFILE (object);

	g_assert (metafile->details->write_state == NULL);
	async_read_cancel (metafile);
	g_assert (metafile->details->read_state == NULL);

	if (metafile->details->public_vfs_uri != NULL) {
		gnome_vfs_uri_unref (metafile->details->public_vfs_uri);
	}
	if (metafile->details->private_vfs_uri != NULL) {
		gnome_vfs_uri_unref (metafile->details->private_vfs_uri);
	}
	
	g_hash_table_remove (metafiles, metafile->details->directory_uri);
	
	metafile_free_metadata (metafile);
	g_hash_table_destroy (metafile->details->node_hash);

	g_assert (metafile->details->write_idle_id == 0);

	g_free (metafile->details);

#ifdef METAFILE_CODE_READY
	EEL_CALL_PARENT (BONOBO_OBJECT_CLASS, destroy, (object));
#endif
}

static GnomeVFSURI *
construct_private_metafile_vfs_uri (const char *uri)
{
	GnomeVFSResult result;
	const char *user_directory;
	GnomeVFSURI *user_directory_uri, *metafiles_directory_uri, *alternate_uri;
	char *escaped_uri, *file_name;

	/* Ensure that the metafiles directory exists. */
	user_directory = g_get_home_dir ();
	user_directory_uri = gnome_vfs_uri_new (user_directory);

	metafiles_directory_uri = gnome_vfs_uri_append_file_name (user_directory_uri,
								  METAFILES_DIRECTORY_NAME);
	gnome_vfs_uri_unref (user_directory_uri);
	result = gnome_vfs_x_make_directory_and_parents (metafiles_directory_uri,
					     METAFILES_DIRECTORY_PERMISSIONS);
	if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_FILE_EXISTS) {
		gnome_vfs_uri_unref (metafiles_directory_uri);
		return NULL;
	}

	/* Construct a file name from the URI. */
	escaped_uri = gnome_vfs_escape_slashes (uri);
	file_name = g_strconcat (escaped_uri, ".xml", NULL);
	g_free (escaped_uri);

	/* Construct a URI for something in the "metafiles" directory. */
	alternate_uri = gnome_vfs_uri_append_file_name (metafiles_directory_uri, file_name);
	gnome_vfs_uri_unref (metafiles_directory_uri);
	g_free (file_name);

	return alternate_uri;
}

static void
gnome_vfs_metafile_set_directory_uri (GnomeVFSMetafile *metafile, const char *directory_uri)
{
	GnomeVFSURI *new_vfs_uri;

	if (stolen_strcmp (metafile->details->directory_uri, directory_uri) == 0) {
		return;
	}

	g_free (metafile->details->directory_uri);
	metafile->details->directory_uri = g_strdup (directory_uri);

	new_vfs_uri = gnome_vfs_uri_new (directory_uri);

	if (metafile->details->directory_vfs_uri != NULL) {
		gnome_vfs_uri_unref (metafile->details->directory_vfs_uri);
	}
	metafile->details->directory_vfs_uri = new_vfs_uri;

	if (metafile->details->public_vfs_uri != NULL) {
		gnome_vfs_uri_unref (metafile->details->public_vfs_uri);
	}
	metafile->details->public_vfs_uri = new_vfs_uri == NULL ? NULL
		: gnome_vfs_uri_append_file_name (new_vfs_uri, GNOME_VFS_METAFILE_NAME_SUFFIX);

	if (metafile->details->private_vfs_uri != NULL) {
		gnome_vfs_uri_unref (metafile->details->private_vfs_uri);
	}
	metafile->details->private_vfs_uri
		= construct_private_metafile_vfs_uri (directory_uri);

}

static GnomeVFSMetafile *
gnome_vfs_metafile_new (const char *directory_uri)
{
	GnomeVFSMetafile *metafile;
	
	metafile = GNOME_VFS_METAFILE (g_object_new (GNOME_VFS_TYPE_METAFILE, NULL));

	gnome_vfs_metafile_set_directory_uri (metafile, directory_uri);

	return metafile;
}

/* Cut-n-Paste code from nautilus-directory.c */
char *
gnome_vfs_metafile_make_uri_canonical (const char *uri)
{
	char *canonical_maybe_trailing_slash;
	char *canonical;
	char *with_slashes;
	size_t length;

	canonical_maybe_trailing_slash = gnome_vfs_x_make_uri_canonical (uri);
	canonical = stolen_str_strip_trailing_chr (canonical_maybe_trailing_slash, '/');
	if (strcmp (canonical, canonical_maybe_trailing_slash) != 0) {
		length = strlen (canonical);
		if (length == 0 || canonical[length - 1] == ':') {
			with_slashes = g_strconcat (canonical, "///", NULL);
			g_free (canonical);
			canonical = with_slashes;
		}
	}

	g_free (canonical_maybe_trailing_slash);
	
	return canonical;
}

GnomeVFSMetafile *
gnome_vfs_metafile_get (const char *directory_uri)
{
	GnomeVFSMetafile *metafile;
	char *canonical_uri;
	
	g_return_val_if_fail (directory_uri != NULL, NULL);
	
	if (metafiles == NULL) {
#ifdef METAFILE_CODE_READY
		metafiles = stolen_g_hash_table_new_free_at_exit
			(g_str_hash, g_str_equal, __FILE__ ": metafiles");
#endif
	}
	
	canonical_uri = gnome_vfs_metafile_make_uri_canonical (directory_uri);
	
	metafile = g_hash_table_lookup (metafiles, canonical_uri);
	
	if (metafile != NULL) {
		bonobo_object_ref (BONOBO_OBJECT (metafile));
	} else {
		metafile = gnome_vfs_metafile_new (canonical_uri);
		
		g_assert (strcmp (metafile->details->directory_uri, canonical_uri) == 0);

		g_hash_table_insert (metafiles,
				     metafile->details->directory_uri,
				     metafile);
	}
	
	g_free (canonical_uri);

	return metafile;
}

/* FIXME
 * Right now we only limit the number of conccurrent reads.
 * We may want to consider limiting writes as well.
 */

int num_reads_in_progress;
GList *pending_reads;

#if 0
#define DEBUG_METADATA_IO
#endif

static void
schedule_next_read (void)
{	
	const int kMaxAsyncReads = 10;
	
	GList* node;
	
#ifdef DEBUG_METADATA_IO
		g_message ("schedule_next_read: %d pending reads, %d reads in progress",
			   g_list_length (pending_reads), num_reads_in_progress);
#endif

	if (pending_reads != NULL && num_reads_in_progress <= kMaxAsyncReads) {
		node = pending_reads;
		pending_reads = g_list_remove_link (pending_reads, node);
#ifdef DEBUG_METADATA_IO
		g_message ("schedule_next_read: %s", GNOME_VFS_METAFILE (node->data)->details->directory_uri);
#endif
		metafile_read_start (node->data);
		g_list_free_1 (node);
		++num_reads_in_progress;
	}
}

static void
async_read_start (GnomeVFSMetafile *metafile)
{
	if (metafile->details->is_read
	    || metafile->details->read_state != NULL) {
	    return;
	}
#ifdef DEBUG_METADATA_IO
	g_message ("async_read_start: %s", metafile->details->directory_uri);
#endif
	pending_reads = g_list_prepend (pending_reads, metafile);
	schedule_next_read ();
}

static void
async_read_done (GnomeVFSMetafile *metafile)
{
#ifdef DEBUG_METADATA_IO
	g_message ("async_read_done: %s", metafile->details->directory_uri);
#endif
	--num_reads_in_progress;
	schedule_next_read ();
}

static void
async_read_cancel (GnomeVFSMetafile *metafile)
{
	GList* node;

#ifdef DEBUG_METADATA_IO
	g_message ("async_read_cancel: %s", metafile->details->directory_uri);
#endif
	node = g_list_find (pending_reads, metafile);

	if (node != NULL) {
		pending_reads = g_list_remove_link (pending_reads, node);
		g_list_free_1 (node);
	}
	
	if (metafile->details->read_state != NULL) {
		metafile_read_cancel (metafile);
		async_read_done (metafile);
	}

}

static CORBA_boolean
corba_is_read (PortableServer_Servant  servant,
	       CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));
	return metafile->details->is_read ? CORBA_TRUE : CORBA_FALSE;
}

static CORBA_char *
corba_get (PortableServer_Servant  servant,
	   const CORBA_char       *file_name,
	   const CORBA_char       *key,
	   const CORBA_char       *default_value,
	   CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	char       *metadata;
	CORBA_char *result;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	metadata = get_file_metadata (metafile, file_name, key, default_value);

	result = CORBA_string_dup (metadata != NULL ? metadata : "");

	g_free (metadata);
	
	return result;
}

static GNOME_VFS_MetadataList *
corba_get_list (PortableServer_Servant  servant,
	        const CORBA_char       *file_name,
	        const CORBA_char       *list_key,
	        const CORBA_char       *list_subkey,
	        CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	GList *metadata_list;
	GNOME_VFS_MetadataList *result;
	int	len;
	int	buf_pos;
	GList   *list_ptr;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	metadata_list = get_file_metadata_list (metafile, file_name, list_key, list_subkey);

	len = g_list_length (metadata_list);
	result = GNOME_VFS_MetadataList__alloc ();
	result->_maximum = len;
	result->_length  = len;
	result->_buffer  = CORBA_sequence_CORBA_string_allocbuf (len);

	/* We allocate our buffer with CORBA calls, so the caller will clean it
	 * all up if we set release to TRUE.
	 */
	CORBA_sequence_set_release (result, CORBA_TRUE);

	buf_pos  = 0;
	list_ptr = metadata_list;
	while (list_ptr != NULL) {
		result->_buffer [buf_pos] = CORBA_string_dup (list_ptr->data);
		list_ptr = g_list_next (list_ptr);
		++buf_pos;
	}

	gnome_vfs_list_deep_free (metadata_list);

	return result;
}

static void
corba_set (PortableServer_Servant  servant,
	   const CORBA_char       *file_name,
	   const CORBA_char       *key,
	   const CORBA_char       *default_value,
	   const CORBA_char       *metadata,
	   CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	if (stolen_str_is_empty (default_value)) {
		default_value = NULL;
	}
	if (stolen_str_is_empty (metadata)) {
		metadata = NULL;
	}

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	if (set_file_metadata (metafile, file_name, key, default_value, metadata)) {
		call_metafile_changed_for_one_file (metafile, file_name);
	}
}

static void
corba_set_list (PortableServer_Servant      servant,
		const CORBA_char            *file_name,
		const CORBA_char            *list_key,
		const CORBA_char            *list_subkey,
		const GNOME_VFS_MetadataList *list,
		CORBA_Environment           *ev)
{
	GnomeVFSMetafile  *metafile;

	GList               *metadata_list;
	CORBA_unsigned_long  buf_pos;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	metadata_list = NULL;
	for (buf_pos = 0; buf_pos < list->_length; ++buf_pos) {
		metadata_list = g_list_prepend (metadata_list, list->_buffer [buf_pos]);
	}
	metadata_list = g_list_reverse (metadata_list);
	
	if (set_file_metadata_list (metafile, file_name, list_key, list_subkey, metadata_list)) {
		call_metafile_changed_for_one_file (metafile, file_name);
	}
	
	g_list_free (metadata_list);
}
					       
static void
corba_copy (PortableServer_Servant   servant,
	    const CORBA_char        *source_file_name,
	    const CORBA_char        *destination_directory_uri,
	    const CORBA_char        *destination_file_name,
	    CORBA_Environment       *ev)
{
	GnomeVFSMetafile *source_metafile;
	GnomeVFSMetafile *destination_metafile;

	source_metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));
	
	destination_metafile = gnome_vfs_metafile_get (destination_directory_uri);

	copy_file_metadata (source_metafile, source_file_name,
			    destination_metafile, destination_file_name);
			    
	bonobo_object_unref (BONOBO_OBJECT (destination_metafile));
}

static void
corba_remove (PortableServer_Servant  servant,
	      const CORBA_char       *file_name,
	      CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	metafile =  GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));
	
	remove_file_metadata (metafile, file_name);
}

static void
corba_rename (PortableServer_Servant  servant,
	      const CORBA_char       *old_file_name,
	      const CORBA_char       *new_file_name,
	      CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	rename_file_metadata (metafile, old_file_name, new_file_name);
}

static void
corba_rename_directory (PortableServer_Servant  servant,
	      const CORBA_char       *new_directory_uri,
	      CORBA_Environment      *ev)
{
	GnomeVFSMetafile  *metafile;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	gnome_vfs_metafile_set_directory_uri (metafile, new_directory_uri);
}

static GList *
find_monitor_node (GList *monitors, const GNOME_VFS_MetafileMonitor monitor)
{
	GList                    *node;
	CORBA_Environment	  ev;
	GNOME_VFS_MetafileMonitor  cur_monitor;		

	CORBA_exception_init (&ev);

	for (node = monitors; node != NULL; node = node->next) {
		cur_monitor = node->data;
		if (CORBA_Object_is_equivalent (cur_monitor, monitor, &ev)) {
			break;
		}
	}
	
	/* FIXME bugzilla.eazel.com 6664: examine ev for errors */

	CORBA_exception_free (&ev);
	
	return node;
}

static void
corba_register_monitor (PortableServer_Servant          servant,
			const GNOME_VFS_MetafileMonitor  monitor,
			CORBA_Environment              *ev)
{
	GnomeVFSMetafile          *metafile;
	
	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));

	g_return_if_fail (find_monitor_node (metafile->details->monitors, monitor) == NULL);

	metafile->details->monitors = g_list_prepend (metafile->details->monitors, (gpointer) CORBA_Object_duplicate (monitor, ev));	

	async_read_start (metafile);
}

static void
corba_unregister_monitor (PortableServer_Servant          servant,
			  const GNOME_VFS_MetafileMonitor  monitor,
			  CORBA_Environment              *ev)
{
	GnomeVFSMetafile          *metafile;
	GList                     *node;

	metafile  = GNOME_VFS_METAFILE (bonobo_object_from_servant (servant));
	
	node = find_monitor_node (metafile->details->monitors, monitor);

	g_return_if_fail (node != NULL);

	metafile->details->monitors = g_list_remove_link (metafile->details->monitors, node);

	CORBA_Object_release (node->data, ev);
	g_list_free_1 (node);
}

static void
gnome_vfs_metafile_notify_metafile_ready (GnomeVFSMetafile *metafile)
{
	GList                     *node;
	CORBA_Environment          ev;
	GNOME_VFS_MetafileMonitor   monitor;		

	CORBA_exception_init (&ev);
	
	for (node = metafile->details->monitors; node != NULL; node = node->next) {
		monitor = node->data;
		GNOME_VFS_MetafileMonitor_metafile_ready (monitor, &ev);
		/* FIXME bugzilla.eazel.com 6664: examine ev for errors */
	}
	
	CORBA_exception_free (&ev);
}

static void
call_metafile_changed (GnomeVFSMetafile *metafile,
		       const GNOME_VFS_FileNameList  *file_names)
{
	GList                     *node;
	CORBA_Environment          ev;
	GNOME_VFS_MetafileMonitor   monitor;		

	CORBA_exception_init (&ev);
	
	for (node = metafile->details->monitors; node != NULL; node = node->next) {
		monitor = node->data;
		GNOME_VFS_MetafileMonitor_metafile_changed (monitor, file_names, &ev);
		/* FIXME bugzilla.eazel.com 6664: examine ev for errors */
	}
	
	CORBA_exception_free (&ev);
}
#if 0

static void
file_list_filler_ghfunc (gpointer key,
			 gpointer value,
			 gpointer user_data)
{
	GNOME_VFS_FileNameList *file_names;

	file_names = user_data;

	file_names->_buffer [file_names->_length] = key;
	
	++file_names->_length;
}

void
call_metafile_changed_for_all_files_mentioned_in_metafile (GnomeVFSMetafile *metafile)
{
	CORBA_unsigned_long   len;
	GNOME_VFS_FileNameList file_names;

	len = g_hash_table_size (metafile->details->node_hash);

	if (len > 0) {
		file_names._maximum =  len;
		file_names._length  =  0;
		file_names._buffer  =  g_new (CORBA_char *, len);

		g_hash_table_foreach (metafile->details->node_hash,
				      file_list_filler_ghfunc,
				      &file_names);

		call_metafile_changed (metafile, &file_names);

		g_free (file_names._buffer);
	}
}
#endif

static void
call_metafile_changed_for_one_file (GnomeVFSMetafile *metafile,
				    const CORBA_char  *file_name)
{
	GNOME_VFS_FileNameList file_names = {0};
	
	file_names._maximum =  1;
	file_names._length  =  1;
	file_names._buffer  =  (CORBA_char **) &file_name;

	call_metafile_changed (metafile, &file_names);
}

typedef struct {
	gboolean is_list;
	union {
		char *string;
		GList *string_list;
	} value;
	char *default_value;
} MetadataValue;

static char *
get_metadata_from_node (xmlNode *node,
			const char *key,
			const char *default_metadata)
{
	xmlChar *property;
	char *result;

	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (key[0] != '\0', NULL);

	property = xmlGetProp (node, key);
	if (property == NULL) {
		result = g_strdup (default_metadata);
	} else {
		result = g_strdup (property);
	}
	xmlFree (property);

	return result;
}

static GList *
get_metadata_list_from_node (xmlNode *node,
			     const char *list_key,
			     const char *list_subkey)
{
	return stolen_xml_get_property_for_children
		(node, list_key, list_subkey);
}

static xmlNode *
create_metafile_root (GnomeVFSMetafile *metafile)
{
	xmlNode *root;

	if (metafile->details->xml == NULL) {
		gnome_vfs_metafile_set_metafile_contents (metafile, xmlNewDoc (METAFILE_XML_VERSION));
	}
	root = xmlDocGetRootElement (metafile->details->xml);
	if (root == NULL) {
		root = xmlNewDocNode (metafile->details->xml, NULL, "directory", NULL);
		xmlDocSetRootElement (metafile->details->xml, root);
	}

	return root;
}

static xmlNode *
get_file_node (GnomeVFSMetafile *metafile,
	       const char *file_name,
	       gboolean create)
{
	GHashTable *hash;
	xmlNode *root, *node;
	
	g_assert (GNOME_VFS_IS_METAFILE (metafile));

	hash = metafile->details->node_hash;
	node = g_hash_table_lookup (hash, file_name);
	if (node != NULL) {
		return node;
	}
	
	if (create) {
		root = create_metafile_root (metafile);
		node = xmlNewChild (root, NULL, "file", NULL);
		xmlSetProp (node, "name", file_name);
		g_hash_table_insert (hash, xmlMemStrdup (file_name), node);
		return node;
	}
	
	return NULL;
}

static char *
get_metadata_string_from_metafile (GnomeVFSMetafile *metafile,
				   const char *file_name,
				   const char *key,
				   const char *default_metadata)
{
	xmlNode *node;

	node = get_file_node (metafile, file_name, FALSE);
	return get_metadata_from_node (node, key, default_metadata);
}

static GList *
get_metadata_list_from_metafile (GnomeVFSMetafile *metafile,
				 const char *file_name,
				 const char *list_key,
				 const char *list_subkey)
{
	xmlNode *node;

	node = get_file_node (metafile, file_name, FALSE);
	return get_metadata_list_from_node (node, list_key, list_subkey);
}

static gboolean
set_metadata_string_in_metafile (GnomeVFSMetafile *metafile,
				 const char *file_name,
				 const char *key,
				 const char *default_metadata,
				 const char *metadata)
{
	char *old_metadata;
	gboolean old_metadata_matches;
	const char *value;
	xmlNode *node;
	xmlAttr *property_node;

	/* If the data in the metafile is already correct, do nothing. */
	old_metadata = get_file_metadata
		(metafile, file_name, key, default_metadata);

	old_metadata_matches = stolen_strcmp (old_metadata, metadata) == 0;
	g_free (old_metadata);
	if (old_metadata_matches) {
		return FALSE;
	}

	/* Data that matches the default is represented in the tree by
	 * the lack of an attribute.
	 */
	if (stolen_strcmp (default_metadata, metadata) == 0) {
		value = NULL;
	} else {
		value = metadata;
	}

	/* Get or create the node. */
	node = get_file_node (metafile, file_name, value != NULL);

	/* Add or remove a property node. */
	if (node != NULL) {
		property_node = xmlSetProp (node, key, value);
		if (value == NULL) {
			xmlRemoveProp (property_node);
		}
	}
	
	/* Since we changed the tree, arrange for it to be written. */
	directory_request_write_metafile (metafile);
	return TRUE;
}

static gboolean
set_metadata_list_in_metafile (GnomeVFSMetafile *metafile,
			       const char *file_name,
			       const char *list_key,
			       const char *list_subkey,
			       GList *list)
{
	xmlNode *node, *child, *next;
	gboolean changed;
	GList *p;
	xmlChar *property;

	/* Get or create the node. */
	node = get_file_node (metafile, file_name, list != NULL);

	/* Work with the list. */
	changed = FALSE;
	if (node == NULL) {
		g_assert (list == NULL);
	} else {
		p = list;

		/* Remove any nodes except the ones we expect. */
		for (child = stolen_xml_get_children (node);
		     child != NULL;
		     child = next) {

			next = child->next;
			if (strcmp (child->name, list_key) == 0) {
				property = xmlGetProp (child, list_subkey);
				if (property != NULL && p != NULL
				    && strcmp (property, (char *) p->data) == 0) {
					p = p->next;
				} else {
					xmlUnlinkNode (child);
					xmlFreeNode (child);
					changed = TRUE;
				}
				xmlFree (property);
			}
		}
		
		/* Add any additional nodes needed. */
		for (; p != NULL; p = p->next) {
			child = xmlNewChild (node, NULL, list_key, NULL);
			xmlSetProp (child, list_subkey, p->data);
			changed = TRUE;
		}
	}

	if (!changed) {
		return FALSE;
	}

	directory_request_write_metafile (metafile);
	return TRUE;
}

static MetadataValue *
metadata_value_new (const char *default_metadata, const char *metadata)
{
	MetadataValue *value;

	value = g_new0 (MetadataValue, 1);

	value->default_value = g_strdup (default_metadata);
	value->value.string = g_strdup (metadata);

	return value;
}

static MetadataValue *
metadata_value_new_list (GList *metadata)
{
	MetadataValue *value;

	value = g_new0 (MetadataValue, 1);

	value->is_list = TRUE;
#ifdef METAFILE_CODE_READY
	value->value.string_list = stolen_g_str_list_copy (metadata);
#endif

	return value;
}

static void
metadata_value_destroy (MetadataValue *value)
{
	if (value == NULL) {
		return;
	}

	if (!value->is_list) {
		g_free (value->value.string);
	} else {
		gnome_vfs_list_deep_free (value->value.string_list);
	}
	g_free (value->default_value);
	g_free (value);
}

static gboolean
metadata_value_equal (const MetadataValue *value_a,
		      const MetadataValue *value_b)
{
	if (value_a->is_list != value_b->is_list) {
		return FALSE;
	}

	if (!value_a->is_list) {
		return stolen_strcmp (value_a->value.string,
					value_b->value.string) == 0
			&& stolen_strcmp (value_a->default_value,
					    value_b->default_value) == 0;
	} else {
		g_assert (value_a->default_value == NULL);
		g_assert (value_b->default_value == NULL);

#ifdef METAFILE_CODE_READY
		return stolen_g_str_list_equal
			(value_a->value.string_list,
			 value_b->value.string_list);
#else
		return FALSE;
#endif
	}
}

static gboolean
set_metadata_in_metafile (GnomeVFSMetafile *metafile,
			  const char *file_name,
			  const char *key,
			  const char *subkey,
			  const MetadataValue *value)
{
	gboolean changed;

	if (!value->is_list) {
		g_assert (subkey == NULL);
		changed = set_metadata_string_in_metafile
			(metafile, file_name, key,
			 value->default_value,
			 value->value.string);
	} else {
		g_assert (value->default_value == NULL);
		changed = set_metadata_list_in_metafile
			(metafile, file_name, key, subkey,
			 value->value.string_list);
	}

	return changed;
}

static char *
get_metadata_string_from_table (GnomeVFSMetafile *metafile,
				const char *file_name,
				const char *key,
				const char *default_metadata)
{
	GHashTable *directory_table, *file_table;
	MetadataValue *value;

	/* Get the value from the hash table. */
	directory_table = metafile->details->changes;
        file_table = directory_table == NULL ? NULL
		: g_hash_table_lookup (directory_table, file_name);
	value = file_table == NULL ? NULL
		: g_hash_table_lookup (file_table, key);
	if (value == NULL) {
		return g_strdup (default_metadata);
	}
	
	/* Convert it to a string. */
	g_assert (!value->is_list);
	if (stolen_strcmp (value->value.string, value->default_value) == 0) {
		return g_strdup (default_metadata);
	}
	return g_strdup (value->value.string);
}

static GList *
get_metadata_list_from_table (GnomeVFSMetafile *metafile,
			      const char *file_name,
			      const char *key,
			      const char *subkey)
{
	GHashTable *directory_table, *file_table;
	char *combined_key;
	MetadataValue *value;

	/* Get the value from the hash table. */
	directory_table = metafile->details->changes;
        file_table = directory_table == NULL ? NULL
		: g_hash_table_lookup (directory_table, file_name);
	if (file_table == NULL) {
		return NULL;
	}
	combined_key = g_strconcat (key, "/", subkey, NULL);
	value = g_hash_table_lookup (file_table, combined_key);
	g_free (combined_key);
	if (value == NULL) {
		return NULL;
	}

	/* Copy the list and return it. */
	g_assert (value->is_list);
#ifdef METAFILE_CODE_READY
	return stolen_g_str_list_copy (value->value.string_list);
#else
	return NULL;
#endif
}

static guint
str_or_null_hash (gconstpointer str)
{
	return str == NULL ? 0 : g_str_hash (str);
}

static gboolean
str_or_null_equal (gconstpointer str_a, gconstpointer str_b)
{
	if (str_a == NULL) {
		return str_b == NULL;
	}
	if (str_b == NULL) {
		return FALSE;
	}
	return g_str_equal (str_a, str_b);
}

static gboolean
set_metadata_eat_value (GnomeVFSMetafile *metafile,
			const char *file_name,
			const char *key,
			const char *subkey,
			MetadataValue *value)
{
	GHashTable *directory_table, *file_table;
	gboolean changed;
	char *combined_key;
	MetadataValue *old_value;

	if (metafile->details->is_read) {
		changed = set_metadata_in_metafile
			(metafile, file_name, key, subkey, value);
		metadata_value_destroy (value);
	} else {
		/* Create hash table only when we need it.
		 * We'll destroy it when we finish reading the metafile.
		 */
		directory_table = metafile->details->changes;
		if (directory_table == NULL) {
			directory_table = g_hash_table_new
				(str_or_null_hash, str_or_null_equal);
			metafile->details->changes = directory_table;
		}
		file_table = g_hash_table_lookup (directory_table, file_name);
		if (file_table == NULL) {
			file_table = g_hash_table_new (g_str_hash, g_str_equal);
			g_hash_table_insert (directory_table,
					     g_strdup (file_name), file_table);
		}

		/* Find the entry in the hash table. */
		if (subkey == NULL) {
			combined_key = g_strdup (key);
		} else {
			combined_key = g_strconcat (key, "/", subkey, NULL);
		}
		old_value = g_hash_table_lookup (file_table, combined_key);

		/* Put the change into the hash. Delete the old change. */
		changed = old_value == NULL || !metadata_value_equal (old_value, value);
		if (changed) {
			g_hash_table_insert (file_table, combined_key, value);
			if (old_value != NULL) {
				/* The hash table keeps the old key. */
				g_free (combined_key);
				metadata_value_destroy (old_value);
			}
		} else {
			g_free (combined_key);
			metadata_value_destroy (value);
		}
	}

	return changed;
}

static void
free_file_table_entry (gpointer key, gpointer value, gpointer user_data)
{
	g_assert (user_data == NULL);

	g_free (key);
	metadata_value_destroy (value);
}

static void
free_directory_table_entry (gpointer key, gpointer value, gpointer user_data)
{
	g_assert (user_data == NULL);
	g_assert (value != NULL);

	g_free (key);
	g_hash_table_foreach (value, free_file_table_entry, NULL);
	g_hash_table_destroy (value);
}

static void
destroy_metadata_changes_hash_table (GHashTable *directory_table)
{
	if (directory_table == NULL) {
		return;
	}
	g_hash_table_foreach (directory_table, free_directory_table_entry, NULL);
	g_hash_table_destroy (directory_table);
}

static void
destroy_xml_string_key (gpointer key, gpointer value, gpointer user_data)
{
	g_assert (key != NULL);
	g_assert (user_data == NULL);
	g_assert (value != NULL);

	xmlFree (key);
}

static void
metafile_free_metadata (GnomeVFSMetafile *metafile)
{
	g_return_if_fail (GNOME_VFS_IS_METAFILE (metafile));

	g_hash_table_foreach (metafile->details->node_hash,
			      destroy_xml_string_key, NULL);
	xmlFreeDoc (metafile->details->xml);
	destroy_metadata_changes_hash_table (metafile->details->changes);
}

static char *
get_file_metadata (GnomeVFSMetafile *metafile,
		   const char *file_name,
		   const char *key,
		   const char *default_metadata)
{
	g_return_val_if_fail (GNOME_VFS_IS_METAFILE (metafile), NULL);
	g_return_val_if_fail (!stolen_str_is_empty (file_name), NULL);
	g_return_val_if_fail (!stolen_str_is_empty (key), NULL);

	if (metafile->details->is_read) {
		return get_metadata_string_from_metafile
			(metafile, file_name, key, default_metadata);
	} else {
		return get_metadata_string_from_table
			(metafile, file_name, key, default_metadata);
	}
}

static GList *
get_file_metadata_list (GnomeVFSMetafile *metafile,
			const char *file_name,
			const char *list_key,
			const char *list_subkey)
{
	g_return_val_if_fail (GNOME_VFS_IS_METAFILE (metafile), NULL);
	g_return_val_if_fail (!stolen_str_is_empty (file_name), NULL);
	g_return_val_if_fail (!stolen_str_is_empty (list_key), NULL);
	g_return_val_if_fail (!stolen_str_is_empty (list_subkey), NULL);

	if (metafile->details->is_read) {
		return get_metadata_list_from_metafile
			(metafile, file_name, list_key, list_subkey);
	} else {
		return get_metadata_list_from_table
			(metafile, file_name, list_key, list_subkey);
	}
}

static gboolean
set_file_metadata (GnomeVFSMetafile *metafile,
		   const char *file_name,
		   const char *key,
		   const char *default_metadata,
		   const char *metadata)
{
	MetadataValue *value;

	g_return_val_if_fail (GNOME_VFS_IS_METAFILE (metafile), FALSE);
	g_return_val_if_fail (!stolen_str_is_empty (file_name), FALSE);
	g_return_val_if_fail (!stolen_str_is_empty (key), FALSE);

	if (metafile->details->is_read) {
		return set_metadata_string_in_metafile (metafile, file_name, key,
							default_metadata, metadata);
	} else {
		value = metadata_value_new (default_metadata, metadata);
		return set_metadata_eat_value (metafile, file_name,
					       key, NULL, value);
	}
}

static gboolean
set_file_metadata_list (GnomeVFSMetafile *metafile,
			const char *file_name,
			const char *list_key,
			const char *list_subkey,
			GList *list)
{
	MetadataValue *value;

	g_return_val_if_fail (GNOME_VFS_IS_METAFILE (metafile), FALSE);
	g_return_val_if_fail (!stolen_str_is_empty (file_name), FALSE);
	g_return_val_if_fail (!stolen_str_is_empty (list_key), FALSE);
	g_return_val_if_fail (!stolen_str_is_empty (list_subkey), FALSE);

	if (metafile->details->is_read) {
		return set_metadata_list_in_metafile (metafile, file_name,
						      list_key, list_subkey, list);
	} else {
		value = metadata_value_new_list (list);
		return set_metadata_eat_value (metafile, file_name,
					       list_key, list_subkey, value);
	}
}

static void
rename_file_metadata (GnomeVFSMetafile *metafile,
		      const char *old_file_name,
		      const char *new_file_name)
{
	gboolean found;
	gpointer key, value;
	xmlNode *file_node;
	GHashTable *hash;

	g_return_if_fail (GNOME_VFS_IS_METAFILE (metafile));
	g_return_if_fail (old_file_name != NULL);
	g_return_if_fail (new_file_name != NULL);

	remove_file_metadata (metafile, new_file_name);

	if (metafile->details->is_read) {
		/* Move data in XML document if present. */
		hash = metafile->details->node_hash;
		found = g_hash_table_lookup_extended
			(hash, old_file_name, &key, &value);
		if (found) {
			g_assert (strcmp ((const char *) key, old_file_name) == 0);
			file_node = value;
			g_hash_table_remove (hash,
					     old_file_name);
			xmlFree (key);
			g_hash_table_insert (hash,
					     xmlMemStrdup (new_file_name), value);
			xmlSetProp (file_node, "name", new_file_name);
			directory_request_write_metafile (metafile);
		}
	} else {
		/* Move data in hash table. */
		/* FIXME: If there's data for this file in the
		 * metafile on disk, this doesn't arrange for that
		 * data to be moved to the new name.
		 */
		hash = metafile->details->changes;
		found = g_hash_table_lookup_extended
			(hash, old_file_name, &key, &value);
		if (found) {
			g_hash_table_remove (hash, old_file_name);
			g_free (key);
			g_hash_table_insert (hash, g_strdup (new_file_name), value);
		}
	}
}

typedef struct {
	GnomeVFSMetafile *metafile;
	const char *file_name;
} ChangeContext;

static void
apply_one_change (gpointer key, gpointer value, gpointer callback_data)
{
	ChangeContext *context;
	const char *hash_table_key, *separator, *metadata_key, *subkey;
	char *key_prefix;

	g_assert (key != NULL);
	g_assert (value != NULL);
	g_assert (callback_data != NULL);

	context = callback_data;

	/* Break the key in half. */
	hash_table_key = key;
	separator = strchr (hash_table_key, '/');
	if (separator == NULL) {
		key_prefix = NULL;
		metadata_key = hash_table_key;
		subkey = NULL;
	} else {
		key_prefix = g_strndup (hash_table_key, separator - hash_table_key);
		metadata_key = key_prefix;
		subkey = separator + 1;
	}

	/* Set the metadata. */
	set_metadata_in_metafile (context->metafile, context->file_name,
				  metadata_key, subkey, value);
	g_free (key_prefix);
}

static void
apply_file_changes (GnomeVFSMetafile *metafile,
		    const char *file_name,
		    GHashTable *changes)
{
	ChangeContext context;

	g_assert (GNOME_VFS_IS_METAFILE (metafile));
	g_assert (file_name != NULL);
	g_assert (changes != NULL);

	context.metafile = metafile;
	context.file_name = file_name;

	g_hash_table_foreach (changes, apply_one_change, &context);
}

static void
apply_one_file_changes (gpointer key, gpointer value, gpointer callback_data)
{
	apply_file_changes (callback_data, key, value);
	g_hash_table_destroy (value);
}

static void
gnome_vfs_metafile_apply_pending_changes (GnomeVFSMetafile *metafile)
{
	if (metafile->details->changes == NULL) {
		return;
	}
	g_hash_table_foreach (metafile->details->changes,
			      apply_one_file_changes, metafile);
	g_hash_table_destroy (metafile->details->changes);
	metafile->details->changes = NULL;
}

static void
copy_file_metadata (GnomeVFSMetafile *source_metafile,
		    const char *source_file_name,
		    GnomeVFSMetafile *destination_metafile,
		    const char *destination_file_name)
{
	xmlNodePtr source_node, node, root;
	GHashTable *hash, *changes;

	g_return_if_fail (GNOME_VFS_IS_METAFILE (source_metafile));
	g_return_if_fail (source_file_name != NULL);
	g_return_if_fail (GNOME_VFS_IS_METAFILE (destination_metafile));
	g_return_if_fail (destination_file_name != NULL);

	/* FIXME bugzilla.eazel.com 3343: This does not properly
	 * handle the case where we don't have the source metadata yet
	 * since it's not read in.
	 */

	remove_file_metadata
		(destination_metafile, destination_file_name);
	g_assert (get_file_node (destination_metafile, destination_file_name, FALSE) == NULL);

	source_node = get_file_node (source_metafile, source_file_name, FALSE);
	if (source_node != NULL) {
		if (destination_metafile->details->is_read) {
			node = xmlCopyNode (source_node, TRUE);
			root = create_metafile_root (destination_metafile);
			xmlAddChild (root, node);
			xmlSetProp (node, "name", destination_file_name);
			g_hash_table_insert (destination_metafile->details->node_hash,
					     xmlMemStrdup (destination_file_name), node);
		} else {
			/* FIXME bugzilla.eazel.com 6526: Copying data into a destination
			 * where the metafile was not yet read is not implemented.
			 */
			g_warning ("not copying metadata");
		}
	}

	hash = source_metafile->details->changes;
	if (hash != NULL) {
		changes = g_hash_table_lookup (hash, source_file_name);
		if (changes != NULL) {
			apply_file_changes (destination_metafile,
					    destination_file_name,
					    changes);
		}
	}

	/* FIXME: Do we want to copy the thumbnail here like in the
	 * rename and remove cases?
	 */
}

static void
remove_file_metadata (GnomeVFSMetafile *metafile,
		      const char *file_name)
{
	gboolean found;
	gpointer key, value;
	xmlNode *file_node;
	GHashTable *hash;

	g_return_if_fail (GNOME_VFS_IS_METAFILE (metafile));
	g_return_if_fail (file_name != NULL);

	if (metafile->details->is_read) {
		/* Remove data in XML document if present. */
		hash = metafile->details->node_hash;
		found = g_hash_table_lookup_extended
			(hash, file_name, &key, &value);
		if (found) {
			g_assert (strcmp ((const char *) key, file_name) == 0);
			file_node = value;
			g_hash_table_remove (hash,
					     file_name);
			xmlFree (key);
			stolen_xml_remove_node (file_node);
			xmlFreeNode (file_node);
			directory_request_write_metafile (metafile);
		}
	} else {
		/* Remove data from hash table. */
		/* FIXME: If there's data for this file on the
		 * metafile on disk, this does not arrange for it to
		 * be removed when the metafile is later read.
		 */
		hash = metafile->details->changes;
		if (hash != NULL) {
			found = g_hash_table_lookup_extended
				(hash, file_name, &key, &value);
			if (found) {
				g_hash_table_remove (hash, file_name);
				g_free (key);
				metadata_value_destroy (value);
			}
		}
	}
}

static void
gnome_vfs_metafile_set_metafile_contents (GnomeVFSMetafile *metafile,
					 xmlDocPtr metafile_contents)
{
	GHashTable *hash;
	xmlNodePtr node;
	xmlChar *name;

	g_return_if_fail (GNOME_VFS_IS_METAFILE (metafile));
	g_return_if_fail (metafile->details->xml == NULL);

	if (metafile_contents == NULL) {
		return;
	}

	metafile->details->xml = metafile_contents;
	
	/* Populate the node hash table. */
	hash = metafile->details->node_hash;
	for (node = stolen_xml_get_root_children (metafile_contents);
	     node != NULL; node = node->next) {
		if (strcmp (node->name, "file") == 0) {
			name = xmlGetProp (node, "name");
			if (g_hash_table_lookup (hash, name) != NULL) {
				xmlFree (name);
				/* FIXME: Should we delete duplicate nodes as we discover them? */
			} else {
				g_hash_table_insert (hash, name, node);
			}
		}
	}
}


static void
metafile_read_cancel (GnomeVFSMetafile *metafile)
{
	if (metafile->details->read_state != NULL) {
		if (metafile->details->read_state->handle != NULL) {
			gnome_vfs_x_read_file_cancel (metafile->details->read_state->handle);
		}
		if (metafile->details->read_state->get_file_info_handle != NULL) {
			gnome_vfs_async_cancel (metafile->details->read_state->get_file_info_handle);
		}
		g_free (metafile->details->read_state);
		metafile->details->read_state = NULL;
	}
}

static gboolean
can_use_public_metafile (GnomeVFSMetafile *metafile)
{
#ifdef METAFILE_CODE_READY
	NautilusSpeedTradeoffValue preference_value;
	
	g_return_val_if_fail (GNOME_VFS_IS_METAFILE (metafile), FALSE);

	if (metafile->details->public_vfs_uri == NULL) {
		return FALSE;
	}

	preference_value = stolen_preferences_get_integer (GNOME_VFS_PREFERENCES_USE_PUBLIC_METADATA);

	if (preference_value == NAUTILUS_SPEED_TRADEOFF_ALWAYS) {
		return TRUE;
	}
	
	if (preference_value == NAUTILUS_SPEED_TRADEOFF_NEVER) {
		return FALSE;
	}

	g_assert (preference_value == NAUTILUS_SPEED_TRADEOFF_LOCAL_ONLY);

	return metafile->details->directory_vfs_uri == NULL ||
		gnome_vfs_uri_is_local (metafile->details->directory_vfs_uri);
#else
	return TRUE;
#endif
}

static void
metafile_read_mark_done (GnomeVFSMetafile *metafile)
{
	g_free (metafile->details->read_state);
	metafile->details->read_state = NULL;	

	metafile->details->is_read = TRUE;

	/* Move over the changes to the metafile that were in the hash table. */
	gnome_vfs_metafile_apply_pending_changes (metafile);

	/* Tell change-watchers that we have update information. */
	gnome_vfs_metafile_notify_metafile_ready (metafile);

	async_read_done (metafile);
}

static void
metafile_read_done (GnomeVFSMetafile *metafile)
{
	metafile_read_mark_done (metafile);
}

static void
metafile_read_try_public_metafile (GnomeVFSMetafile *metafile)
{
	metafile->details->read_state->use_public_metafile = TRUE;
	metafile_read_restart (metafile);
}

static void
metafile_read_check_for_directory_callback (GnomeVFSAsyncHandle *handle,
					    GList *results,
					    gpointer callback_data)
{
	GnomeVFSMetafile *metafile;
	GnomeVFSGetFileInfoResult *result;

	metafile = GNOME_VFS_METAFILE (callback_data);

	g_assert (metafile->details->read_state->get_file_info_handle == handle);
#ifdef METAFILE_CODE_READY
	g_assert (stolen_g_list_exactly_one_item (results));
#endif

	metafile->details->read_state->get_file_info_handle = NULL;

	result = results->data;

	if (result->result == GNOME_VFS_OK
	    && ((result->file_info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_TYPE) != 0)
	    && result->file_info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
		/* Is a directory. */
		metafile_read_try_public_metafile (metafile);
	} else {
		/* Not a directory. */
		metafile_read_done (metafile);
	}
}

static void
metafile_read_check_for_directory (GnomeVFSMetafile *metafile)
{
	GList fake_list;

	/* We only get here if the public metafile is in question,
	 * which in turn only happens if the URI is one that gnome-vfs
	 * can handle.
	 */
	g_assert (metafile->details->directory_vfs_uri != NULL);

	/* We have to do a get_info call to check if this a directory. */
	fake_list.data = metafile->details->directory_vfs_uri;
	fake_list.next = NULL;
	fake_list.prev = NULL;
	gnome_vfs_async_get_file_info
		(&metafile->details->read_state->get_file_info_handle,
		 &fake_list,
		 GNOME_VFS_FILE_INFO_DEFAULT,
		 metafile_read_check_for_directory_callback,
		 metafile);
}

static void
metafile_read_failed (GnomeVFSMetafile *metafile)
{
	gboolean need_directory_check, is_directory;

	g_assert (GNOME_VFS_IS_METAFILE (metafile));

	metafile->details->read_state->handle = NULL;

	if (!metafile->details->read_state->use_public_metafile
	    && can_use_public_metafile (metafile)) {
		/* The goal here is to read the real metafile, but
		 * only if the directory is actually a directory.
		 */

		/* First, check if we already know if it a directory. */
		/* I don't think we can do this at the GnomeVFS level */
		/* oh well, you win some, you lose some...
		   file = nautilus_file_get (metafile->details->directory_uri);
		   if (file == NULL || file->details->is_gone) {
		   need_directory_check = FALSE;
		   is_directory = FALSE;
		   } else if (file->details->info == NULL) {
		   need_directory_check = TRUE;
		   is_directory = TRUE;
		   } else {
		   need_directory_check = FALSE;
		   is_directory = nautilus_file_is_directory (file);
		   }
		   nautilus_file_unref (file); */
		need_directory_check = TRUE;
		is_directory = TRUE;
		/* Do the directory check if we don't know. */
		if (need_directory_check) {
			metafile_read_check_for_directory (metafile);
			return;
		}

		/* Try for the public metafile if it is a directory. */
		if (is_directory) {
			metafile_read_try_public_metafile (metafile);
			return;
		}
	}

	metafile_read_done (metafile);
}

static void
metafile_read_done_callback (GnomeVFSResult result,
			     GnomeVFSFileSize file_size,
			     char *file_contents,
			     gpointer callback_data)
{
	GnomeVFSMetafile *metafile;
	int size;
	char *buffer;

	metafile = GNOME_VFS_METAFILE (callback_data);
	g_assert (metafile->details->xml == NULL);

	if (result != GNOME_VFS_OK) {
		g_assert (file_contents == NULL);
		metafile_read_failed (metafile);
		return;
	}

	size = file_size;
	if ((GnomeVFSFileSize) size != file_size) {
		g_free (file_contents);
		metafile_read_failed (metafile);
		return;
	}
	
	/* The gnome-xml parser requires a zero-terminated array. */
	buffer = g_realloc (file_contents, size + 1);
	buffer[size] = '\0';
	gnome_vfs_metafile_set_metafile_contents (metafile,
						 xmlParseMemory (buffer, size));
	g_free (buffer);

	metafile_read_done (metafile);
}

static void
metafile_read_restart (GnomeVFSMetafile *metafile)
{
	char *text_uri;

	text_uri = gnome_vfs_uri_to_string
		(metafile->details->read_state->use_public_metafile
		 ? metafile->details->public_vfs_uri
		 : metafile->details->private_vfs_uri,
		 GNOME_VFS_URI_HIDE_NONE);

#ifdef METAFILE_CODE_READY
	metafile->details->read_state->handle = stolen_read_entire_file_async
		(text_uri, metafile_read_done_callback, metafile);
#else
	metafile_read_done_callback (0, 0, 0, 0); /* quiet compiler */
#endif

	g_free (text_uri);
}

static void
metafile_read_start (GnomeVFSMetafile *metafile)
{
	g_assert (GNOME_VFS_IS_METAFILE (metafile));

	if (metafile->details->is_read
	    || metafile->details->read_state != NULL) {
		return;
	}

	metafile->details->read_state = g_new0 (MetafileReadState, 1);
	metafile_read_restart (metafile);

#ifdef METAFILE_CODE_READY
	/* FIXME: we need a new mechanism for knowing if we allow metafiles
	          perhaps it should be in the module?
	*/
	if (!allow_metafile (metafile)) {
		metafile_read_mark_done (metafile);
	} else {
		metafile->details->read_state = g_new0 (MetafileReadState, 1);
		metafile_read_restart (metafile);
	}
#endif
}

static void
metafile_write_done (GnomeVFSMetafile *metafile)
{
	if (metafile->details->write_state->write_again) {
		metafile_write_start (metafile);
		return;
	}

	xmlFree (metafile->details->write_state->buffer);
	g_free (metafile->details->write_state);
	metafile->details->write_state = NULL;
	bonobo_object_unref (BONOBO_OBJECT (metafile));
}

static void
metafile_write_failed (GnomeVFSMetafile *metafile)
{
	if (metafile->details->write_state->use_public_metafile) {
		metafile->details->write_state->use_public_metafile = FALSE;
		metafile_write_start (metafile);
		return;
	}

	metafile_write_done (metafile);
}

static void
metafile_write_failure_close_callback (GnomeVFSAsyncHandle *handle,
				       GnomeVFSResult result,
				       gpointer callback_data)
{
	GnomeVFSMetafile *metafile;

	metafile = GNOME_VFS_METAFILE (callback_data);

	metafile_write_failed (metafile);
}

static void
metafile_write_success_close_callback (GnomeVFSAsyncHandle *handle,
				       GnomeVFSResult result,
				       gpointer callback_data)
{
	GnomeVFSMetafile *metafile;

	metafile = GNOME_VFS_METAFILE (callback_data);
	g_assert (metafile->details->write_state->handle == NULL);

	if (result != GNOME_VFS_OK) {
		metafile_write_failed (metafile);
		return;
	}

	/* Now that we have finished writing, it is time to delete the
	 * private file if we wrote the public one.
	 */
	if (metafile->details->write_state->use_public_metafile) {
		/* A synchronous unlink is OK here because the private
		 * metafiles are local, so an unlink is very fast.
		 */
		gnome_vfs_unlink_from_uri (metafile->details->private_vfs_uri);
	}

	metafile_write_done (metafile);
}

static void
metafile_write_callback (GnomeVFSAsyncHandle *handle,
			 GnomeVFSResult result,
			 gconstpointer buffer,
			 GnomeVFSFileSize bytes_requested,
			 GnomeVFSFileSize bytes_read,
			 gpointer callback_data)
{
	GnomeVFSMetafile *metafile;

	metafile = GNOME_VFS_METAFILE (callback_data);
	g_assert (metafile->details->write_state->handle == handle);
	g_assert (metafile->details->write_state->buffer == buffer);
	g_assert (metafile->details->write_state->size == bytes_requested);

	g_assert (metafile->details->write_state->handle != NULL);
	gnome_vfs_async_close (metafile->details->write_state->handle,
			       result == GNOME_VFS_OK
			       ? metafile_write_success_close_callback
			       : metafile_write_failure_close_callback,
			       metafile);
	metafile->details->write_state->handle = NULL;
}

static void
metafile_write_create_callback (GnomeVFSAsyncHandle *handle,
				GnomeVFSResult result,
				gpointer callback_data)
{
	GnomeVFSMetafile *metafile;
	
	metafile = GNOME_VFS_METAFILE (callback_data);
	g_assert (metafile->details->write_state->handle == handle);
	
	if (result != GNOME_VFS_OK) {
		metafile_write_failed (metafile);
		return;
	}

	gnome_vfs_async_write (metafile->details->write_state->handle,
			       metafile->details->write_state->buffer,
			       metafile->details->write_state->size,
			       metafile_write_callback,
			       metafile);
}

static void
metafile_write_start (GnomeVFSMetafile *metafile)
{
	g_assert (GNOME_VFS_IS_METAFILE (metafile));

	metafile->details->write_state->write_again = FALSE;

	/* Open the file. */
	gnome_vfs_async_create_uri
		(&metafile->details->write_state->handle,
		 metafile->details->write_state->use_public_metafile
		 ? metafile->details->public_vfs_uri
		 : metafile->details->private_vfs_uri,
		 GNOME_VFS_OPEN_WRITE, FALSE, METAFILE_PERMISSIONS,
		 metafile_write_create_callback, metafile);
}

static void
metafile_write (GnomeVFSMetafile *metafile)
{
	int xml_doc_size;
	
	g_assert (GNOME_VFS_IS_METAFILE (metafile));

	bonobo_object_ref (BONOBO_OBJECT (metafile));

	/* If we are already writing, then just remember to do it again. */
	if (metafile->details->write_state != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (metafile));
		metafile->details->write_state->write_again = TRUE;
		return;
	}

	/* Don't write anything if there's nothing to write.
	 * At some point, we might want to change this to actually delete
	 * the metafile in this case.
	 */
	if (metafile->details->xml == NULL) {
		bonobo_object_unref (BONOBO_OBJECT (metafile));
		return;
	}

	/* Create the write state. */
	metafile->details->write_state = g_new0 (MetafileWriteState, 1);
	metafile->details->write_state->use_public_metafile
		= can_use_public_metafile (metafile);
	xmlDocDumpMemory (metafile->details->xml,
			  &metafile->details->write_state->buffer,
			  &xml_doc_size);
	metafile->details->write_state->size = xml_doc_size;
	metafile_write_start (metafile);
}

static gboolean
metafile_write_idle_callback (gpointer callback_data)
{
	GnomeVFSMetafile *metafile;

	metafile = GNOME_VFS_METAFILE (callback_data);

	metafile->details->write_idle_id = 0;
	metafile_write (metafile);

	bonobo_object_unref (BONOBO_OBJECT (metafile));

	return FALSE;
}

static void
directory_request_write_metafile (GnomeVFSMetafile *metafile)
{
	g_assert (GNOME_VFS_IS_METAFILE (metafile));

#ifdef METAFILE_CODE_READY
	if (!allow_metafile (metafile)) {
		return;
	}
#endif

	/* Set up an idle task that will write the metafile. */
	if (metafile->details->write_idle_id == 0) {
		bonobo_object_ref (BONOBO_OBJECT (metafile));
		metafile->details->write_idle_id =
			g_idle_add (metafile_write_idle_callback,
				    metafile);
	}
}
