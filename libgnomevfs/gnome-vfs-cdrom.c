/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-vfs-cdrom.c - cdrom utilities

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
           Gene Z. Ragan <gzr@eazel.com>
           Seth Nickell  <snickell@stanford.edu>
*/

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-i18n.h>

#ifdef __linux__
#include <linux/cdrom.h>
#endif

#ifdef HAVE_SYS_CDIO_H
#include <sys/cdio.h>
#endif

#include "gnome-vfs-iso9660.h"
#include "gnome-vfs-cdrom.h"


int
_gnome_vfs_get_cdrom_type (const char *vol_dev_path, int* fd)
{
#if defined (HAVE_SYS_MNTTAB_H)   /* Solaris */
	GString *new_dev_path;
	struct cdrom_tocentry entry;
	struct cdrom_tochdr header;
	int type;

	/* For ioctl call to work dev_path has to be /vol/dev/rdsk.
	 * There has to be a nicer way to do this.
	 */
	new_dev_path = g_string_new (vol_dev_path);
	new_dev_path = g_string_insert_c (new_dev_path, 9, 'r');
	*fd = open (new_dev_path->str, O_RDONLY | O_NONBLOCK);
	g_string_free (new_dev_path, TRUE);

	if (*fd < 0) {
		return CDS_DATA_1;
	}

	if (ioctl (*fd, CDROMREADTOCHDR, &header) == 0) {
		return CDS_DATA_1;
	}

	type = CDS_DATA_1;
	
	for (entry.cdte_track = 1;
	     entry.cdte_track <= header.cdth_trk1;
	     entry.cdte_track++) {
		entry.cdte_format = CDROM_LBA;
		if (ioctl (*fd, CDROMREADTOCENTRY, &entry) == 0) {
			if (entry.cdte_ctrl & CDROM_DATA_TRACK) {
				type = CDS_AUDIO;
				break;
			}
		}
	}

	return type;
#elif defined(HAVE_SYS_MNTCTL_H)
	return CDS_NO_INFO;
#elif defined(__FreeBSD__)
	struct ioc_toc_header header;
	struct ioc_read_toc_single_entry entry;
	int type;
#ifndef CDROM_DATA_TRACK
#define CDROM_DATA_TRACK 4
#endif

	*fd = open (vol_dev_path, O_RDONLY|O_NONBLOCK);
	if (*fd < 0) {
	    	return CDS_DATA_1;
	}

	if (ioctl (*fd, CDIOREADTOCHEADER, &header) == 0) {
	    	return CDS_DATA_1;
	}

	type = CDS_DATA_1;
	for (entry.track = header.starting_track;
		entry.track <= header.ending_track;
		entry.track++) {
	    	entry.address_format = CD_LBA_FORMAT;
		if (ioctl (*fd, CDIOREADTOCENTRY, &entry) == 0) {
		    	if (entry.entry.control & CDROM_DATA_TRACK) {
			    	type = CDS_AUDIO;
				break;
			}
		}
	}

	return type;
#else
	*fd = open (vol_dev_path, O_RDONLY|O_NONBLOCK);
	return ioctl (*fd, CDROM_DISC_STATUS, CDSL_CURRENT);
#endif
}

#ifdef __linux__
static int
get_iso9660_volume_name_data_track_offset (int fd)
{
	struct cdrom_tocentry toc;
	char toc_header[2];
	int i, offset;

	if (ioctl (fd, CDROMREADTOCHDR, &toc_header)) {
		return 0;
	}

	for (i = toc_header[0]; i <= toc_header[1]; i++) {
		memset (&toc, 0, sizeof (struct cdrom_tocentry));
		toc.cdte_track = i;
		toc.cdte_format = CDROM_MSF;
		if (ioctl (fd, CDROMREADTOCENTRY, &toc)) {
			return 0;
		}

		if (toc.cdte_ctrl & CDROM_DATA_TRACK) {
			offset = ((i == 1) ? 0 :
				(int)toc.cdte_addr.msf.frame +
				(int)toc.cdte_addr.msf.second*75 +
				(int)toc.cdte_addr.msf.minute*75*60 - 150);
			return offset;
		}
	}

	return 0;
}
#endif

char *
_gnome_vfs_get_iso9660_volume_name (int fd)
{
	struct iso_primary_descriptor iso_buffer;
	int offset;

#ifdef __linux__
	offset = get_iso9660_volume_name_data_track_offset (fd);
#else
	offset = 0;
#endif

	lseek (fd, (off_t) 2048*(offset+16), SEEK_SET);
	read (fd, &iso_buffer, 2048);

	if (iso_buffer.volume_id[0] == 0) {
		return g_strdup (_("ISO 9660 Volume"));
	}
	
	return g_strndup (iso_buffer.volume_id, 32);
}
