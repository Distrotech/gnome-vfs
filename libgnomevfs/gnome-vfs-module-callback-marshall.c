/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*

   Copyright (C) 2003 Red Hat, Inc

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

   Author: Alexander Larsson <alexl@redhat.com>
*/


#include <config.h>
#include <string.h>
#include <libbonobo.h>
#include "gnome-vfs-module-callback.h"

#include "gnome-vfs-module-callback-module-api.h"
#include "gnome-vfs-module-callback-private.h"
#include "gnome-vfs-backend.h"
#include "gnome-vfs-private.h"
#include "gnome-vfs-client-call.h"
#include "gnome-vfs-standard-callbacks.h"
#include <GNOME_VFS_Daemon.h>


static CORBA_char *
corba_string_or_null_dup (char *str)
{
	if (str != NULL) {
		return CORBA_string_dup (str);
	} else {
		return CORBA_string_dup ("");
	}
}

/* empty string interpreted as NULL */
static char *
decode_corba_string_or_null (CORBA_char *str, gboolean empty_is_null)
{
	if (empty_is_null && *str == 0) {
		return NULL;
	} else {
		return g_strdup (str);
	}
}



static CORBA_any *
auth_marshal_in (gconstpointer in, gsize in_size)
{
	CORBA_any *retval;
	GNOME_VFS_ModuleCallbackAuthenticationIn *ret_in;
	const GnomeVFSModuleCallbackAuthenticationIn *auth_in;
	
	if (in_size != sizeof (GnomeVFSModuleCallbackAuthenticationIn)) {
		return NULL;
	}
	auth_in = in;

	retval = CORBA_any_alloc();
	retval->_type = TC_GNOME_VFS_ModuleCallbackAuthenticationIn;
	retval->_value = GNOME_VFS_ModuleCallbackAuthenticationIn__alloc();
	ret_in = retval->_value;

	ret_in->uri = corba_string_or_null_dup (auth_in->uri);
	ret_in->realm = corba_string_or_null_dup (auth_in->realm);
	ret_in->previous_attempt_failed = auth_in->previous_attempt_failed;
	ret_in->auth_type = auth_in->auth_type;

	return retval;
}

static gboolean
auth_demarshal_in (const CORBA_any *any_in,
		   gpointer *in, gsize *in_size,
		   gpointer *out, gsize *out_size)
{
	GNOME_VFS_ModuleCallbackAuthenticationIn *corba_in;
	GnomeVFSModuleCallbackAuthenticationIn *auth_in;
	GnomeVFSModuleCallbackAuthenticationOut *auth_out;
	
	if (!CORBA_TypeCode_equal (any_in->_type, TC_GNOME_VFS_ModuleCallbackAuthenticationIn, NULL)) {
		return FALSE;
	}
	
	auth_in = *in = g_new0 (GnomeVFSModuleCallbackAuthenticationIn, 1);
	*in_size = sizeof (GnomeVFSModuleCallbackAuthenticationIn);
	auth_out = *out = g_new0 (GnomeVFSModuleCallbackAuthenticationOut, 1);
	*out_size = sizeof (GnomeVFSModuleCallbackAuthenticationOut);

	corba_in = (GNOME_VFS_ModuleCallbackAuthenticationIn *)any_in->_value;

	auth_in->uri = decode_corba_string_or_null (corba_in->uri, TRUE);
	auth_in->realm = decode_corba_string_or_null (corba_in->realm, TRUE);
	auth_in->previous_attempt_failed = corba_in->previous_attempt_failed;
	auth_in->auth_type = corba_in->auth_type;
	
	return TRUE;
}

static CORBA_any *
auth_marshal_out (gconstpointer out, gsize out_size)
{
	CORBA_any *retval;
	GNOME_VFS_ModuleCallbackAuthenticationOut *ret_out;
	const GnomeVFSModuleCallbackAuthenticationOut *auth_out;

	if (out_size != sizeof (GnomeVFSModuleCallbackAuthenticationOut)) {
		return NULL;
	}
	auth_out = out;

	retval = CORBA_any_alloc();
	retval->_type = TC_GNOME_VFS_ModuleCallbackAuthenticationOut;
	retval->_value = GNOME_VFS_ModuleCallbackAuthenticationOut__alloc();
	ret_out = retval->_value;

	ret_out->username = corba_string_or_null_dup (auth_out->username);
	ret_out->no_username = auth_out->username == NULL;
	ret_out->password = corba_string_or_null_dup (auth_out->password);

	return retval;
}

static gboolean
auth_demarshal_out (CORBA_any *any_out, gpointer out, gsize out_size)
{
	GNOME_VFS_ModuleCallbackAuthenticationOut *corba_out;
	GnomeVFSModuleCallbackAuthenticationOut *auth_out;

	if (!CORBA_TypeCode_equal (any_out->_type, TC_GNOME_VFS_ModuleCallbackAuthenticationOut, NULL) ||
	    out_size != sizeof (GnomeVFSModuleCallbackAuthenticationOut)) {
		return FALSE;
	}
	auth_out = out;

	corba_out = (GNOME_VFS_ModuleCallbackAuthenticationOut *)any_out->_value;

	auth_out->username = decode_corba_string_or_null (corba_out->username,
							  corba_out->no_username);
	auth_out->password = decode_corba_string_or_null (corba_out->password, TRUE);
	
	return TRUE;
}

