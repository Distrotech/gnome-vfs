/* gnome-vfs-resolve.c - Resolver API

   Copyright (C) 2004 Christian Kellner <gicmo@gnome-de.org>

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

*/

#include <config.h>

#include <errno.h>
#include <glib/gmem.h>
#include <glib/gmessages.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
/* Keep <sys/types.h> above the network includes for FreeBSD. */
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <libgnomevfs/gnome-vfs-resolve.h>

#define INIT_BUFSIZE 8192 /* Unix Network Programming Chapter 11 Page 304 :) */

#ifdef USE_GETHOSTBYNAME
G_LOCK_DEFINE (dns_lock);
#ifndef h_errno
extern int h_errno;
#endif
#endif

struct GnomeVFSResolveHandle_ {
#ifdef HAVE_GETADDRINFO
	   struct addrinfo *result;
	   struct addrinfo *current;
#else
	   GList *result;
	   GList *current;
#endif	
};

#ifndef HAVE_GETADDRINFO

static GnomeVFSResult
resolvehandle_from_hostent (struct hostent *he, GnomeVFSResolveHandle **handle)
{
	   GnomeVFSAddress *addr;
	   GList *result;
	   char **iter;
	   void *aptr;
	   size_t addrlen;
	   struct sockaddr *sa;
	   struct sockaddr_in sin;
#ifdef ENABLE_IPV6	   
	   struct sockaddr_in6 sin6;
#endif

	   switch (he->h_addrtype) {
			 
	   case AF_INET:
			 bzero (&sin, sizeof (sin));
			 sin.sin_family = AF_INET;
			 aptr = &(sin.sin_addr);
			 sa = (struct sockaddr *) &sin;
			 addrlen = sizeof (struct sockaddr_in);
			 break;
			 
#ifdef ENABLE_IPV6
	   case AF_INET6:
			 bzero (&sin6, sizeof (sin6));
			 sin6.sin6_family = AF_INET6;
			 aptr = &(sin6.sin6_addr);
			 sa = (struct sockaddr *) &sin6;
			 addrlen = sizeof (struct sockaddr_in6);
			 break;
#endif
	   default:
			 return GNOME_VFS_ERROR_INTERNAL;
	   }

	   result = NULL;
	   *handle = NULL;
	   
	   for (iter = he->h_addr_list; *iter != NULL; iter++) {
			 g_memmove (aptr, *iter, he->h_length);
			 addr = gnome_vfs_address_new_from_sockaddr (sa, addrlen);

			 if (addr != NULL)
				    result = g_list_append (result, addr);
	   }

	   if (result == NULL)
			 return GNOME_VFS_ERROR_INTERNAL;

	   *handle = g_new0 (GnomeVFSResolveHandle, 1);
	   (*handle)->result = result;
	   (*handle)->current = result;
	   
	   return GNOME_VFS_OK;
}

#endif

#ifdef HAVE_GETADDRINFO
static GnomeVFSResult
_gnome_vfs_result_from_gai_error (int error)
{

	switch (error) {

	case EAI_NONAME: return GNOME_VFS_ERROR_HOST_NOT_FOUND;
	case EAI_ADDRFAMILY:		
	case EAI_NODATA: return GNOME_VFS_ERROR_HOST_HAS_NO_ADDRESS;
	case EAI_SYSTEM: return gnome_vfs_result_from_errno ();
	case EAI_FAIL:
	case EAI_AGAIN: return GNOME_VFS_ERROR_NAMESERVER;
		
	case EAI_MEMORY: return GNOME_VFS_ERROR_NO_MEMORY;

     /* We should not get these errors there just here to have
	   complete list of getaddrinfo errors*/
	case EAI_FAMILY:
	case EAI_SOCKTYPE:
	case EAI_SERVICE:
	case EAI_BADFLAGS:
	default:
		return GNOME_VFS_ERROR_INTERNAL;
	}
}

#endif

/**
 * gnome_vfs_resolve:
 * @hostname: The hostname you want to resolve.
 * @handle: A pointer to a pointer to a #GnomeVFSResolveHandle.
 *
 * Tries to resolve @hostname. If the operation was successful you can
 * get the resolved addresses in form of #GnomeVFSAddress by calling
 * gnome_vfs_resolve_next_address.
 * 
 * 
 * Return value: A #GnomeVFSResult indicating the success of the operation.
 *
 * Since: 2.8
 **/
