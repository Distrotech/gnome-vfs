/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gconf-method.c - VFS Access to the GConf configuration database.

   Copyright (C) 1999 Free Software Foundation

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

   Author: Dave Camp <campd@oit.edu> */

/* FIXME: More error checking */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include "gnome-vfs-module.h"

#include "file-method.h"

static GnomeVFSResult   do_open         (GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSOpenMode mode);
static GnomeVFSResult   do_create       (GnomeVFSMethodHandle **method_handle,
                                         GnomeVFSURI *uri,
                                         GnomeVFSOpenMode mode,
                                         gboolean exclusive,
                                         guint perm);
static GnomeVFSResult   do_close        (GnomeVFSMethodHandle *method_handle);
static GnomeVFSResult   do_read         (GnomeVFSMethodHandle *method_handle,
                                         gpointer buffer,
                                         GnomeVFSFileSize num_bytes,
                                         GnomeVFSFileSize *bytes_read);
static GnomeVFSResult   do_write        (GnomeVFSMethodHandle *method_handle,
                                         gconstpointer buffer,
                                         GnomeVFSFileSize num_bytes,
                                         GnomeVFSFileSize *bytes_written);
static GnomeVFSResult  do_open_directory
                                        (GnomeVFSMethodHandle **method_handle,
					 GnomeVFSURI *uri,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys,
					 const GnomeVFSDirectoryFilter *filter);
static GnomeVFSResult  do_close_directory
                                        (GnomeVFSMethodHandle *method_handle);
static GnomeVFSResult  do_read_directory 
                                        (GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info);
static GnomeVFSResult  do_get_file_info
                                        (GnomeVFSURI *uri,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);
static GnomeVFSResult  do_get_file_info_from_handle
                                        (GnomeVFSMethodHandle *method_handle,
					 GnomeVFSFileInfo *file_info,
					 GnomeVFSFileInfoOptions options,
					 const GList *meta_keys);
static gboolean        do_is_local      (const GnomeVFSURI *uri);


static GnomeVFSMethod method = {
        do_open,
        do_create,
        do_close,
        do_read,
        do_write,
        NULL,
        NULL,
        NULL,
        do_open_directory,
        do_close_directory,
        do_read_directory,
        do_get_file_info,
	do_get_file_info_from_handle, /* FIXME */
        do_is_local,
        NULL,
	NULL,
        NULL,
	NULL,
	NULL /* truncate */
};

static GConfClient *client = NULL;

/* This is to make sure the path starts with `/', so that at least we
   get a predictable behavior when the leading `/' is not present.  */
#define MAKE_ABSOLUTE(dest, src)                        \
G_STMT_START{                                           \
        if ((src)[0] != '/') {                          \
                (dest) = alloca (strlen (src) + 2);     \
                (dest)[0] = '/';                        \
                strcpy ((dest), (src));                 \
        } else {                                        \
                (dest) = (src);                         \
        }                                               \
}G_STMT_END

typedef struct {
        GnomeVFSURI *uri;
        GnomeVFSFileInfoOptions options;
        const GList *meta_keys;
        const GnomeVFSDirectoryFilter *filter;

        GSList *subdirs;
        GSList *pairs;
} DirectoryHandle;

static DirectoryHandle *
directory_handle_new (GnomeVFSURI *uri,
                      GnomeVFSFileInfoOptions options,
                      const GList *meta_keys,
                      const GnomeVFSDirectoryFilter *filter,
                      GSList *subdirs,
                      GSList *pairs)
{
        DirectoryHandle *retval;
        
        retval = g_new (DirectoryHandle, 1);
        
        retval->uri = gnome_vfs_uri_ref (uri);
        retval->options = options;
        retval->meta_keys = meta_keys;
        retval->filter = filter;
        retval->pairs = pairs;
        retval->subdirs = subdirs;

        return retval;
}

static void
directory_handle_destroy (DirectoryHandle *handle) 
{
        /* FIXME: Free unused pairs */
        gnome_vfs_uri_unref (handle->uri);
        g_free (handle);
}