static void
auth_free_in (gpointer in)
{
	GnomeVFSModuleCallbackAuthenticationIn *auth_in;

	auth_in = in;

	g_free (auth_in->uri);
	g_free (auth_in->realm);

	g_free (auth_in);
}

static void
auth_free_out (gpointer out)
{
	GnomeVFSModuleCallbackAuthenticationOut *auth_out;

	auth_out = out;
	g_free (auth_out->username);
	g_free (auth_out->password);
	g_free (auth_out);
}

struct ModuleCallbackMarshaller {
	char *name;
	CORBA_any *(*marshal_in)(gconstpointer in, gsize in_size);
	gboolean (*demarshal_in)(const CORBA_any *any_in, gpointer *in, gsize *in_size, gpointer *out, gsize *out_size);
	CORBA_any *(*marshal_out)(gconstpointer out, gsize out_size);
	gboolean (*demarshal_out)(CORBA_any *any_out, gpointer out, gsize out_size);
	void (*free_in)(gpointer in);
	void (*free_out)(gpointer out);
};

static struct ModuleCallbackMarshaller module_callback_marshallers[] = {
	{ GNOME_VFS_MODULE_CALLBACK_AUTHENTICATION,
	  auth_marshal_in,
	  auth_demarshal_in,
	  auth_marshal_out,
	  auth_demarshal_out,
	  auth_free_in,
	  auth_free_out
	},
	{ GNOME_VFS_MODULE_CALLBACK_HTTP_PROXY_AUTHENTICATION,
	  auth_marshal_in,
	  auth_demarshal_in,
	  auth_marshal_out,
	  auth_demarshal_out,
	  auth_free_in,
	  auth_free_out
	},
};


static struct ModuleCallbackMarshaller *
lookup_marshaller (const char *callback_name)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS (module_callback_marshallers); i++) {
		if (strcmp (module_callback_marshallers[i].name, callback_name) == 0) {
			return &module_callback_marshallers[i];
		}
	}
	return NULL;
}



gboolean
_gnome_vfs_module_callback_marshal_invoke (const char    *callback_name,
					   gconstpointer  in,
					   gsize          in_size,
					   gpointer       out,
					   gsize          out_size)
{
	CORBA_Environment ev;
	CORBA_any *any_in;
	CORBA_any *any_out;
	gboolean res;
	struct ModuleCallbackMarshaller *marshaller;

	g_print ("_gnome_vfs_module_callback_marshal_invoke(%s) - thread %p\n", callback_name, g_thread_self());
	
	marshaller = lookup_marshaller (callback_name);
	if (marshaller == NULL) {
		return FALSE;
	}

	any_in = (marshaller->marshal_in)(in, in_size);
	if (any_in == NULL) {
		return FALSE;
	}

	CORBA_exception_init (&ev);
	any_out = NULL;
	res = GNOME_VFS_ClientCall_ModuleCallbackInvoke (_gnome_vfs_daemon_get_current_daemon_client_call (),
							 callback_name,
							 any_in,
							 &any_out,
							 &ev);
	CORBA_free (any_in);
	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);
		return FALSE;
	}

	if (!res) {
		CORBA_free (any_out);
		return FALSE;
	}
	
	if (!(marshaller->demarshal_out)(any_out, out, out_size)) {
		CORBA_free (any_out);
		return FALSE;
	}
	CORBA_free (any_out);
	return TRUE;
}



gboolean
_gnome_vfs_module_callback_demarshal_invoke (const char    *callback_name,
					     const CORBA_any * module_in,
					     CORBA_any ** module_out)
{
	gboolean res;
	gpointer in, out;
	gsize in_size, out_size;
	struct ModuleCallbackMarshaller *marshaller;
	CORBA_any *empty_any;

	g_print ("_gnome_vfs_module_callback_demarshal_invoke(%s) - thread %p\n", callback_name, g_thread_self());

	marshaller = lookup_marshaller (callback_name);
	if (marshaller == NULL) {
		return FALSE;
	}
	
	if (!(marshaller->demarshal_in)(module_in,
					&in, &in_size,
					&out, &out_size)) {
		return FALSE;
	}

	res = gnome_vfs_module_callback_invoke (callback_name,
						in, in_size,
						out, out_size);
	if (!res) {
		(marshaller->free_in) (in);
		g_free (out); /* only shallow free necessary */
		empty_any = CORBA_any_alloc();
		empty_any->_type = TC_null;
		empty_any->_value = NULL;
		*module_out = empty_any;

		return FALSE;
	}
	(marshaller->free_in) (in);

	*module_out = (marshaller->marshal_out)(out, out_size);
	(marshaller->free_out) (out);

	if (*module_out == NULL) {
		empty_any = CORBA_any_alloc();
		empty_any->_type = TC_null;
		empty_any->_value = NULL;
		*module_out = empty_any;
		return FALSE;
	}
	
	return TRUE;
}
