#define _LARGEFILE64_SOURCE

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
#include "util.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#define DEBUG 0
#define INSTANCE_BLOCK_LENGTH 255
#define MAX_GROUPS 16000
#define MAX_INTERNAL_SEARCH_RESULTS 8192

void *word_table[WORD_SLOTS];
void *word_extension_table[WORD_EXTENSION_SLOTS];

static int instance_table[INSTANCE_TABLE_SIZE];
static int next_instance_blocks[INSTANCE_TABLE_SIZE];
static instance_block instance_buffer[INSTANCE_BUFFER_SIZE];
static int shutting_down_p = 0;
int instance_buffer_size = INSTANCE_BUFFER_SIZE;

static int num_word_extension_blocks = 1;
int current_instance_block_number = 1;
static int num_word_id = 1;
static int next_free_buffer = 1;
static int instance_file = 0;
static int word_extension_file = 0;
static int article_file = 0;
static int word_file = 0;
static unsigned int current_article_id = 1;
static int current_group_id = 1;
static GHashTable *group_table = NULL;
static char *reverse_group_table[MAX_GROUPS];
static int allocated_instance_buffers = 0;
static int gc_next = 0;
char *index_dir = INDEX_DIRECTORY;
int total_single_word_instances = 0;
static int total_word_instances = 0;
int ninstance_blocks_read = 0;
int is_indexing_p = 0;

time_t now_time;

#define article_id(id) ((id) & 0xfffffff)

#define count_id(id) (((unsigned int)id) >> 28)

#if 0
unsigned int article_id(unsigned int spec) {
  return spec & 0xffffff;
}

unsigned int count_id(unsigned int spec) {
  return spec >> 28;
}
#endif


char *index_file_name(char *name) {
  static char file_name[1024];
  strcpy(file_name, index_dir);
  strcat(file_name, "/");
  strcat(file_name, name);
  return file_name;
}

void error_out(void) {
  int i = 1;
  
  i--;
  i = 1/i;
  printf("%d", i);
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
  if (ftruncate64(instance_file,
		  (loff_t)(current_instance_block_number + 1) * BLOCK_SIZE)
      == -1) {
    merror("Increasing the instance file size");
  }
  return current_instance_block_number;
}

int allocate_chunking_instance_block(int ibn, int blocks) {
  int first = current_instance_block_number + 1;
  int *block = (int *)cmalloc(BLOCK_SIZE);

  if (lseek64(instance_file, (loff_t)0, SEEK_END) == -1) {
    merror("Seeking to the end of the instance file.");
  }

  while (blocks-- > 0) {
    current_instance_block_number++;
    if (blocks > 0) {
      *block = current_instance_block_number + 1;
      next_instance_blocks[current_instance_block_number] = 
	current_instance_block_number + 1;
    } else
      *block = 0;
    write_from(instance_file, (char *)block, BLOCK_SIZE);
  }

  free(block);
  return first;
}

/* Allocate a new, fresh word extension block. */
int allocate_extension_block(void) {
  if (ftruncate64(word_extension_file,
		  (loff_t)(num_word_extension_blocks + 1) * WORD_BLOCK_SIZE) == -1) 
    merror("Increasing the word extension file");
  
  return num_word_extension_blocks;
}

int sort_instance_buffer_less(const void *ib1, const void *ib2) {
  return ((instance_block*)ib1)->block_id - 
    ((instance_block*)ib2)->block_id;
}


/* This function goes through the instance buffers and tries to free
   10% of them.  It looks for buffers that haven't been used in 5
   seconds. */
