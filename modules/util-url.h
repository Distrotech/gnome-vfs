#ifndef UTIL_URL_H_
#define UTIL_URL_H_ 1

#define URL_DEFAULTANON 1
#define URL_NOSLASH     2

char *vfs_split_url (const char *path, char **host, char **user, int *port, char **pass,
		     int default_port, int flags);

#endif
