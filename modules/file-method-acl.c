/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-acl.h - ACL Handling for the GNOME Virtual File System.
   Virtual File System.

   Copyright (C) 2005 Christian Kellner
   Copyright (C) 2005 Sun Microsystems

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

   Authors: Christian Kellner <gicmo@gnome.org>
            Alvaro Lopez Ortega <alvaro@sun.com>
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/resource.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#ifdef HAVE_POSIX_ACL
#include <acl/libacl.h>
#endif

#include "file-method-acl.h"

/* ************************************************************************** */
/* POSIX ACL */
#ifdef HAVE_POSIX_ACL

static char *
uid_to_string (uid_t uid) 
{
	char *uid_string = NULL;
#ifdef HAVE_PWD_H
	struct passwd *pw = NULL;
	gpointer buffer = NULL;
	gint error;

#if defined (HAVE_POSIX_GETPWUID_R) || defined (HAVE_NONPOSIX_GETPWUID_R)	
	struct passwd pwd;
	glong  bufsize;
	
#ifdef _SC_GETPW_R_SIZE_MAX
	bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
#else
	bufsize = 64;
#endif /* _SC_GETPW_R_SIZE_MAX */

	
	do {
		g_free (buffer);

		/* why bufsize + 6 see #156446 */
		buffer = g_malloc (bufsize + 6);
		errno = 0;
		
#ifdef HAVE_POSIX_GETPWUID_R
	    error = getpwuid_r (uid, &pwd, buffer, bufsize, &pw);
        error = error < 0 ? errno : error;
#else /* HAVE_NONPOSIX_GETPWUID_R */
       /* HPUX 11 falls into the HAVE_POSIX_GETPWUID_R case */
#if defined(_AIX) || defined(__hpux)
	    error = getpwuid_r (uid, &pwd, buffer, bufsize);
	    pw = error == 0 ? &pwd : NULL;
#else /* !_AIX */
        pw = getpwuid_r (uid, &pwd, buffer, bufsize);
        error = pw ? 0 : errno;
#endif /* !_AIX */            
#endif /* HAVE_NONPOSIX_GETPWUID_R */
		
	    if (pw == NULL) {
		    if (error == 0 || error == ENOENT) {
				break;
		    }
	    }

	    if (bufsize > 32 * 1024) {
			break;
	    }

	    bufsize *= 2;
	    
	} while (pw == NULL);
#endif /* HAVE_POSIX_GETPWUID_R || HAVE_NONPOSIX_GETPWUID_R */

	if (pw == NULL) {
		setpwent ();
		pw = getpwuid (getuid ());
		endpwent ();
	}

	if (pw != NULL) {
		uid_string = g_strdup (pw->pw_name);
	}

#endif /* HAVE_PWD_H */
	
	if (uid_string == NULL) {
		uid_string = g_strdup_printf ("%d", uid);
	}
	
	return uid_string;	
}

static char *
gid_to_string (gid_t gid) 
{
	char *gid_string = NULL;
#ifdef HAVE_GETGRGID_R
	struct group *gr = NULL;
	gpointer buffer = NULL;
	gint error;
	struct group grp;
	glong  bufsize;
	
#ifdef _SC_GETGR_R_SIZE_MAX
	bufsize = sysconf (_SC_GETGR_R_SIZE_MAX);
#else
	bufsize = 64;
#endif /* _SC_GETPW_R_SIZE_MAX */

	do {
		g_free (buffer);

		/* why bufsize + 6 see #156446 */
		buffer = g_malloc (bufsize + 6);
	    error = getgrgid_r (gid, &grp, buffer, bufsize, &gr);
	    
        error = error < 0 ? errno : error;
	
		if (gr == NULL) {
		    if (error == 0 || error == ENOENT) {
				break;
		    }
	    }

	    if (bufsize > 32 * 1024) {
			break;
	    }

	    bufsize *= 2;
	    
	} while (gr == NULL);
	
	if (gr != NULL) {
		gid_string = g_strdup (gr->gr_name);
	}
	
#endif
	if (gid_string == NULL) {
		gid_string = g_strdup_printf ("%d", gid);
	}
	
	return gid_string;
}

