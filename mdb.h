#ifndef MDB_H
#define MDB_H

#include "tokenizer.h"

#define BLOCK_HEADER_SIZE 8
#define INSTANCE_BLOCK_HEADER_SIZE 4

typedef struct {
  const char *word;
  int word_id;
  int head;
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
void write_group_table(void);
int enter_article(document *doc, char *group, int article);
void enter_instance(unsigned int article_id, word_descriptor *wd,
		    unsigned int count);
int is_number(const char *string);

#define ARTICLE_SIZE (MAX_SAVED_BODY_LENGTH + MAX_HEADER_LENGTH*2 + 4 + 4 + 4)

#endif
