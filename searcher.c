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

#define MAX_FILE_NAME 1024

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hs:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 's':
      news_spool = optarg;
      break;
      
    case 'h':
      printf ("Usage: we:search [--spool <directory>] <directories ...>\n");
      break;

    default:
      break;
    }
  }

  return optind;
}

int
main(int argc, char **argv)
{
  int dirn;
  struct stat stat_buf;

  dirn = parse_args(argc, argv);
  
  start_time = time(NULL);

  /* Initialize key/data structures. */
  tokenizer_init();

  mdb_init();

  for ( ; dirn < argc; dirn++) {
    
    if (stat(argv[dirn], &stat_buf) == -1) {
      perror("tokenizer");
      break;
    }

    if (S_ISDIR(stat_buf.st_mode)) 
      index_directory(argv[dirn]);
  }

  mdb_report();
  flush();

  exit(0);
}

  