#define POSIX_N_TAGS 3
static int
permset_to_perms (acl_permset_t set, GnomeVFSACLPerm *tags)
{
	int i;
	
	memset (tags, 0, sizeof (GnomeVFSACLPerm) * POSIX_N_TAGS);
	i = 0;

	if (acl_get_perm (set, ACL_READ) == 1) {
		tags[0] = GNOME_VFS_ACL_READ;
		i++;
	}
	
	if (acl_get_perm (set, ACL_WRITE) == 1) {
		tags[i] = GNOME_VFS_ACL_WRITE;
		i++;
	}
	
	if (acl_get_perm (set, ACL_EXECUTE)) {
		tags[i] = GNOME_VFS_ACL_EXECUTE;	
	}

	return i;
}

static int
posix_acl_read (GnomeVFSACL *acl,
                acl_t        p_acl,
                gboolean     def)
{
	acl_entry_t    entry;
	int            res;
	int            e_id;
	int            n;

	if (p_acl == NULL) {
		return 0;	
	}

    n    = 0;
	e_id = ACL_FIRST_ENTRY;	
	while ((res = acl_get_entry (p_acl, e_id, &entry)) == 1) {
		GnomeVFSACLPerm   pset[POSIX_N_TAGS];
		GnomeVFSACLKind   kind;
		GnomeVFSACE      *ace;
		acl_permset_t     e_ps;
		acl_tag_t         e_type;
		void             *e_qf;
		char             *id;	

		e_id   = ACL_NEXT_ENTRY;	
		e_type = ACL_UNDEFINED_ID;
		e_qf   = NULL;
		
		res = acl_get_tag_type (entry, &e_type);

		if (res == -1 || e_type == ACL_UNDEFINED_ID || e_type == ACL_MASK) {
			continue;
		}
		
		if (def == FALSE && (e_type != ACL_USER && e_type != ACL_GROUP)) {
			/* skip the standard unix permissions */
			continue;	
		}

		res = acl_get_permset (entry, &e_ps);

		if (res == -1) {
			continue;
		}
		
		e_qf = acl_get_qualifier (entry);
		
		id = NULL;
		kind = GNOME_VFS_ACL_KIND_NULL;
		switch (e_type) {

		case ACL_USER:
			id = uid_to_string (*(uid_t *) e_qf);	
			/* FALLTHROUGH */
			
		case ACL_USER_OBJ:
			kind = GNOME_VFS_ACL_USER;
			break;

		case ACL_GROUP:
			id = gid_to_string (*(gid_t *) e_qf);
			/* FALLTHROUGH */

		case ACL_GROUP_OBJ:
			kind = GNOME_VFS_ACL_GROUP;
			break;
			
		case ACL_MASK:
		case ACL_OTHER:
			kind = GNOME_VFS_ACL_OTHER;
			break;
		}
		
		permset_to_perms (e_ps, pset);
		ace = gnome_vfs_ace_new (kind, id, pset);

		g_free (id);

		gnome_vfs_acl_set (acl, ace);

		g_object_unref (ace);

		if (e_qf) {
			acl_free (e_qf);
		}
		
		n++;
	}
	
	return n;
}

#endif /* HAVE_POSIX_ACL */

GnomeVFSResult file_get_acl (const char       *path,
                             GnomeVFSFileInfo *info,
                             struct stat      *statbuf, /* needed? */
                             GnomeVFSContext  *context)
{
#ifdef HAVE_POSIX_ACL
	acl_t p_acl;
	int   n;
	
	if (acl_extended_file (path) != 1) {
		return GNOME_VFS_OK;
	}
	
	if (info->acl != NULL) {
		/* FIXME create a _clear () for this */
		g_object_unref (info->acl);	
	}
	
	info->acl = gnome_vfs_acl_new ();
	
	p_acl = acl_get_file (path, ACL_TYPE_ACCESS);
	n = posix_acl_read (info->acl, p_acl, FALSE);
	
	if (p_acl) {
		acl_free (p_acl);
	}
		
	if (S_ISDIR (statbuf->st_mode)) {
		p_acl = acl_get_file (path, ACL_TYPE_DEFAULT);
		
		n += posix_acl_read (info->acl, p_acl, TRUE);
		
		if (p_acl) {
			acl_free (p_acl);
		}
	}
	
	if (n > 0) {
		info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_ACL;
	} else {
		g_object_unref (info->acl);
		info->acl = NULL;
	}
	
	return GNOME_VFS_OK;
#else
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
#endif
}
                             
GnomeVFSResult 
file_set_acl (const char             *path,
			  const GnomeVFSFileInfo *info,
              GnomeVFSContext        *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}