GnomeVFSResult
gnome_vfs_resolve (const char              *hostname,
			    GnomeVFSResolveHandle  **handle)
{
#ifdef HAVE_GETADDRINFO
	   struct addrinfo hints, *result;
	   int res;

	   bzero (&hints, sizeof (hints));
	   hints.ai_socktype = SOCK_STREAM;

#ifdef ENABLE_IPV6
# ifdef HAVE_AI_ADDRCONFIG /* RFC3493 */
	   hints.ai_flags = AI_ADDRCONFIG;
	   hints.ai_family = AF_UNSPEC;
# else	
	   hints.ai_family = AF_UNSPEC;
# endif	
#else
	   hints.ai_family = AF_INET;
#endif /* ENABLE_IPV6 */
	   res = getaddrinfo (hostname, NULL, &hints, &result);
	   
	   if (res != 0) {
			 return _gnome_vfs_result_from_gai_error (res);
	   }

	   *handle = g_new0 (GnomeVFSResolveHandle, 1);
	   
	   (*handle)->result  = result;
	   (*handle)->current = result;
	   
	   return GNOME_VFS_OK;
#else /* HAVE_GETADDRINFO */
	   struct hostent resbuf, *result = &resbuf;
	   GnomeVFSResult ret;
	   int res;
#ifdef HAVE_GETHOSTBYNAME_R_GLIBC
	   size_t buflen;
	   char *buf;
	   int h_errnop;
	   
	   buf = NULL;
	   buflen = INIT_BUFSIZE;
	   h_errnop = 0;
	   
	   do {
			 buf = g_renew (char, buf, buflen);
			 
			 res = gethostbyname_r (hostname,
							    &resbuf,
							    buf,
							    buflen,
							    &result,
							    &h_errnop);
			 buflen *= 2;

	   } while (res == ERANGE);

	   if (res != 0 || result == NULL || result->h_addr_list[0] == NULL) {
	    			 return gnome_vfs_result_from_h_errno_val (h_errnop);	 
	   }

	   ret = resolvehandle_from_hostent (result, handle);
	   g_free (buf);
#elif HAVE_GETHOSTBYNAME_R_SOLARIS
	   size_t buflen;
	   char *buf;
	   int h_errnop;
	   
	   buf = NULL;
	   buflen = INIT_BUFSIZE;
	   h_errnop = 0;
	   
	   do {
			 buf = g_renew (char, buf, buflen);
			 
			 result = gethostbyname_r (hostname,
								  &resbuf,
								  buf,
								  buflen,
								  &h_errnop);
			 buflen *= 2;

	   } while (h_errnop == ERANGE);

	   if (result == NULL) {
	    			 return gnome_vfs_result_from_h_errno_val (h_errnop);	 
	   }

	   ret = resolvehandle_from_hostent (result, handle);	
	   g_free (buf);
#elif HAVE_GETHOSTBYNAME_R_HPUX
	   struct hostent_data buf;

	   res = gethostbyname_r (hostname, result, &buf);

	   if (res != 0) {
			 return gnome_vfs_result_from_h_errno_val (res);
	   }
	   ret = resolvehandle_from_hostent (result, handle);
#else
	   res = 0;/* only set to avoid unused variable error */
	   
	   G_LOCK (dns_lock);
	   result = gethostbyname (hostname);

	   if (result == NULL) {
			 ret = gnome_vfs_result_from_h_errno ();
	   } else {
			 ret = resolvehandle_from_hostent (result, handle);
	   }

	   G_UNLOCK (dns_lock);
#endif /* GETHOSTBYNAME */
	   return ret;
#endif /* HAVE_GETADDRINFO */
}


/**
 * gnome_vfs_resovle_reset_to_beginning:
 * @handle: A #GnomeVFSResolveHandle.
 *
 * Reset @handle so that a following call to gnome_vfs_next_address
 * will return the first resolved address.
 *
 *
 * Since: 2.8
 **/
void
gnome_vfs_resolve_reset_to_beginning (GnomeVFSResolveHandle *handle)
{
	   g_return_if_fail (handle != NULL);

	   handle->current = handle->result;
}

/**
 * gnome_vfs_next_address:
 * @handle: A #GnomeVFSResolveHandle.
 * @address: A pointer to a pointer to a #GnomeVFSAddress.
 * 
 * Stores the next #GnomeVFSAddress available in @handle of the
 * former lookup in @address.
 * 
 * Return value: %TRUE if the next address was stored in @address or
 * %FALSE if no other address is available.
 *
 * Since: 2.8
 **/
gboolean
gnome_vfs_next_address (GnomeVFSResolveHandle  *handle,
				    GnomeVFSAddress       **address)
{
	   g_return_val_if_fail (address != NULL, FALSE);
	   g_return_val_if_fail (handle != NULL, FALSE);

	   *address = NULL;
	   
#ifdef HAVE_GETADDRINFO   
	   while (*address == NULL && handle->current != NULL) {
			 *address = gnome_vfs_address_new_from_sockaddr (handle->current->ai_addr,
												    handle->current->ai_addrlen);
			 handle->current = handle->current->ai_next;

	   }
#else
	   if (handle->current) {
			 *address = gnome_vfs_address_dup (handle->current->data);
			  handle->current = handle->current->next;
	   }
#endif	
	   return *address != NULL ? TRUE : FALSE;	
}


/**
 * gnome_vfs_resolve_free:
 * @handle: A #GnomeVFSResolveHandle.
 *
 * Use this function to free a #GnomeVFSResolveHandle returned by
 * gnome_vfs_resolve.
 * 
 * Since: 2.8
 **/
void
gnome_vfs_resolve_free (GnomeVFSResolveHandle  *handle)
{
#ifdef HAVE_GETADDRINFO
	   if (handle->result != NULL)
			 freeaddrinfo (handle->result);

#else
	   for (handle->current = handle->result; handle->current != NULL;
		   handle->current = handle->current->next) {
			 gnome_vfs_address_free (handle->current->data);
	   }
	   g_list_free (handle->result);
	   
#endif
	   g_free (handle);
}