static GnomeVFSResult 
set_mime_type_value (GnomeVFSFileInfo *info,
                     const GConfValue *value,
                     GnomeVFSFileInfoOptions options)
{
        gchar *mime_type;
        
        switch (value->type) {
        case GCONF_VALUE_INVALID :
                mime_type = "X-GConf/invalid";
                break;
        case GCONF_VALUE_STRING :
                mime_type = "X-GConf/string";
                break;
        case GCONF_VALUE_INT :
                mime_type = "X-GConf/int";
                break;
        case GCONF_VALUE_FLOAT :
                mime_type = "X-GConf/float";
                break;
        case GCONF_VALUE_BOOL :
                mime_type = "X-GConf/bool";
                break;
        case GCONF_VALUE_SCHEMA :
                mime_type = "X-GConf/schema";
                break;
        case GCONF_VALUE_LIST :
                mime_type = "X-GConf/list";
                break;
        case GCONF_VALUE_PAIR :
                mime_type = "X-GConf/pair";
                break;
        default :
                mime_type = "unknown/unknown";
                break;
                
        }

        info->mime_type = g_strdup (mime_type);

	return GNOME_VFS_OK;
}

static GnomeVFSResult
set_mime_type_dir (GnomeVFSFileInfo *info,
                   const gchar *dirname,
                   GnomeVFSFileInfoOptions options)
{
        info->mime_type = g_strdup ("special/directory");

	return GNOME_VFS_OK;
}

static GnomeVFSResult
get_value_size (const GConfValue *value, GnomeVFSFileSize *size)
{
	
	GnomeVFSFileSize subvalue_size = 0;
	GnomeVFSResult result = GNOME_VFS_OK;
	GSList *values;
	GConfSchema *schema;
	
	*size = 0;
	
	switch (value->type) {
        case GCONF_VALUE_INVALID :
                *size = 0;
                break;
        case GCONF_VALUE_STRING :
                if (value->d.string_data != NULL) 
			*size = strlen (value->d.string_data);
		else 
			*size = 0;
                break;
        case GCONF_VALUE_INT :
                *size = sizeof (gint);
                break;
        case GCONF_VALUE_FLOAT :
                *size = sizeof (gdouble);
                break;
        case GCONF_VALUE_BOOL :
                *size = sizeof (gboolean);
                break;
        case GCONF_VALUE_SCHEMA :
                schema = value->d.schema_data;
		*size = 0;
		if (schema->short_desc != NULL)
			*size += strlen (schema->short_desc);
		if (schema->long_desc != NULL)
			*size += strlen (schema->long_desc);
		if (schema->owner != NULL)
			*size += strlen (schema->owner);
		if (schema->default_value != NULL) {
			result = get_value_size (schema->default_value, 
						 &subvalue_size);
			if (result != GNOME_VFS_OK)
				return result;
			
			*size += subvalue_size;
		}
		
		break;
	case GCONF_VALUE_LIST :
                *size = 0;
		/* FIXME: This could be optimized, and may be a problem with
                 * huge lists. */
		values = value->d.list_data.list;
		while (values != NULL) {
			result = get_value_size ((GConfValue*)values->data,
						 &subvalue_size);
			if (result != GNOME_VFS_OK) 
				return result;
			
			*size += subvalue_size;
			values = g_slist_next (values);
		}	
                break;
        case GCONF_VALUE_PAIR :
                result = get_value_size (value->d.pair_data.car, 
					 &subvalue_size);
		if (result != GNOME_VFS_OK) 
			return result;
		*size = subvalue_size;
                
		result = get_value_size (value->d.pair_data.car, 
					 &subvalue_size);
		if (result != GNOME_VFS_OK) 
			return result;
		
		*size += subvalue_size;		
                break;
	default :
		return GNOME_VFS_ERROR_INTERNAL;
		break;     
        }
	
	return GNOME_VFS_OK;
}
	
