#define _POSIX_C_SOURCE 200306L

#include <fcntl.h>


void no_reuse(int fd) {
#ifdef POSIX_FADV_NOREUSE
  if (posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE) < 0) {
    perror("fadvise");
  }
#endif
}


