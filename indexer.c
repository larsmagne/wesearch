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
#include "index.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hs:f:i:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 's':
      news_spool = optarg;
      break;
      
    case 'i':
      index_dir = optarg;
      break;
      
    case 'f':
      from_file = optarg;
      break;
      
    case 'h':
      printf ("Usage: we:index [--spool <directory>] <directories ...>\n");
      break;

    default:
      break;
    }
  }

  return optind;
}

void lock_and_uid(void) {
  if (mlockall(MCL_FUTURE) == -1) {
    perror("we-index");
    exit(1);
  }

  setuid(500);
  setgid(500);
}

static time_t last_start_time = 0;
static int last_total_files = 0;

void index_from_file(char *from_file) {
  FILE *fp;
  char file[MAX_FILE_NAME], *f;
  time_t elapsed, last_elapsed, now;

  if ((fp = fopen(from_file, "r")) == NULL)
    return;

  strcpy(file, NEWS_SPOOL);
  f = file + strlen(file);

  while (fscanf(fp, "%s\n", f) != EOF) {
    index_file(file);
    if (! (total_files++ % 1000)) {
      now = time(NULL);
      elapsed = now-start_time;
      if (last_start_time == 0) {
	last_start_time = start_time;
      }
      if (elapsed != 0) {
	last_elapsed = now-last_start_time;
	printf("    %d files (%d/s; %d/s last %d seconds)\n",
	       total_files, 
	       (int)(total_files/elapsed),
	       (last_elapsed?
		(int)((total_files-last_total_files) / last_elapsed):
		0),
	       (int)last_elapsed);
	last_start_time = now;
	last_total_files = total_files;
	printf("    %d instances (%d per second)\n",
	       instances, (int)(instances/elapsed));
      }
    }
  }

  fclose(fp);

}

int main(int argc, char **argv)
{
  int dirn;
  struct stat stat_buf;

  lock_and_uid();
  dirn = parse_args(argc, argv);
  
  start_time = time(NULL);

  /* Initialize key/data structures. */
  tokenizer_init();

  mdb_init();

  if (from_file != NULL) {
    index_from_file(from_file);
  } else {
    for ( ; dirn < argc; dirn++) {
      
      if (stat(argv[dirn], &stat_buf) == -1) {
	perror("tokenizer");
	break;
      }
      
      if (S_ISDIR(stat_buf.st_mode)) 
	index_directory(argv[dirn]);
      else if (S_ISREG(stat_buf.st_mode)) 
	index_file(argv[dirn]);
    }
  }

  mdb_report();
  flush();

  exit(0);
}

  