static GnomeVFSResult
set_stat_info_value (GnomeVFSFileInfo *info,
                     const GConfValue *value,
                     GnomeVFSFileInfoOptions options)
{
	GnomeVFSResult result;
        info->is_symlink = FALSE;
        info->type = GNOME_VFS_FILE_TYPE_UNKNOWN;
        info->permissions = 0;
        info->device = 0;
        info->inode = 0;
        info->link_count = 0;
        info->uid = 0;
        info->gid = 0;
        
	result = get_value_size (value, &info->size);
	if (result != GNOME_VFS_OK) 
		return result;

        info->block_count = 0;
        info->io_block_size = 0;
        info->atime = 0;
        info->ctime = 0;
        info->mtime = 0;
        info->is_local = TRUE;
        info->is_suid = FALSE;
        info->is_sgid = FALSE;
        info->has_sticky_bit = FALSE;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
set_stat_info_dir (GnomeVFSFileInfo *info,
                   GnomeVFSFileInfoOptions options)
{
        info->is_symlink = FALSE;
        info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
        info->permissions = 0;
        info->device = 0;
        info->inode = 0;
        info->link_count = 0;
        info->uid = 0;
        info->gid = 0;
        info->size = 0;
        info->block_count = 0;
        info->io_block_size = 0;
        info->atime = 0;
        info->ctime = 0;
        info->mtime = 0;
        info->is_local = TRUE;
        info->is_suid = FALSE;
        info->is_sgid = FALSE;
        info->has_sticky_bit = FALSE;

	return GNOME_VFS_OK;
}

static GnomeVFSResult
do_open (GnomeVFSMethodHandle **method_handle,
	 GnomeVFSURI *uri,
	 GnomeVFSOpenMode mode)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult   
do_create (GnomeVFSMethodHandle **method_handle,
	   GnomeVFSURI *uri,
	   GnomeVFSOpenMode mode,
	   gboolean exclusive,
	   guint perm)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult   
do_close (GnomeVFSMethodHandle *method_handle)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult
do_read (GnomeVFSMethodHandle *method_handle,
	 gpointer buffer,
	 GnomeVFSFileSize num_bytes,
	 GnomeVFSFileSize *bytes_read)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult   
do_write (GnomeVFSMethodHandle *method_handle,
	  gconstpointer buffer,
	  GnomeVFSFileSize num_bytes,
	  GnomeVFSFileSize *bytes_written)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;
}

static GnomeVFSResult 
do_open_directory (GnomeVFSMethodHandle **method_handle,
                   GnomeVFSURI *uri,
                   GnomeVFSFileInfoOptions options,
                   const GList *meta_keys,
                   const GnomeVFSDirectoryFilter *filter)
{
        GSList *pairs;
        GSList *subdirs;
        gchar *dirname;

        MAKE_ABSOLUTE (dirname, uri->text);
        
        subdirs = gconf_client_all_dirs (client, dirname);
        pairs = gconf_client_all_entries (client, dirname);
        
        *method_handle = 
		(GnomeVFSMethodHandle*)directory_handle_new (uri,
							     options,
							     meta_keys,
							     filter,
							     subdirs,
							     pairs);
        return GNOME_VFS_OK;
}

static GnomeVFSResult 
do_close_directory (GnomeVFSMethodHandle *method_handle)
{
        directory_handle_destroy ((DirectoryHandle *)method_handle);
        return GNOME_VFS_OK;
}

static GnomeVFSResult
file_info_value (GnomeVFSFileInfo *info,
                 GnomeVFSFileInfoOptions options,
                 GConfValue *value,
                 const char *key)
{
        GnomeVFSResult result;

	info->name = g_strdup (key);
        result = set_stat_info_value (info, value, options);
	
	if (result != GNOME_VFS_OK) return result;
        
        if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
                result = set_mime_type_value (info, value, options);

        return result;
}