void free_some_instance_buffers_1(int seconds) {
  int buffers_to_free = instance_buffer_size / 10;
  time_t now = time(NULL);
  instance_block *ib;
  int buffers_freed = 0;
  instance_block **ibs;
  int nibs = 0, i;

  ibs = malloc(buffers_to_free * sizeof(instance_block*));
  bzero(ibs, buffers_to_free * sizeof(instance_block*));

  printf("Freeing %d buffers\n", buffers_to_free);

  while (buffers_to_free > 0) {
    if (gc_next++ == instance_buffer_size - 1) {
      gc_next = 0;
      break;
    }

    ib = &instance_buffer[gc_next];
    /* We swap out blocks that haven't been used in a while. */
    if (ib->block_id != 0 &&
	/* Either this is a block with a next pointer, which means that
	   we don't need it any more... */
	((is_indexing_p && ib->num_used == INSTANCE_BLOCK_LENGTH) || 
	 /* Or it hasn't been used in a long time. */
	 ((now - ib->access_time) > seconds))) {
#if DEBUG
      printf("%d; %d is %d old\n", seconds, 
	     gc_next, (int)(now - ib->access_time));
#endif
      ibs[nibs++] = ib;
      buffers_to_free--;
      buffers_freed++;
    }
  }

  qsort(ibs, nibs, sizeof(instance_block*), sort_instance_buffer_less);

  for(i = 0; i < nibs; i++) 
    flush_instance_block(ibs[i]);

  free(ibs);

  printf("Freed %d buffers.\n", buffers_freed);

  /* This will kill interactive performance, but we want
     throughput. */
  fsync(instance_file);
}

/* Calls the freeing function until it finally frees something. */
void free_some_instance_buffers(void) {
  int aib = allocated_instance_buffers;
  int seconds = 60 * 60;

  free_some_instance_buffers_1(seconds);
  while (aib == allocated_instance_buffers) {
    printf("No buffers freed; sleeping...\n");
    sleep(1);
    seconds = seconds / 2;
    free_some_instance_buffers_1(seconds);
  }
}

/* Find a free in-memory instance buffer. */
int get_free_instance_buffer(void) {
  if (allocated_instance_buffers++ == instance_buffer_size - 2)
    free_some_instance_buffers();

  while (instance_buffer[next_free_buffer].block_id != 0) {
    if (next_free_buffer == instance_buffer_size - 2) {
      fprintf(stderr, "instance_buffer_size wraparound.\n");
      next_free_buffer = 0;
    }
    next_free_buffer++;
  }

#if DEBUG
  printf("Allocated buffer %d\n", next_free_buffer);
#endif
  return next_free_buffer;
}


/* Determine how many entries in an instance block are used. */
int instance_block_used_entries(char *b) {
  int n = 0;
  int *block = (int*)b;
  int length = BLOCK_SIZE / sizeof(int) - 1;

  /* Skip past the header. */
  block += 1;

  /* If the first byte of the instance is zero, then we have reached
     the end of the block.  We always extend the block if it's full,
     so there's no danger of segfaulting here without checking against
     INSTANCE_BLOCK_HEADER_SIZE. */

  while (block[n] && n < length) 
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
    ib->block = cmalloc(BLOCK_SIZE);

    if (ib->block == NULL) {
      perror("chow-indexer");
      error_out();
    }
  }

  ninstance_blocks_read++;
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
    if (next_instance_blocks[block_id]) {
      instance_block *ib = &instance_buffer[bn];
      if (ib->block == NULL) 
	ib->block = cmalloc(BLOCK_SIZE);
      /* We don't really need to switch anything in, because it's
	 an empty, chunked block. */
      bzero(ib->block, BLOCK_SIZE);
      *(int*)(ib->block) = next_instance_blocks[block_id];
      next_instance_blocks[block_id] = 0;
      ib->block_id = block_id;
      ib->dirty = 0;
      ib->num_used = 0;
    } else {
      swap_instance_block_in(bn, block_id);
    }
    instance_table[block_id] = bn;
  }

#if DEBUG
  printf("bn is now %d, block_id %d\n", bn, block_id);
