#ifndef MDB_H
#define MDB_H

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

#endif