static GnomeVFSResult
file_info_dir (GnomeVFSFileInfo *info,
               GnomeVFSFileInfoOptions options,
               gchar *dirname)
{
	GnomeVFSResult result;
	
        info->name = g_strdup (dirname);
        
	result = set_stat_info_dir (info, options);

	if (result != GNOME_VFS_OK) return result;

        if (options & GNOME_VFS_FILE_INFO_GETMIMETYPE)
                result = set_mime_type_dir (info, dirname, options);
        
        return result;
}

        
static GnomeVFSResult 
read_directory (DirectoryHandle *handle,
		GnomeVFSFileInfo *file_info,
		gboolean *skip)
{
        GnomeVFSResult result;
        GSList *tmp;
	const GnomeVFSDirectoryFilter *filter;
	GnomeVFSDirectoryFilterNeeds filter_needs;
	gboolean filter_called;
	
	filter_called = FALSE;
        filter = handle->filter;

	if (filter != NULL) {
		filter_needs = gnome_vfs_directory_filter_get_needs (filter);
	} else {
		filter_needs = GNOME_VFS_DIRECTORY_FILTER_NEEDS_NOTHING;
	}

	/* Get the next key info */
        if (handle->subdirs != NULL) {
                gchar *dirname = handle->subdirs->data;
                result = file_info_dir (file_info, handle->options, dirname);
                g_free (handle->subdirs->data);
                tmp = g_slist_next (handle->subdirs);
                g_slist_free_1 (handle->subdirs);
                handle->subdirs = tmp;
        } else if (handle->pairs != NULL) {
                GConfEntry *pair = handle->pairs->data;
                result = file_info_value (file_info, handle->options,
                                          pair->value, pair->key);
                g_free (handle->pairs->data);
                tmp = g_slist_next (handle->subdirs);
                g_slist_free_1 (handle->pairs);
                handle->pairs = tmp;
        } else {
		result = GNOME_VFS_ERROR_EOF;
	}
	
	if (result != GNOME_VFS_OK) {
		return result;
	}
	
	/* Filter the file */
	*skip = FALSE;;
	if (filter != NULL
	    && !filter_called
	    && !(filter_needs 
		 & (GNOME_VFS_DIRECTORY_FILTER_NEEDS_TYPE
		    | GNOME_VFS_DIRECTORY_FILTER_NEEDS_STAT
		    | GNOME_VFS_DIRECTORY_FILTER_NEEDS_MIMETYPE
		    | GNOME_VFS_DIRECTORY_FILTER_NEEDS_METADATA))) {
		if (!gnome_vfs_directory_filter_apply (filter, file_info)) {
			*skip = TRUE;
			return GNOME_VFS_OK;
		}
		filter_called = TRUE;
	}

	return result;
}

static GnomeVFSResult 
do_read_directory (GnomeVFSMethodHandle *method_handle,
                   GnomeVFSFileInfo *file_info)
{
	GnomeVFSResult result;
	gboolean skip;
	
	skip = FALSE;
	
	do {
		result = read_directory ((DirectoryHandle*)method_handle,
					 file_info, 
					 &skip);
		if (result != GNOME_VFS_OK) 
			break;
		if (skip)
			gnome_vfs_file_info_clear (file_info);
		
	} while (skip);
	
	return result;	    
}

GnomeVFSResult
do_get_file_info (GnomeVFSURI *uri,
                  GnomeVFSFileInfo *file_info,
                  GnomeVFSFileInfoOptions options,
                  const GList *meta_keys)
{
        GConfValue *value;
        gchar *key;
        
        MAKE_ABSOLUTE (key, uri->text);
        
        value = gconf_client_get (client, key);
        return file_info_value (file_info, options, value, key);
}

static GnomeVFSResult  
do_get_file_info_from_handle (GnomeVFSMethodHandle *method_handle,
			      GnomeVFSFileInfo *file_info,
			      GnomeVFSFileInfoOptions options,
			      const GList *meta_keys)
{
	return GNOME_VFS_ERROR_WRONGFORMAT;	
}


gboolean 
do_is_local (const GnomeVFSURI *uri)
{
        return TRUE;
}

GnomeVFSMethod *
vfs_module_init (void)
{
        char *argv[] = {"dummy"};
        int argc = 1;
	
	if (!gconf_is_initialized ()) {
		/* auto-initializes OAF if necessary */
		gconf_init (argc, argv);
	}

	/* These just return and do nothing if GTK
	   is already initialized. */
	gtk_type_init();
	gtk_signal_init();
	
	client = gconf_client_new();

	gtk_object_ref(GTK_OBJECT(client));
	gtk_object_sink(GTK_OBJECT(client));
	
        return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
	gtk_object_destroy(GTK_OBJECT(client));
	gtk_object_unref(GTK_OBJECT(client));
	client = NULL;
}