#endif
  
  if (instance_buffer[bn].block == NULL) {
    printf("Got a zero block (bn: %d, block_id: %d).\n", bn, block_id);
    error_out();
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


/* malloc a new, fresh word block. */
char *allocate_word_block(const char *word) {
  int slot_number = hash(word);
  char *block = cmalloc(WORD_BLOCK_SIZE);

  if (block == NULL) {
    perror("chow-indexer");
    error_out();
  }

  word_table[slot_number] = block;
  return block;  
}


/* Given that we have found the (head of the) word block for a
   specific word, find the word descriptor for this word.  Note that
   this function isn't reentrant -- it returns a static
   word_descriptor.  The caller has to save the values itself if it
   wants to keep them. */
word_descriptor *block_search_word(const char *word, char *word_block) {
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
	  error_out();
	}
	b = word_block + BLOCK_HEADER_SIZE;
      } else 
	return NULL;
    }

    if (word_block == NULL) {
      printf("Went to a non-existing block here.\n");
      error_out();
    } 

    while ((*b != 0) && (*b++ == *w++))
      ;

    if ((! *b) && (! *w)) {
      b++;
      /* We've found the word. */
      dword.word = word;
      dword.word_id = *((int*)b);
      b += 4;
      dword.head = ((int*)b);
      b += 4;
      dword.tail = ((int*)b);
      b += 4;
      dword.w_block = word_block;
#if DEBUG
      printf("Found %s, %d, %d, %x\n",
	     dword.word, dword.word_id, (int)&dword.head, (int)&dword.tail);
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


/* Find the word block for a specified word.  */
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

/* Mark this word block as clean. */
void clean_block(char *block) {
  *(block+6) = 0;
}


/* Say whether this word block is dirty. */
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
  //int instance_block;

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
       strlen(word) + 3*sizeof(int) + 1 + 1) > WORD_BLOCK_SIZE) {
    /* There's no room in this block, so we add a new block. */
    /* printf("Allocating an extension block for %s: %d\n",
       word, num_word_extension_blocks); */
    
    new_block = cmalloc(WORD_BLOCK_SIZE);
    
    if (new_block == NULL) {
      perror("chow-indexer");
      error_out();
    }

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
  *((int*) b) = 0;
  b += 4;
  *((int*) b) = 0;
  b += 4;

  set_last_word((short*) word_block, b - word_block - BLOCK_HEADER_SIZE);
  dirty_block(word_block);

#if DEBUG
  dump_word_block(word_block);
#endif
 
  return lookup_word(word);
}


/* Enter a word instance into the instance table. */
void enter_instance(unsigned int article_id, word_descriptor *wd,
		    unsigned int count) {
  instance_block *ib;
  char *block;
  unsigned int tmp = article_id;
  int new_instance_block;
  unsigned int prev;
  int nib;

  /* The idea here is that we don't allocate instance blocks for words
     unless we have two instances of the word.  For the first
     instance, we cheat by letting *wd->head be zero, and has the
     article_id/count in the tail. */
  if (*wd->head) {
    /* Normal, blocked-out instance. */
    ib = get_instance_block(*wd->tail);
    block = ib->block;
  } else if (*wd->tail) {
    /* We had a zero head, but a non-zero tail, so this instance is
       the second instance of this word.  We need to allocate an
       instance block, put the first instance into the block, and then
       put this instance into the block. */
    prev = *wd->tail;
    nib = allocate_instance_block();
    *wd->head = nib;
    *wd->tail = nib;
    enter_instance(article_id(prev), wd, count_id(prev));
    ib = get_instance_block(*wd->tail);
    block = ib->block;
    total_single_word_instances--;
  } else {
    /* This is the first instance of the word.  We just put it in the
       tail pointer. */
    total_single_word_instances++;
    tmp |= (count << 28);
    *wd->tail = tmp;
    return;
  }

  while (ib->num_used == INSTANCE_BLOCK_LENGTH) {
    /* printf("Going to block %d\n", *(int*)(ib->block)); */ 
    ib = get_instance_block(*(int*)(ib->block));
  }

  /* Go to the end of the block. */
  block += INSTANCE_BLOCK_HEADER_SIZE;
  block += ib->num_used * 4;

#if DEBUG
  printf("Skipping %d, %d\n",
	 ib->num_used * 4 + INSTANCE_BLOCK_HEADER_SIZE, ib->num_used);
#endif

  /* Enter the data. */
  tmp |= (count << 28);
  *((int*) block) = tmp;
  block += 4;

#if DEBUG
  printf("Instanced %d:%d into %d\n", article_id, count, ib->block_id);
#endif

  /* Do accounting. */
  ib->num_used++;
  ib->dirty = 1;
  ib->access_time = now_time;

  /* printf("ib->num_used: %d\n", ib->num_used); */

  /* If we've now filled this block, we allocate a new one, and hook
     it onto the end of this chain. */
  if (ib->num_used == INSTANCE_BLOCK_LENGTH) {
    block = ib->block;

    if (0) {
      new_instance_block = allocate_instance_block();
      *((int*) block) = new_instance_block;
      *wd->tail = new_instance_block;
    } else {
      if (*((int*) block) != 0) {
	/* We have more pre-allocated blocks in this chunk, so we just
	   set the tail to point to the next block in the chunk. */
	//printf("%s to preallocated block\n", wd->word);
	*wd->tail = *((int*) block);
      } else {
	/* No more blocks in this chunk, so we allocate a new chunk and
	   update the tail pointer to point to the first block in this
	   new chunk. */
	//printf("%s allocating new chunk\n", wd->word);
	new_instance_block =
	  allocate_chunking_instance_block(*wd->tail, 128);
	*((int*) block) = new_instance_block;
	*wd->tail = new_instance_block;
      }
    }
    dirty_block(wd->w_block);
    flush_instance_block(ib);
  }
}


