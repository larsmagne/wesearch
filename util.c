#define _LARGEFILE64_SOURCE

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "tokenizer.h"
#include "config.h"
#include "mdb.h"
#include "util.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

/* The same as strcpy, but returns a pointer to the end of the
   destination string. */
char *mstrcpy(char *dest, char *src) {
  while ((*dest++ = *src++) != 0)
    ;
  return dest;
}


/* The same as strcpy, but returns a pointer to the end of the
   source string. */
char *sstrcpy(char *dest, char *src) {
  while ((*dest++ = *src++) != 0)
    ;
  return src;
}


/* Say whether a string is all-numerical. */
int is_number(const char *string) {
  while (*string)
    if (! isdigit(*string++)) 
      return 0;
  return 1;
}


/* Return the size of a file. */
loff_t file_size (int fd) {
  struct stat64 stat_buf;
  if (fstat64(fd, &stat_buf) == -1) {
    perror("Statting a file to find out the size");
    exit(1);
  }
  return stat_buf.st_size;
}

