#ifndef PARSE_H_
#define PARSE_H_

int  vfs_parse_ls_lga (char *p, struct stat *s, char **filename, char **linkname);
void print_vfs_message (char *msg, ...);

#endif
