#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "config.h"
#include "mdb.h"
#include "tokenizer.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>

int total_words = 0;
int total_files = 0;
time_t start_time = 0;

void index_directory(const char* dir_name) {
  DIR *dirp;
  struct dirent *dp;
  char file_name[1024];
  document *doc;
  word_count *words;
  word_descriptor *wd;
  struct stat stat_buf;
  int count;
  time_t elapsed;

  printf("%s\n", dir_name); 
    
  dirp = opendir(dir_name);
    
  while ((dp = readdir(dirp)) != NULL) {

    snprintf(file_name, sizeof(file_name), "%s/%s", dir_name,
	     dp->d_name);

    if (strcmp(dp->d_name, ".") &&
	strcmp(dp->d_name, "..")) {
    
      if (stat(file_name, &stat_buf) == -1) {
	perror("tokenizer");
	break;
      }
    
      if (S_ISDIR(stat_buf.st_mode)) 
	index_directory(file_name);
      else if (is_number(dp->d_name)) {
	total_files++;
	doc = parse_file(file_name);
	if (doc != NULL) {
	  words = doc->words;
	  while (words->word) {
	    count = (words->count);

	    /* printf("'%s'\n", words->word); */
	    /* Retrieve a key/data pair. */
	    if ((wd = lookup_word(words->word)) == NULL) {
	      if ((total_words++ % 1000) == 0) {
		printf("Got %d words (%s)\n", total_words-1, words->word);
		elapsed = time(NULL)-start_time;
		if (elapsed != 0) {
		  printf("    %d files (%d per second)\n",
			 total_files, total_files/elapsed);
		}
	      }
	      wd = enter_word(words->word);
	      if (wd == NULL) {
		printf("Can't find '%s' after entering it.\n", words->word);
		exit(1);
	      }
	    }

	    enter_instance(wd, words->count, 0);
	    words++;
	  }
	}
      }
    }
  }
}

int
main(int argc, char **argv)
{
  int ret, t_ret, dirn;
  struct stat stat_buf;
  int average_key_size = 8;
  int average_data_size = 4;
  int pagesize = 512;

  start_time = time(NULL);

  /* Initialize key/data structures. */
  tokenizer_init();

  mdb_init();
  
  for (dirn = 1; dirn < argc; dirn++) {
    
    if (stat(argv[dirn], &stat_buf) == -1) {
      perror("tokenizer");
      break;
    }

    if (S_ISDIR(stat_buf.st_mode)) 
      index_directory(argv[dirn]);
  }

  mdb_report();
  
}

  
