#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include "config.h"
#include "mdb.h"
#include "tokenizer.h"
#include "index.h"
#include "util.h"

int instances = 0;
int total_unique_words = 0;
int total_files = 0;
time_t start_time = 0;
char *from_file = NULL;
static GHashTable *indexed_files = NULL;
int suppress_duplicate_files = 0;
static FILE *indexed_files_file = NULL;


/* Read the list of parsed articles from disk into the indexed_files table. */
void read_indexed_files_table(void) {
  FILE *fp;
  char file[MAX_FILE_NAME], *f;
  
  if ((fp = fopen(index_file_name(INDEXED_FILES_FILE), "r")) == NULL)
    return;

  while (fscanf(fp, "%s\n", file) != EOF) {
    f = cmalloc(strlen(file)+1);
    strcpy(f, file);
#if DEBUG
    printf("Got %s\n", f);
#endif
    g_hash_table_insert(indexed_files, (gpointer)f, (gpointer)1);
  }
  fclose(fp);
}

void indexer_init(void) {
  if (suppress_duplicate_files) 
    read_indexed_files_table();
  if ((indexed_files_file = fopen(index_file_name(INDEXED_FILES_FILE), "a"))
      == NULL)
    merror("Opening the instance file");
  fseek(indexed_files_file, 0, SEEK_END);
}


void log_indexed_file(char *group, int article) {
  fprintf(indexed_files_file, "%s/%d\n", group, article);
}


void flush_indexed_file(void) {
  fflush(indexed_files_file);
}


void index_article(const char* group, int article) {
  char file_name[MAX_FILE_NAME];
  const char *g = group;
  char *f;
  char c;

  strcpy(file_name, NEWS_SPOOL);
  f = file_name + strlen(file_name);
  
  while ((c = *g++) != 0) {
    if (c == '.')
      c = '/';
    *f++ = c;
  }

  sprintf(f, "%d", article);

  index_file(file_name);
}

void index_word(char *word, int count, int article_id) {
  word_descriptor *wd;

  /* printf("%s\n", word); */

  /* See if the word is in the word table.  If not, enter it. */
  if ((wd = lookup_word(word)) == NULL) {
    if ((total_unique_words++ % 1000) == 0) 
      printf("Got %d words (%s); %d single instance words\n", 
	     total_unique_words-1, word,
	     total_single_word_instances);
    wd = enter_word(word);
    if (wd == NULL) {
      printf("Can't find '%s' after entering it.\n", word);
      exit(1);
    }
  } 

  instances++;
  enter_instance(article_id, wd, count);
}
  
void just_read_file (const char *file_name) {
  int file, b, total = 0;
  char *buffer[4096];

  if ((file = open(file_name, O_RDONLY|O_STREAMING)) != -1) {
    while ((b = read(file, buffer, 4096)) > 0) {
      total += b;
    }
    close(file);
  }
}

int index_file(const char *file_name) {
  document *doc;
  word_count *words;
  int count;
  int article;
  char group[MAX_FILE_NAME];
  int article_id = 0;

  now_time = time(NULL);

  /* printf("%s\n", file_name);  */
  if (path_to_article_spec(file_name, group, &article)) {

    /*
      just_read_file(file_name);
      return 0;
    */

    doc = parse_file(file_name);
    log_indexed_file(group, article);

    /* We ignore documents that have more than MAX_DISTINCT_WORDS.  They
       are probably bogus. */
    if (doc != NULL && doc->num_words <= MAX_DISTINCT_WORDS) {
      article_id = enter_article(doc, group, article);
      words = doc->words;
      while (words->word) {
	count = words->count;
	index_word(words->word, words->count, article_id);
	words++;
      }

      /* Add this article to the list for this group. */
      index_word(group, 1, article_id);

      if (*(doc->address) &&
	  !strstr(doc->address, "public.gmane.org")) {
	index_word(doc->address, 1, article_id);
      }

      return doc->num_words;
    }
  } else {
    printf("Can't find an article spec for %s\n", file_name);
  }
  return 0;
}

int index_unnamed_file(const char *file_name) {
  document *doc;
  word_count *words;
  int count;
  int article_id = 0;

  if ((doc = parse_file(file_name)) == NULL)
    return 0;

  now_time = time(NULL);

  log_indexed_file(doc->group, doc->article);

  /* We ignore documents that have more than MAX_DISTINCT_WORDS.  They
     are probably bogus. */
  if (doc != NULL && doc->num_words <= MAX_DISTINCT_WORDS) {
    article_id = enter_article(doc, doc->group, doc->article);
    words = doc->words;
    while (words->word) {
      count = words->count;
      index_word(words->word, words->count, article_id);
      words++;
    }

    /* Add this article to the list for this group. */
    index_word(doc->group, 1, article_id);

    if (*(doc->address) &&
	!strstr(doc->address, "public.gmane.org")) {
      index_word(doc->address, 1, article_id);
    }

    return doc->num_words;
  }
  return 0;
}

void index_directory(const char* dir_name) {
  DIR *dirp;
  struct dirent *dp;
  char file_name[MAX_FILE_NAME];
  struct stat stat_buf;
  time_t elapsed;
  int total_words = 0;

  //printf("%s\n", dir_name); 

  if ((dirp = opendir(dir_name)) == NULL) {
    perror("indexer");
    return;
  }
    
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
	total_words += index_unnamed_file(file_name);

	if (! (total_files++ % 1000)) {
	  /*
	  if (total_files > 1000000) {
	    mdb_report();
	    flush();
	    exit(0);
	  }
	  */

	  elapsed = time(NULL)-start_time;
	  if (elapsed != 0) {
	    printf("    %d files (%d per second)\n",
		   total_files, (int)(total_files/elapsed));
	    printf("    %d instances (%d per second)\n",
		   instances, (int)(instances/elapsed));
	  }
	}
      }
    }
  }
  closedir(dirp);
}

