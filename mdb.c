#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "tokenizer.h"
#include "config.h"
#include "mdb.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#define DEBUG 0
#define INSTANCE_BLOCK_LENGTH 255
#define MAX_GROUPS 16000

static void *word_table[WORD_SLOTS];
static void *word_extension_table[WORD_EXTENSION_SLOTS];

static int instance_table[INSTANCE_TABLE_SIZE];
static instance_block instance_buffer[INSTANCE_BUFFER_SIZE];

static int num_word_extension_blocks = 1;
static int current_instance_block_number = 1;
static int num_word_id = 1;
static int next_free_buffer = 1;
static int instance_file = 0;
static int word_extension_file = 0;
static int article_file = 0;
static int word_file = 0;
static int current_article_id = 1;
static int current_group_id = 1;
static GHashTable *group_table = NULL;
static char *reverse_group_table[MAX_GROUPS];

void merror(char *error) {
  perror(error);
  exit(1);
}

/* A function used when debugging.  It dumps the contents of the
   specified word block. */
void dump_word_block(char *block) {
  printf("Address: %x\n", (unsigned int)block);
  printf(" Next: %d\n", *((int*)block));
  block += 4;
  printf(" Last: %d\n", *((short*)block));
  block += 2;
  printf(" Dirty: %d\n", *block);
  block += 2;

  while (*block) {
    printf("  Word: ");
    while (*block) {
      printf("%c", *block++);
    }
    block++;
    printf("\n");
    printf("   word_id: %d\n", *((int*)block));
    block += 4;
    printf("   head: %d\n", *((int*)block));
    block += 4;
    printf("   tail: %d\n", *((int*)block));
    block += 4;
  }
}

/* Allocate a new, fresh instance block.  This is done by just
   extending the size of the instance file by one block's worth. */
int allocate_instance_block(void) {
  current_instance_block_number++;
  if (ftruncate(instance_file,
		(current_instance_block_number + 1) * BLOCK_SIZE)
      == -1) {
    merror("Increasing the instance file size");
  }
  return current_instance_block_number;
}

/* Allocate a new, fresh word extension block. */
int allocate_extension_block(void) {
  if (ftruncate(word_extension_file,
		(num_word_extension_blocks + 1) * BLOCK_SIZE) == -1) 
    merror("Increasing the word extension file");
  
  return num_word_extension_blocks;
}

/* Find a free in-memory instance buffer. */
int get_free_instance_buffer(void) {
  while (instance_buffer[next_free_buffer].block_id != 0) {
    if (next_free_buffer++ > INSTANCE_BUFFER_SIZE) {
      fprintf(stderr, "INSTANCE_BUFFER_SIZE is too small.\n");
      exit(1);
    }
  }

  return next_free_buffer;
}

void read_block(int fd, char *block, int block_size) {
  int rn = 0, ret;
  
  while (rn < block_size) {
    ret = read(fd, block + rn, block_size - rn);
    if (ret == 0) {
      fprintf(stderr, "Reached end of file (block_size: %d).\n", block_size);
      exit(1);
    } else if (ret == -1) 
      merror("Reading a block");
      
    rn += ret;
  }
}

/* Read a block from a file at a specified offset into a in-memory
   block. */
void read_into(int fd, int block_id, char *block, int block_size) {
  if (lseek(fd, block_id * block_size, SEEK_SET) == -1) {
    merror("Seeking before reading a block");
  }

  read_block(fd, block, block_size);
}

/* Determine how many entries in an instance block are used. */
int instance_block_used_entries(char *block) {
  int n = 0;

  /* Skip past the header. */
  block += INSTANCE_BLOCK_HEADER_SIZE;

  /* If the first byte of the instance is zero, then we have reached
     the end of the block.  We always extend the block if it's full,
     so there's no danger of segfaulting here without checking against
     INSTANCE_HEADER_BLOCK_SIZE. */

  while (block[n*4]) 
    n++;

  return n;
}

/* Get an instance block from the instance file, and put it into the
   in-memory instance buffer. */
void swap_instance_block_in(int bn, int block_id) {
  instance_block *ib = &instance_buffer[bn];

#if DEBUG  
  printf("Swapping in instance block %d\n", block_id);
#endif
  
  if (ib->block == NULL) {
    ib->block = (char*) malloc(BLOCK_SIZE);

    if (ib->block == NULL) {
      perror("chow-indexer");
      exit(1);
    }
  }

  read_into(instance_file, block_id, ib->block, BLOCK_SIZE);
  ib->block_id = block_id;
  ib->dirty = 0;
  ib->num_used = instance_block_used_entries(ib->block);
}

