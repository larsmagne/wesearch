#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#if defined(__FreeBSD__)
#  include <sys/mman.h>
#endif
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

static int from_stdin = 0;

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "h0ds:f:i:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'd':
      suppress_duplicate_files = 1;
      break;

    case 's':
      news_spool = optarg;
      break;
      
    case 'i':
      index_dir = optarg;
      break;
      
    case 'f':
      from_file = optarg;
      break;
      
    case '0':
      from_stdin = 1;
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
  /* Unfortunately, FreeBSD does not implement mlockall - see PR kern/43426 */
  if (mlockall(MCL_FUTURE) == -1) {
    perror("we-index");
  }

  setgid(500);
  setuid(500);
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
	       total_files - 1, 
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

void index_from_stdin(void) {
  FILE *fp;
  char file[MAX_FILE_NAME];
  time_t elapsed, last_elapsed, now;
  char *str;

  fp = fdopen(0, "r");

  while (fgets(file, MAX_FILE_NAME, fp)) {
    if (! *file)
      break;

    if ((str = index(file, '\n')) != NULL)
      *str = 0;
    
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
	       total_files - 1, 
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
}

void closedown(int i) {
 time_t now = time(NULL);
 printf("Closed down at %s", ctime(&now));
 flush();
 flush_indexed_file();
 exit(0);
}

int main(int argc, char **argv)
{
  int dirn;
  struct stat stat_buf;

  if (geteuid() == 0)
    lock_and_uid();
  dirn = parse_args(argc, argv);
  
  start_time = time(NULL);
  is_indexing_p = 1;

  if (signal(SIGHUP, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (signal(SIGINT, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  /* Initialize key/data structures. */
  tokenizer_init();
  indexer_init();
  mdb_init();

  if (from_file != NULL) {
    index_from_file(from_file);
  } else if (1) {
    //index_directory("/home/larsi/clocc");
    index_directory("/ispool/datespool");
  } else if (from_stdin != 0) {
    index_from_stdin();
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

  
