#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/time.h>

#include "config.h"
#include "mdb.h"

long double tz_to_time(struct timeval *tv) {
  long double t;

  t = tv->tv_usec;
  t = t / 1000000;
  t += tv->tv_sec;
  return t;
}

void search(char **expressions) {
  struct timeval tv1, tv2;
  struct timezone tz1, tz2;
  long double t1, t2;

  gettimeofday(&tv1, &tz1);
  mdb_search(expressions);
  gettimeofday(&tv2, &tz2);

  t1 = tz_to_time(&tv1);
  t2 = tz_to_time(&tv2);

  printf("# Time elapsed: %Lf\n", t2 - t1);
}

