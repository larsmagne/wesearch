#ifndef MDB_H
#define MDB_H

#include "tokenizer.h"

#define BLOCK_HEADER_SIZE 8
#define INSTANCE_BLOCK_HEADER_SIZE 4
#define MAX_GROUP_NAME_LENGTH 1024
#define MAX_SEARCH_ITEMS 1024

typedef struct {
  const char *word;
  int word_id;
  int *head;
  int *tail;
} word_descriptor;

typedef struct {
  int block_id;
  int dirty;
  int access_time;
  int num_used;
  char *block;
} instance_block;


word_descriptor *enter_word(char *word);
word_descriptor *lookup_word(const char *word);
void mdb_init(void);
void mdb_report(void);
void flush_group_table(void);
void flush(void);
int enter_article(document *doc, char *group, int article);
void enter_instance(unsigned int article_id, word_descriptor *wd,
		    unsigned int count);
int is_number(const char *string);

#define ARTICLE_SIZE (MAX_SAVED_BODY_LENGTH + MAX_HEADER_LENGTH*2 + 4 + 4 + 4)

typedef struct {
  char *group;
  int article;
  char author[MAX_HEADER_LENGTH];
  char subject[MAX_HEADER_LENGTH];
  char body[MAX_SAVED_BODY_LENGTH];
  time_t time;
  int goodness;
} search_result;

#define MAX_SEARCH_RESULTS 100

void read_group_table(void);
void read_next_article_id(void);
void read_word_table(void);
void read_next_instance_block_number(void);
void read_word_extension_table(void);
search_result *mdb_search(char **expressions, FILE *fd, int *nres);
void flush_instance_block(instance_block *ib);
void print_search_results(search_result *sr, int nresults, FILE *fdp);
void dump_statistics(void);
void defragment_instance_table(void);
int swap_in_instance_blocks(word_descriptor *wd);
instance_block *get_instance_block(int block_id);
void dirty_block(char *block);
void read_indexed_files_table(void);
char *index_file_name(char *name);

extern char *index_dir;
extern int total_single_word_instances;
extern int instance_buffer_size;
extern void *word_extension_table[];
extern void *word_table[];
extern int ninstance_blocks_read;
extern int current_instance_block_number;
extern int speculative_readahead;

#endif
