#ifndef _BSD_SOURCE
#  define _BSD_SOURCE 1
#endif
#include <sys/types.h>

#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>

#include "gnome-vfs-mime-magic.h"
#include "gnome-vfs.h"

extern GnomeMagicEntry *gnome_vfs_mime_magic_parse (const char *filename, int *nents);

int main(int argc, char *argv[])
{
	GnomeMagicEntry *ents = NULL;
	char *filename = NULL, *out_filename;
	int nents;

	FILE *f;

	gnome_vfs_init();

	if(argc > 1) {
		if(argv[1][0] == '-') {
			fprintf(stderr, "Usage: %s [filename]\n", argv[0]);
			return 1;
		} else if(access(argv[1],F_OK))
			filename = argv[1];
	} else {
	        filename = g_strconcat (GNOME_VFS_CONFDIR, "/gnome-vfs-mime-magic", NULL);
	}

	if(!filename) {
		printf("Input file does not exist (or unspecified)...\n");
		printf("Usage: %s [filename]\n", argv[0]);
		return 1;
	}

	ents = gnome_vfs_mime_magic_parse(filename, &nents);

	if(!nents){
		fprintf (stderr, "%s: Error parsing the %s file\n", argv [0], filename);
		return 0;
	}

	out_filename = g_strconcat(filename, ".dat", NULL);

	f = fopen (out_filename, "w");
	if (f == NULL){
		fprintf (stderr, "%s: Can not create the output file %s\n", argv [0], out_filename);
		return 1;
	}

	if(fwrite(ents, sizeof(GnomeMagicEntry), nents, f) != nents){
		fprintf (stderr, "%s: Error while writing the contents of %s\n", argv [0], out_filename);
		fclose(f);
		return 1;
	}

	fclose(f);

	return 0;
}
