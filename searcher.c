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
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "search.h"
#include "searcher.h"

#define MAX_FILE_NAME 1024

static int do_stats = 0;
static int do_defragment = 0;

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hsdi:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'i':
      index_dir = optarg;
      break;
      
    case 's':
      do_stats = 1;
      break;
      
    case 'd':
      do_defragment = 1;
      break;
      
    case 'h':
      printf ("Usage: we-search [--index <directory>] <terms ...>\n");
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

  if (do_stats) {
    dump_statistics();
    exit(0);
  }

  if (do_defragment) {
    defragment_instance_table();
    exit(0);
  }

  search(argv + opts, 1);
  exit(0);
}