/* Get an instance block. */
instance_block *get_instance_block(int block_id) {
  int bn;

  if (! (bn = instance_table[block_id])) {
    /* The block is not in the buffer, so we need to swap it in. */
    bn = get_free_instance_buffer();
    swap_instance_block_in(bn, block_id);
    instance_table[block_id] = bn;
  }

#if DEBUG  
  printf("bn is now %d, block_id %d\n", bn, block_id);
#endif
  
  if (instance_buffer[bn].block == NULL) {
    printf("Got a zero block.\n");
    exit(1);
  }
  
  return &(instance_buffer[bn]);
}

/* Various hashes used for hashing words. */
unsigned int one_at_a_time_hash(const char *key, int len)
{
  unsigned int   hash, i;
  for (hash=0, i=0; i<len; ++i)
  {
    hash += key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return (hash & (WORD_SLOTS - 1));
} 

unsigned int
hash8(const char *s, int len) {
  register const unsigned int *iv = (const unsigned int *)s;
  register unsigned int h = 0;
  register unsigned int n;
  if (len > 3) {
    if (len & 3) {
      h = *(unsigned int *)(s + len - 4);
    }
    len /= 4;
    for (n = 0; n < len; n++) {
      h+= iv[n];
      h = (h << 7) | (h >> (32 - 7));
    }
  } else {
    if (len > 1) {
      h += s[1];
      if (len == 3)
	h += s[2];
    }
    h += s[0];
  }
  h ^= (h >> 13);
  h ^= (h >> 7);
  return (h % (WORD_SLOTS - 1));
}

int naive_hash(const char *word, int len) {
  int hash = 0;
  while (*word)
    hash += *word++;

  return hash % WORD_SLOTS;   
}

int hash(const char *word) {
  int length = strlen(word);
  //return hash8(word, length);
  //return naive_hash(word, length);
  return one_at_a_time_hash(word, length);
}

/* malloc a word block a new, fresh word block. */
char *allocate_word_block(const char *word) {
  int slot_number = hash(word);
  char *block = (char*) malloc(BLOCK_SIZE);

  if (block == NULL) {
    perror("chow-indexer");
    exit(1);
  }

  bzero(block, BLOCK_SIZE);
  
  word_table[slot_number] = block;
  return block;  
}

/* Given that we have found the (head of the) word block for a
   specific word, find the word descriptor for this word.  Note that
   this function isn't reentrant -- it returns a static
   word_descriptor.  The caller has to save the values itself if it
   wants to keep them. */
word_descriptor *block_search_word(const char *word, const char *word_block) {
  const char *b = word_block + BLOCK_HEADER_SIZE; /* Skip past the header. */
  const char *w;
  static word_descriptor dword;

  while (TRUE) {
    w = word;
    if (*b == 0) {
      /* We have reached the end of the block; check whether it's the
	 last block. */
      if (*((int*)word_block) != 0) {
	/* printf("Going to the next block, %d.\n", *((int*)word_block)); */
	word_block = word_extension_table[*((int*)word_block)];
	if (word_block == NULL) {
	  printf("Went to a non-existing block.\n");
	  exit(1);
	}
	b = word_block + BLOCK_HEADER_SIZE;
      } else 
	return NULL;
    }

    if (word_block == NULL) {
      printf("Went to a non-existing block here.\n");
      exit(1);
    } 

    while ((*b != 0) && (*b++ == *w++))
      ;

    if ((! *b) && (! *w)) {
      b++;
      /* We've found the word. */
      dword.word = word;
      dword.word_id = *((int*)b);
      b += 4;
      dword.head = *((int*)b);
      b += 4;
      dword.tail = ((int*)b);
      b += 4;
#if DEBUG
      printf("Found %s, %d, %d, %x\n",
	     dword.word, dword.word_id, dword.head, (int)&dword.tail);
#endif
      return &dword;
    } else {
      while (*b++)
	;
      /* Skip past the data after the string. */
      b += 3*sizeof(int);
    }
  }
}

/* Find the word block for word.  */
char *lookup_word_block(const char *word) {
  unsigned int slot_number = hash(word);
#if DEBUG
  printf("Slot number %d, %d.\n", slot_number, hash(word));
#endif
  return word_table[slot_number];
}

/* Lookup a word in the word table. */
word_descriptor *lookup_word(const char *word) {
  char *word_block = lookup_word_block(word);

#if DEBUG
  printf("Looking up '%s'...\n", word);
#endif
  
  if (word_block != NULL) {
#if DEBUG
    printf("Looking up %s\n", word);
    dump_word_block(word_block);
#endif    
    return block_search_word(word, word_block);
  } else
    return NULL;
}

/* Mark this word block as dirty. */
void dirty_block(char *block) {
  *(block+6) = '1';
}

void clean_block(char *block) {
  *(block+6) = 0;
}

/* Mark this word block as dirty. */
int dirty_block_p(char *block) {
  return *(block+6);
}

/* Get the position of the end of the entries in the word block. */
int get_last_word(short *block) {
  return *(block+2);
}

/* Set the position of the end of the entries in the word block. */
void set_last_word(short *block, short last) {
  *(block+2) = last;
}

/* Enter a word into the word table. */
word_descriptor *enter_word(char *word) {
  char *word_block = lookup_word_block(word);
  int next_block_pointer;
  int last_word;
  char *b, *w = word;
  char *new_block;
  int instance_block;

  if (word_block == NULL)
    word_block = allocate_word_block(word);

  /* Find the last block for this word. */
  while ((next_block_pointer = *((int*)(word_block))) != 0) 
    word_block = word_extension_table[next_block_pointer];

#if DEBUG
  printf("Entering '%s'...\n", word);
#endif
  
  /* We now have the block where we're (possibly) going to put
     this word. */
  last_word = get_last_word((short*)word_block);
  if ((BLOCK_HEADER_SIZE + last_word +
       strlen(word) + 3*sizeof(int) + 1 + 1) > BLOCK_SIZE) {
    /* There's no room in this block, so we add a new block. */
    printf("Allocating an extension block for %s: %d\n",
	   word, num_word_extension_blocks);
    
    new_block = (char*) malloc(BLOCK_SIZE);
    
    if (new_block == NULL) {
      perror("chow-indexer");
      exit(1);
    }

    bzero(new_block, BLOCK_SIZE);

    allocate_extension_block();
    word_extension_table[num_word_extension_blocks] = new_block;
    *((int*)word_block) = num_word_extension_blocks++;

    dirty_block(word_block);
    
    word_block = new_block;
    last_word = 0;
  }

  /* We now know we have room to write the word to the block. */
  b = word_block + BLOCK_HEADER_SIZE + last_word;
  
  while ((*b++ = *w++) != 0)
    ;
  *((int*) b) = num_word_id++;
  b += 4;
  instance_block = allocate_instance_block();
  *((int*) b) = instance_block;
  b += 4;
  *((int*) b) = instance_block;
  b += 4;

  set_last_word((short*) word_block, b - word_block - BLOCK_HEADER_SIZE);
  dirty_block(word_block);

#if DEBUG
  dump_word_block(word_block);
#endif
 
  return lookup_word(word);
}

/* Say whether a string is all-numerical. */
int is_number(const char *string) {
  while (*string)
    if (! isdigit(*string++)) 
      return 0;
  return 1;
}

/* Enter a word instance into the instance table. */
void enter_instance(unsigned int article_id, word_descriptor *wd,
		    unsigned int count) {
  instance_block *ib = get_instance_block(*wd->tail);
  char *block = ib->block;
  int num_used = ib->num_used;
  unsigned int tmp = article_id;
  int new_instance_block;

  /* Go to the end of the block. */
  block += INSTANCE_BLOCK_HEADER_SIZE;
  block += num_used * 4;

#if DEBUG
  printf("Skipping %d, %d\n",
	 num_used * 4 + INSTANCE_BLOCK_HEADER_SIZE, num_used);
#endif

  /* Enter the data. */
  tmp |= (count << 28);
  *((int*) block) = tmp;
  block += 4;

#if DEBUG
  printf("Instanced %d:%d into %d\n", article_id, count, ib->block_id);
#endif

  /* Do accounting. */
  num_used++;
  ib->num_used = num_used;
  ib->dirty = 1;

  /* If we've now filled this block, we allocate a new one, and hook
     it onto the end of this chain. */
  if (num_used++ == INSTANCE_BLOCK_LENGTH) {
    new_instance_block = allocate_instance_block();
    block = ib->block;
    *((int*) block) = new_instance_block;
    *wd->tail = new_instance_block;
  }
}


void mdb_init(void) {
  if ((instance_file = open(INSTANCE_FILE, O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the instance file");

  if ((article_file = open(ARTICLE_FILE, O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the article file");

  if ((word_extension_file =
       open(WORD_EXTENSION_FILE, O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the word extension file");

  if ((word_file = open(WORD_FILE, O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the word file");

  if (ftruncate(word_file, WORD_SLOTS * BLOCK_SIZE) == -1) 
    merror("Truncating the word file");

  group_table = g_hash_table_new(g_str_hash, g_str_equal);
  bzero(word_table, WORD_SLOTS * 4);
  
  read_next_article_id();
  read_group_table();
  read_word_table();
  read_word_extension_table();
  read_next_instance_block_number();
}

void mdb_report(void) {
  printf("Total number of extension blocks allocated: %d\n",
	 num_word_extension_blocks);
}

char *mstrcpy(char *dest, char *src) {
  while ((*dest++ = *src++) != 0)
    ;
  return dest;
}

char *sstrcpy(char *dest, char *src) {
  while ((*dest++ = *src++) != 0)
    ;
  return src;
}

int group_id(char *group) {
  int gid;
  char *g;
  
  if ((gid = (int)g_hash_table_lookup(group_table, (gpointer)group)) == 0) {
    current_group_id++;
    gid = current_group_id;
    g = (char*)malloc(strlen(group)+1);
    strcpy(g, group);
    g_hash_table_insert(group_table, (gpointer)g, (gpointer)gid);
    reverse_group_table[gid] = g;
  }
  
  return gid;
}

int write_from(int fp, char *buf, int size) {
  int w = 0, written = 0;

  while (written < size) {
    if ((w = write(fp, buf + written, size - written)) < 0)
      merror("Writing a block");

    written += w;
  }
  return written;
}

int enter_article(document *doc, char *group, int article) {
  char abuf[ARTICLE_SIZE];
  char *b = abuf;
  int gid = group_id(group);

  bzero(abuf, ARTICLE_SIZE);
  
  *((int*)b) = gid;
  b += 4;
  *((int*)b) = article;
  b += 4;
  *((int*)b) = doc->time;
  b += 4;

  b = mstrcpy(b, doc->author);
  b = mstrcpy(b, doc->subject);
  b = mstrcpy(b, doc->body);
  
  current_article_id++;
  lseek(article_file, current_article_id * ARTICLE_SIZE, SEEK_SET);
  write_from(article_file, abuf, ARTICLE_SIZE);
  return current_article_id;
}

void read_article(char *block, int article_id) {
  if (lseek(article_file, article_id * ARTICLE_SIZE, SEEK_SET) == -1)
    merror("Reading an article from the article file");
  
  read_block(article_file, block, ARTICLE_SIZE);
}

void flush_group_entry(gpointer key, gpointer value, gpointer user_data) {
  FILE *fp = (FILE *) user_data;
  char *group = (char*) key;
  int group_id = (int) value;

  fprintf(fp, "%s %d\n", group, group_id);
}

void flush_group_table(void) {
  FILE *fp;
  if ((fp = fopen(GROUP_FILE, "w")) == NULL)
    merror("Opening the group file");

  g_hash_table_foreach(group_table, flush_group_entry, (gpointer)fp);

  fclose(fp);
}

void flush_instance_block(instance_block *ib) {
  if (lseek(instance_file, ib->block_id * BLOCK_SIZE, SEEK_SET) == -1) 
    merror("Flushing an instance block");

#if DEBUG
  printf("Flushing instance block %d\n", ib->block_id);
#endif
  
  write_from(instance_file, ib->block, BLOCK_SIZE);
  ib->dirty = 0;
}

void flush_instance_table(void) {
  int i;
  instance_block *ib;
  
  for (i = 0; i<INSTANCE_TABLE_SIZE; i++) {
    if (instance_table[i]) {
      ib = &instance_buffer[instance_table[i]];
      if (ib->dirty)
	flush_instance_block(ib);
    }
  }
}

void flush_word_block(char *block, int block_number) {
  if (lseek(word_file, block_number * BLOCK_SIZE, SEEK_SET) == -1) 
    merror("Flushing a word block");

  clean_block(block);
  write_from(word_file, block, BLOCK_SIZE);
}

void flush_word_table(void) {
  int i;
  char *block;

  for (i = 0; i<WORD_SLOTS; i++) {
    if ((block = (char*)word_table[i]) != NULL) 
      if (dirty_block_p(block)) 
	flush_word_block(block, i);
  }
}

void flush_word_extension_block(char *block, int block_number) {
  if (lseek(word_extension_file, block_number * BLOCK_SIZE, SEEK_SET) == -1) 
    merror("Flushing a word extension block");

  write_from(word_extension_file, block, BLOCK_SIZE);
  clean_block(block);
}

void flush_word_extension_table(void) {
  int i;
  char *block;

  for (i = 0; i<num_word_extension_blocks; i++) {
    if ((block = (char*)word_extension_table[i]) != NULL) 
      if (dirty_block_p(block)) 
	flush_word_extension_block(block, i);
  }
}

void flush(void) {
  flush_word_table();
  flush_word_extension_table();
  flush_instance_table();
  flush_group_table();
}

void read_group_table(void) {
  FILE *fp;
  char group[MAX_GROUP_NAME_LENGTH], *g;
  int group_id;
  
  if ((fp = fopen(GROUP_FILE, "r")) == NULL)
    return;

  while (fscanf(fp, "%s %d\n", group, &group_id) != EOF) {
    g = (char*)malloc(strlen(group)+1);
    strcpy(g, group);
    printf("Got %s (%d)\n", g, group_id);
    g_hash_table_insert(group_table, (gpointer)g, (gpointer)group_id);
    reverse_group_table[group_id] = g;
  }
  fclose(fp);
}

void read_next_article_id(void) {
  struct stat stat_buf;
  int size;
  
  if (fstat(article_file, &stat_buf) == -1)
    merror("Statting the article file");

  size = stat_buf.st_size;

  if ((size % ARTICLE_SIZE) != 0) {
    printf("Invalid size for the article file (%d) for this article size (%d).\n",
	   size, ARTICLE_SIZE);
    exit(1);
  }

  current_article_id = size / ARTICLE_SIZE;
}

int file_size (int fd) {
  struct stat stat_buf;
  if (fstat(fd, &stat_buf) == -1)
    merror("Statting a file to find out the size");
  return stat_buf.st_size;
}

void read_next_instance_block_number(void) {
  int size = file_size(instance_file);
  
  if ((size % BLOCK_SIZE) != 0) {
    printf("Invalid size for the instance file (%d) for this block size (%d).\n",
	   size, BLOCK_SIZE);
    exit(1);
  }

  current_instance_block_number = size / BLOCK_SIZE;
}

void read_word_table(void) {
  char *block = NULL;
  int i;
    
  lseek(word_file, 0, SEEK_SET);

  for (i = 0; i<WORD_SLOTS; i++) {
    if (block == NULL)
      block = (char*)malloc(BLOCK_SIZE);

    read_block(word_file, block, BLOCK_SIZE);
    if (get_last_word((short*)block)) {
      word_table[i] = block;
      block = NULL;
    } else
      word_table[i] = NULL;

  }

  if (block)
    free(block);
}

void read_word_extension_table(void) {
  char *block = NULL;
  int i;
  int size = file_size(word_extension_file);
  
  if ((size % BLOCK_SIZE) != 0) {
    printf("Invalid size for the word extension file (%d) for this block size (%d).\n",
	   size, BLOCK_SIZE);
    exit(1);
  }
  num_word_extension_blocks = size / BLOCK_SIZE;

#if DEBUG
  printf("Number of word extension blocks: %d\n", num_word_extension_blocks);
#endif
  
  lseek(word_extension_file, 0, SEEK_SET);
  for (i = 0; i<num_word_extension_blocks; i++) {
    block = (char*)malloc(BLOCK_SIZE);
    read_block(word_extension_file, block, BLOCK_SIZE);
    word_extension_table[i] = block;
  }

  num_word_extension_blocks++;
}

typedef struct {
  char *word;
  int negate_p;
  int word_id;
  instance_block *instance;
  int next;
} search_item;

typedef struct {
  char *group;
  int article;
  char author[MAX_HEADER_LENGTH];
  char subject[MAX_HEADER_LENGTH];
  char body[MAX_SAVED_BODY_LENGTH];
  time_t time;
  int goodness;
} search_result;

#define MAX_SEARCH_ITEMS 1024
#define MAX_SEARCH_RESULTS 1024

static search_item search_items[MAX_SEARCH_ITEMS];
static search_result search_results[MAX_SEARCH_RESULTS];

unsigned int next_instance(search_item *si) {
  unsigned int result;
  
  if (!si->instance)
    return 0;

  if (si->next == INSTANCE_BLOCK_LENGTH) {
    si->instance = get_instance_block(*((int*)(si->instance->block)));
    si->next = 0;
  }

  if (si->next > si->instance->num_used)
    return 0;

  result = *((int*)(si->instance->block + INSTANCE_BLOCK_HEADER_SIZE +
		    (si->next * 4)));

  return result;
}

unsigned int article_id(unsigned int spec) {
  return spec & 0xfff;
}

unsigned int count_id(unsigned int spec) {
  return spec >> 28;
}

void wind_to_article_id(search_item *si, int aid) {
  int next = 0;

  while (TRUE) {
    next = article_id(next_instance(si));
    if (next == 0 || next >= aid)
      return;
    si->next++;
  }
  
}

void search_details(int article_id, int goodness, int index) {
  char block[ARTICLE_SIZE];
  char *b = block;
  search_result *sr = &search_results[index];
  int group_id;

  read_article(block, article_id);

  group_id = *((int*)b);
  b += 4;
  sr->article = *((int*)b);
  b += 4;
  sr->time = *((int*)b);
  b += 4;

  b = sstrcpy(sr->author, b);
  b = sstrcpy(sr->subject, b);
  b = sstrcpy(sr->body, b);

#if DEBUG
  printf("group_id: %x, article: %x\n", group_id, sr->article);
#endif

  sr->group = reverse_group_table[group_id];
  sr->goodness = goodness;
}

void print_search_results(search_result *sr, int nresults) {
  int i;

  for (i = 0; i<nresults; i++) {
    printf("%d\t%s\t%d\t%d\t%s\t%s\t%s\n",
	   sr->goodness,
	   sr->group,
	   sr->article,
	   (unsigned int)sr->time,
	   sr->author,
	   sr->subject,
	   sr->body);
    sr++;
  }
  
}

search_result *mdb_search(char **expressions) {
  int ne = 0, i;
  char *exp;
  word_descriptor *wd;
  search_item *si;
  int max_article_id, aid, sid;
  int matches, goodness;
  int ends = 0, nresults = 0;

  bzero(search_results, MAX_SEARCH_RESULTS * sizeof(search_result));
  for (i = 0; expressions[i]; i++) {
    exp = expressions[i];
    printf("Searching for %s\n", exp);
    si = &search_items[ne];
    if (*exp == '-') {
      si->negate_p = 1;
      si->word = exp + 1;
    } else {
      si->negate_p = 0;
      si->word = exp;
    }

    if ((wd = lookup_word(si->word)) != NULL) {
      si->word_id = wd->word_id;
      si->instance = get_instance_block(wd->head);
      printf("Found %s: %d\n", si->word, si->word_id);
    } else {
      si->instance = NULL;
    }
    si->next = 0;

    ne++;
  }

  if (ne == 0)
    return NULL;

  while (ends < ne) {
    max_article_id = 0;
    for (i = 0; i<ne; i++) {
      aid = article_id(next_instance(&search_items[i]));
      if (aid > max_article_id)
	max_article_id = aid;
    }

#if DEBUG
    printf("max_article_id: %d\n", max_article_id);
#endif

    for (i = 0; i<ne; i++) 
      wind_to_article_id(&search_items[i], max_article_id);

    matches = 0;
    goodness = 1;
    ends = 0;
    for (i = 0; i<ne; i++) {
      si = &search_items[i];
      sid = next_instance(si);
      aid = article_id(sid);
      if ((si->negate_p == 1 && aid != max_article_id) ||
	  (si->negate_p == 0 && aid == max_article_id)) {
	matches++;
	goodness *= count_id(sid);
	if (si->negate_p == 0)
	  si->next++;
      }
      if (aid == 0)
	ends++;
    }
    
    if (matches == ne && ends != ne) {
#if DEBUG      
      printf("Found a match: %d, goodness %d\n", max_article_id, goodness);
#endif
      search_details(max_article_id, goodness, nresults++);
    }
  }

  print_search_results(search_results, nresults);
  return search_results;
}