void mdb_init(void) {
  if ((instance_file = open64(index_file_name(INSTANCE_FILE),
			      O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the instance file");

  /*
  if (ftruncate64(instance_file,
		  (loff_t)3000000 * BLOCK_SIZE) == -1) {
    merror("Increasing the instance file size");
  }
  */

  if ((article_file = open64(index_file_name(ARTICLE_FILE),
			   O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the article file");

  if ((word_extension_file =
       open(index_file_name(WORD_EXTENSION_FILE), O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the word extension file");

  if ((word_file = open(index_file_name(WORD_FILE), 
			O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the word file");

  if (ftruncate64(word_file, (loff_t)WORD_SLOTS * WORD_BLOCK_SIZE) == -1) 
    merror("Truncating the word file");

  group_table = g_hash_table_new(g_str_hash, g_str_equal);
  bzero(word_table, WORD_SLOTS * 4);

  bzero(next_instance_blocks, INSTANCE_TABLE_SIZE * sizeof(int));
  
  read_next_article_id();
  read_group_table();
  read_word_table();
  read_word_extension_table();
  read_next_instance_block_number();
}

/* Print a status report. */
void mdb_report(void) {
  printf("Total number of extension blocks allocated: %d\n",
	 num_word_extension_blocks);
}


/* Look up a group in the group hash table and return its group_id. */
int group_id(char *group) {
  int gid;
  char *g;
  
  if ((gid = (int)g_hash_table_lookup(group_table, (gpointer)group)) == 0) {
    current_group_id++;
    gid = current_group_id;
    g = cmalloc(strlen(group)+1);
    strcpy(g, group);
    g_hash_table_insert(group_table, (gpointer)g, (gpointer)gid);
    reverse_group_table[gid] = g;
  }
  
  return gid;
}


/* Write the document to the article file.  The article file has one
   block per article, and contains stuff like the group id, the
   subject, the author, and so on, which is useful when displaying
   search results. */
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

#if DEBUG
  printf("Length of article: %d\n", b-abuf);
  printf("Length of body: %d\n", strlen(doc->body));
#endif
  
  current_article_id++;
  lseek64(article_file, (loff_t)current_article_id * ARTICLE_SIZE, SEEK_SET);
  write_from(article_file, abuf, ARTICLE_SIZE);
  return current_article_id;
}


/* Read the article block for article_id from the article file. */
void read_article(char *block, int article_id) {
  if (lseek64(article_file, (loff_t)article_id * ARTICLE_SIZE, SEEK_SET) == -1)
    merror("Reading an article from the article file");
  
  read_block(article_file, block, ARTICLE_SIZE);
}


/* Flush the group table to disk. */
void flush_group_entry(gpointer key, gpointer value, gpointer user_data) {
  FILE *fp = (FILE *) user_data;
  char *group = (char*) key;
  int group_id = (int) value;

  fprintf(fp, "%s %d\n", group, group_id);
}

void flush_group_table(void) {
  FILE *fp;
  if ((fp = fopen(index_file_name(GROUP_FILE), "w")) == NULL)
    merror("Opening the group file");

  g_hash_table_foreach(group_table, flush_group_entry, (gpointer)fp);

  fclose(fp);
}


/* Flush the instance table to disk. */
void flush_instance_block(instance_block *ib) {
#if DEBUG
  printf("Flushing instance block %d\n", ib->block_id);
#endif

  if (ib->dirty) {
    if (lseek64(instance_file,
		(loff_t)ib->block_id * BLOCK_SIZE, SEEK_SET) == -1) 
      merror("Flushing an instance block");
    write_from(instance_file, ib->block, BLOCK_SIZE);
  }

  ib->dirty = 0;
  instance_table[ib->block_id] = 0;
  ib->block_id = 0;
  ib->num_used = 0;
  ib->access_time = 0;
  bzero(ib->block, BLOCK_SIZE);
  allocated_instance_buffers--;
  if (shutting_down_p) 
    free(ib->block);
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


/* Flush the word table to disk. */
void flush_word_block(char *block, int block_number) {
  if (lseek64(word_file, (loff_t)block_number * WORD_BLOCK_SIZE, SEEK_SET) == -1) 
    merror("Flushing a word block");

  clean_block(block);
  write_from(word_file, block, WORD_BLOCK_SIZE);
  if (shutting_down_p)
    free(block);
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


/* Flush the extension word table to disk. */
void flush_word_extension_block(char *block, int block_number) {
  if (lseek64(word_extension_file, (loff_t)block_number * WORD_BLOCK_SIZE, SEEK_SET) == -1) 
    merror("Flushing a word extension block");

  write_from(word_extension_file, block, WORD_BLOCK_SIZE);
  clean_block(block);
  if (shutting_down_p)
    free(block);
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


/* Flush everything to disk. */
void soft_flush(void) {
  printf("Flushing tables...");
  flush_word_table();
  flush_word_extension_table();
  flush_instance_table();
  flush_group_table();
  printf("done.\n");
}

void flush(void) {
  shutting_down_p = 1;
  soft_flush();
}


/* Read the group table from disk into the relevant tables. */
void read_group_table(void) {
  FILE *fp;
  char group[MAX_GROUP_NAME_LENGTH], *g;
  int group_id;
  
  if ((fp = fopen(index_file_name(GROUP_FILE), "r")) == NULL)
    return;

  while (fscanf(fp, "%s %d\n", group, &group_id) != EOF) {
    g = cmalloc(strlen(group)+1);
    strcpy(g, group);
#if DEBUG
    printf("Got %s (%d)\n", g, group_id);
#endif
    g_hash_table_insert(group_table, (gpointer)g, (gpointer)group_id);
    reverse_group_table[group_id] = g;

    if (group_id > current_group_id)
      current_group_id = group_id;

  }
  fclose(fp);
}


/* Find out what the next article_id is supposed to be by looking at
   the size of the article file. */
void read_next_article_id(void) {
  loff_t size = file_size(article_file);
  
  if ((size % ARTICLE_SIZE) != 0) {
    printf("Invalid size for the article file (%Ld) for this article size (%d).\n",
	   size, ARTICLE_SIZE);
    error_out();
  }

  current_article_id = (unsigned int)(size / ARTICLE_SIZE);
}


/* Find out what the next instance block id is by looking at the size
   of the instance file. */
void read_next_instance_block_number(void) {
  loff_t size = file_size(instance_file);
  
  if ((size % BLOCK_SIZE) != 0) {
    printf("Invalid size for the instance file (%Ld) for this block size (%d).\n",
	   size, BLOCK_SIZE);
    error_out();
  }

  current_instance_block_number = (int)(size / BLOCK_SIZE);
}


/* Read the word file into the word table. */
void read_word_table(void) {
  char *block = NULL;
  int i;
    
  lseek64(word_file, (loff_t)0, SEEK_SET);

  for (i = 0; i<WORD_SLOTS; i++) {
    if (block == NULL)
      block = cmalloc(WORD_BLOCK_SIZE);

    read_block(word_file, block, WORD_BLOCK_SIZE);
    if (get_last_word((short*)block)) {
      /* This is a non-empty word block, so we store it in the
         table. */
      word_table[i] = block;
      block = NULL;
    } else
      word_table[i] = NULL;

  }

  if (block)
    free(block);
}


/* Read the extension word file into the extension word table. */
void read_word_extension_table(void) {
  char *block = NULL;
  int i;
  loff_t size = file_size(word_extension_file);
  
  if ((size % WORD_BLOCK_SIZE) != 0) {
    printf("Invalid size for the word extension file (%Ld) for this block size (%d).\n",
	   size, WORD_BLOCK_SIZE);
    error_out();
  }
  num_word_extension_blocks = (int)(size / WORD_BLOCK_SIZE);

#if DEBUG
  printf("Number of word extension blocks: %d\n", num_word_extension_blocks);
#endif
  
  lseek64(word_extension_file, (loff_t)0, SEEK_SET);
  for (i = 0; i<num_word_extension_blocks; i++) {
    block = cmalloc(WORD_BLOCK_SIZE);
    read_block(word_extension_file, block, WORD_BLOCK_SIZE);
    word_extension_table[i] = block;
  }

  num_word_extension_blocks++;
}


/* Searching the database. */

typedef struct {
  char *word;
  int negate_p;
  int word_id;
  instance_block *instance;
  int sarticle_id;
  int next;
} search_item;

typedef struct {
  int article_id;
  int goodness;
} isearch_result;

static search_item search_items[MAX_SEARCH_ITEMS];
static search_result search_results[MAX_SEARCH_RESULTS];
static isearch_result isearch_results[MAX_INTERNAL_SEARCH_RESULTS];

/* Function for debugging instance blocks. */
void dump_instances(instance_block *ib) {
  int i;
  int *block;
  int next_block;
  int total = 0;
  
  while (TRUE) {
    block = (int*) ib->block;
    next_block = *block++;
    for (i = 0; i<INSTANCE_BLOCK_LENGTH; i++) {
      if (block[i]) {
	printf("%x ", block[i]);
	total++;
      }
    }
#if 1
    printf("\n");
    printf("num_used: %d\n", ib->num_used);
#endif
    if (next_block)
      ib = get_instance_block(next_block);
    else {
      if (total == 1)
	total_single_word_instances++;
      total_word_instances++;
#if DEBUG
      printf("Total instances: %d\n", total);
#endif
      return;
    }
  }
}


/* Return the next instance in a word instance chain. */
unsigned int next_instance(search_item *si) {
  unsigned int result;

  if (!si->instance) {
    result = si->sarticle_id;
    si->sarticle_id = 0;
    return result;
  }

  /* INSTANCE_BLOCK_LENGTH */
  if (si->next == INSTANCE_BLOCK_LENGTH) {
#if DEBUG
    printf("Going to next block.\n");
#endif
    si->instance = get_instance_block(*((int*)(si->instance->block)));
    si->next = 0;
  }

  if (si->next > si->instance->num_used) {
#if DEBUG
    printf("next is %d, num used is %d.\n", si->next, si->instance->num_used);
#endif
    return 0;
  }

  result = *((int*)(si->instance->block + INSTANCE_BLOCK_HEADER_SIZE +
		    (si->next * 4)));

  return result;
}

/* Skip past all instances that have an article id less than the
   specified one. */
void wind_to_article_id(search_item *si, int aid) {
  int next = 0;

  while (TRUE) {
    next = article_id(next_instance(si));
    if (next == 0 || next >= aid)
      return;
    si->next++;
  }
  
}


/* Fill in the details from the search -- subject, author, etc. */
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


/* Output the search results. */
void print_search_results(search_result *sr, int nresults, FILE *fdp) {
  int i;

  for (i = 0; i<nresults; i++) {
    fprintf(fdp, "%d\t%s\t%d\t%d\t%s\t%s\t%s\n",
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


/* Sort the results based on the goodness of the results.  The
   goodness is computed as a product of how many times each specified
   word appeared in the articles. */
int result_less(const void *sr1, const void *sr2) {
  return ((search_result*)sr2)->goodness -
    ((search_result*)sr1)->goodness;
}

void sort_search_results(search_result *sr, int nresults) {
  qsort(sr, nresults, sizeof(search_result), result_less);
}


/* Search the preliminary search results according to goodness.  This
   is used when the number of search results is higher than the number
   we want to return to the user. */
int iresult_less(const void *sr1, const void *sr2) {
  return ((isearch_result*)sr2)->goodness -
    ((isearch_result*)sr1)->goodness;
}

int sort_isearch_results(isearch_result *isr, int nresults) {
  qsort(isr, nresults, sizeof(isearch_result), iresult_less);
  if (nresults < MAX_SEARCH_RESULTS)
    return nresults;
  else
    return MAX_SEARCH_RESULTS;
}

/* Reshuffle the initial results so that the last ones become the
   first ones. */
int rearrange_isearch_results(isearch_result *isr, int nresults, int wrapped) {
  int n = nresults - 1;
  int total_nresults = nresults + wrapped*MAX_INTERNAL_SEARCH_RESULTS;
  int todo = (total_nresults > MAX_SEARCH_RESULTS? MAX_SEARCH_RESULTS: 
	      nresults);
  int done = 0;
  isearch_result dummy;

  while (done < todo) {
    /* Switch places. */
    dummy.article_id = isr[done].article_id;
    dummy.goodness = isr[done].goodness;

    isr[done].article_id = isr[n].article_id;
    isr[done].goodness = isr[n].goodness;

    isr[n].article_id = dummy.article_id;
    isr[n].goodness = dummy.goodness;

    if (n == 0) {
      n = MAX_INTERNAL_SEARCH_RESULTS;
    }
    n--;

    done++;
  }

  return done;
}


/* The main search function.  It takes a list of words to search
   for. */
search_result *mdb_search(char **expressions, FILE *fdp, int *nres) {
  int ne = 0, i;
  char *exp;
  word_descriptor *wd;
  search_item *si;
  int max_article_id, aid, sid;
  int matches, goodness;
  int ends = 0, nresults = 0;
  int ended = 0;
  int positives = 0;
  int wrapped = 0;
  int total_nresults = 0;
  char *stop;

  fprintf(fdp, "# Articles: %d\n", current_article_id);

  print_stop();
  bzero(search_results, MAX_SEARCH_RESULTS * sizeof(search_result));
  for (i = 0; expressions[i]; i++) {
    exp = expressions[i];
#if DEBUG
    printf("Searching for %s\n", exp);
#endif
    si = &search_items[ne];
    if (*exp == '-') {
      si->negate_p = 1;
      si->word = exp + 1;
    } else {
      si->negate_p = 0;
      si->word = exp;
    }

  print_stop();
    if (stop_word_p(si->word)) {
      fprintf(fdp, "# Ignoring: %s\n", si->word);
    } else if (strlen(si->word) < MIN_WORD_LENGTH) {
      fprintf(fdp, "# Short: %s\n", si->word);
    } else if (strlen(si->word) > MAX_WORD_LENGTH &&
	       !strchr(si->word, '.')) {
      fprintf(fdp, "# Long: %s\n", si->word);
    } else {
      if ((wd = lookup_word(si->word)) != NULL) {
	si->word_id = wd->word_id;
	if (*wd->head) {
	  si->instance = get_instance_block(*wd->head);
	  swap_in_instance_blocks(wd);
	} else
	  si->sarticle_id = *wd->tail;
#if DEBUG
	printf("Found %s: id %d\n", si->word, si->word_id);
#endif
      } else {
	si->instance = NULL;
      }
      si->next = 0;
      
      ne++;
      if (si->negate_p == 0)
	positives++;
    }
  }

  if (positives == 0)
    return NULL;

  stop = print_stop();
  while (! ended) {
    max_article_id = 0;
    if (stop != print_stop()) {
      printf("Error now: %x\n", print_stop());
      error_out();
    }
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
      //printf("Article id %d\n", aid);
      if ((si->negate_p == 1 && aid != max_article_id) ||
	  (si->negate_p == 0 && aid == max_article_id)) {
	matches++;
	goodness *= count_id(sid) + 1;
	if (si->negate_p == 0)
	  si->next++;
      }
      if (aid == 0 && si->negate_p == 0) {
	ended = 1;
	ends++;
      }
    }
    
    if (matches == ne && ends != ne) {
#if DEBUG
      printf("Found a match: %d, %d, goodness %d\n", 
	     nresults, max_article_id, goodness);
#endif
      isearch_results[nresults].article_id = max_article_id;
      isearch_results[nresults].goodness = goodness;

      total_nresults++;

      if (++nresults >= MAX_INTERNAL_SEARCH_RESULTS) {
	wrapped++;
	nresults = 0;
	/*
	  fprintf(fdp, "# Internal-max: %d\n", MAX_INTERNAL_SEARCH_RESULTS);
	  break;
	*/
      }
    }
  }

  print_stop();
  fprintf(fdp, "# Internal: %d\n", total_nresults);

  if (total_nresults >= MAX_SEARCH_RESULTS) {
    //fprintf(fdp, "# Max: %d\n", MAX_SEARCH_RESULTS);
    //nresults = sort_isearch_results(isearch_results, nresults);
    nresults = rearrange_isearch_results(isearch_results, nresults, wrapped);
  }

  for (i = 0; i<nresults; i++) {
    search_details(isearch_results[i].article_id, 
		   isearch_results[i].goodness, i);
  }

  print_stop();
#if DEBUG
  printf("Total results: %d\n", total_nresults);
#endif
  fprintf(fdp, "# Results: %d\n", nresults);
  sort_search_results(search_results, nresults);
  *nres = nresults;
  return search_results;
}


void block_dump_word(const char *word_block) {
  const char *b = word_block + BLOCK_HEADER_SIZE; /* Skip past the header. */
  int head;

  while (TRUE) {
    if (*b == 0) {
      /* We have reached the end of the block; check whether it's the
	 last block. */
      if (*((int*)word_block) != 0) {
	/* printf("Going to the next block, %d.\n", *((int*)word_block)); */
	word_block = word_extension_table[*((int*)word_block)];
	if (word_block == NULL) {
	  printf("Went to a non-existing block.\n");
	  error_out();
	}
	b = word_block + BLOCK_HEADER_SIZE;
      } else 
	return;
    }

    if (word_block == NULL) {
      printf("Went to a non-existing block here.\n");
      error_out();
    } 

    while (*b++)
      ;

    printf("Word: %s\n", b);

    b += 4;
    head = *((int*)b);
    b += 4;
    b += 4;
    dump_instances(get_instance_block(head));
  }
}


void dump_statistics(void) {
  int i;
  char *block;

  for (i = 0; i<WORD_SLOTS; i++) {
    if ((block = (char*)word_table[i]) != NULL) {
      block_dump_word(block);
    }
  }
  printf("Total words: %d, total single-instance words: %d\n", 
	 total_word_instances,
	 total_single_word_instances);

}


/* Swap in all instance blocks for a word. */
int swap_in_instance_blocks(word_descriptor *wd) {
  instance_block *ib = get_instance_block(*wd->head);
  int nblocks = 0;
  int next;

  while (ib != NULL) {
    nblocks++;
    if ((next = *(int*)(ib->block)) != 0) {
      ib = get_instance_block(next);
    } else {
      ib = NULL;
    }
  }

  return nblocks;
}


