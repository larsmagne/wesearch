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

void search(char **expressions, int fd) {
  struct timeval tv1, tv2;
  struct timezone tz1, tz2;
  long double t1, t2;
  FILE *fdp = fdopen(fd, "w");
  search_result *sr;
  int nresults;

  gettimeofday(&tv1, &tz1);
  sr = mdb_search(expressions, fdp, &nresults);
  gettimeofday(&tv2, &tz2);

  t1 = tz_to_time(&tv1);
  t2 = tz_to_time(&tv2);

  fprintf(fdp, "# Elapsed: %Lf\n", t2 - t1);

  if (sr)
    print_search_results(sr, nresults, fdp);

  fflush(fdp);
}

