/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*

   Copyright (C) 2001 Eazel, Inc

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

   Author: Michael Fleming <mfleming@eazel.com>
*/

#ifndef GNOME_VFS_CALLBACK_H
#define GNOME_VFS_CALLBACK_H

/*
 * defined callback structures
 */

/*
 * hook name: "simple-authn"
 * In arg: GnomeVFSCallbackSimpleAuthIn *
 * Out arg: GnomeVFSCallbackSimpleAuthOut *
 * 
 * Called when access to a URI requires a username/password
 */

#define GNOME_VFS_HOOKNAME_BASIC_AUTH "simple-authn"

/*
 * hook name: "http:proxy-authn"
 * In arg: GnomeVFSCallbackSimpleAuthIn *
 * Out arg: GnomeVFSCallbackSimpleAuthOut *
 * 
 * Called when access to an HTTP proxy requires a username/password
 */

#define GNOME_VFS_HOOKNAME_HTTP_PROXY_AUTH "http:proxy-authn"

typedef struct {
	char *uri;		/* Full URI of operation */
	char *realm;		/* for HTTP auth, NULL for others */
	gboolean previous_authn_failed;
				/* TRUE if there were credentials specified
				 * for this request, but they resulted in
				 * an authorization error. 
				 * ("you gave me the wrong pw!")
				 * 
				 * FALSE if there were no credentials specified
				 * but they are required to continue
				 * 
				 */
	enum {
		AuthTypeBasic,	/* Password will be transmitted unencrypted */
		AuthTypeDigest	/* Digest is transferred, not plaintext credentials */		
	} auth_type;
} GnomeVFSCallbackSimpleAuthIn;

typedef struct {
	char *username;		/* will be freed by g_free,
				 * NULL indicates no auth should be provided;
				 * if the request requires authn, the operation
				 * will fail with a GNOME_VFS_ERROR_ACCESS_DENIED
				 * code
				 */
	char *password;		/* will be freed by g_free */
} GnomeVFSCallbackSimpleAuthOut;

/*
 * hook name: "status-message"
 * In arg: GnomeVFSCallbackStatusMessageIn *
 * Out arg: GnomeVFSCallbackStatusMessageOut *
 * 
 * Called when a GnomeVFS API or module has a status message to return to
 * the user.
 */

#define GNOME_VFS_HOOKNAME_STATUS_MESSAGE "status-message"

typedef struct {
	char *uri;		/* Full URI of operation */
	char *message;		/* A message indicating the current state or
				 * NULL if there is no message */
	gint percentage;	/* Percentage indicating completeness 0-100 or
				 * -1 if there is no progress percentage to
				 * report */
} GnomeVFSCallbackStatusMessageIn;

typedef struct {
} GnomeVFSCallbackStatusMessageOut;

#endif /* GNOME_VFS_CALLBACK_H */
