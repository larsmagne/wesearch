#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include "config.h"
#include "mdb.h"
#include "tokenizer.h"
#include "indexer.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "search.h"

#define MAX_FILE_NAME 1024

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hs:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'h':
      printf ("Usage: we-search [--spool <directory>] <directories ...>\n");
      break;

    default:
      break;
    }
  }

  return optind;
}

int main(int argc, char **argv) {
  int opts;
  opts = parse_args(argc, argv);
  
  mdb_init();
  tokenizer_init();

  search(argv + opts);
  exit(0);
}
