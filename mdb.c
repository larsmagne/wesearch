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

#define DEBUG 0

static void *word_table[WORD_SLOTS];
static void *word_extension_table[WORD_EXTENSION_SLOTS];

static int instance_table[INSTANCE_TABLE_SIZE];
static instance_block instance_buffer[INSTANCE_BUFFER_SIZE];

static int num_word_extension_blocks = 1;
static int current_instance_block_number = 1;
static int num_word_id = 1;
static int next_free_buffer = 1;
static instance_file = 0;


void merror(void) {
  perror("mainsearch");
  exit(1);
}

/* A function used when debugging.  It dumps the contents of the
   specified word block. */
void dump_word_block(char *block) {
  printf("Address: %x\n", block);
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
    merror();
  }
  return current_instance_block_number;
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

/* Read a block from a file at a specified offset into a in-memory
   block. */
void read_into(int fd, int block_id, char *block, int block_size) {
  int rn = 0, ret;
  
  if (lseek(fd, block_id * block_size, SEEK_SET) == -1) {
    merror();
  }

  while (rn < block_size) {
    ret = read(fd, block + rn, block_size - rn);
    if (ret == 0) {
      fprintf(stderr, "Reached end of file (block_id: %d, block_size: %d).\n",
	      block_id, block_size);
      exit(1);
    } else if (ret == -1) 
      merror();
      
    rn += ret;
  }
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

  while (block[n*6]) 
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
int one_at_a_time_hash(const char *key, int len)
{
  int   hash, i;
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
  //hash8(word, length);
  one_at_a_time_hash(word, length);
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
  int word_id, head, tail;
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
      printf("Found %s, %d, %d, %d\n",
	     dword.word, dword.word_id, dword.head, &dword.tail);
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
  int slot_number = hash(word);
#if DEBUG
  printf("Slot number %d.\n", slot_number);
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

/* Get the position of the end of the entries in the word block. */
int get_last_word(short *block) {
  return *(block+2);
}

/* Set the position of the end of the entries in the word block. */
int set_last_word(short *block, short last) {
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

    word_extension_table[num_word_extension_blocks] = new_block;
    *((int*)word_block) = num_word_extension_blocks++;

    dirty_block(word_block);
    
    word_block = new_block;
    last_word = 0;
  }

  /* We now know we have room to write the word to the block. */
  b = word_block + BLOCK_HEADER_SIZE + last_word;
  
  while (*b++ = *w++)
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
void enter_instance(word_descriptor *wd, unsigned int count, int group_id) {
  instance_block *ib = get_instance_block(*wd->tail);
  char *block = ib->block;
  int num_used = ib->num_used;
  unsigned int tmp = wd->word_id;
  int new_instance_block;

  /* Go to the end of the block. */
  block += INSTANCE_BLOCK_HEADER_SIZE;
  block += num_used * 6;

#if DEBUG
  printf("Skipping %d, %d\n",
	 num_used * 6 + INSTANCE_BLOCK_HEADER_SIZE, num_used);
#endif

  /* Enter the data. */
  tmp &= (count << 28);
  *((int*) block) = tmp;
  block += 4;
  *((short*) block) = group_id;
  block += 2;

  /* Do accounting. */
  num_used++;
  ib->num_used = num_used;
  ib->dirty = 1;

  /* If we've now filled this block, we allocate a new one, and hook
     it onto the end of this chain. */
  if (num_used++ == 170) {
    new_instance_block = allocate_instance_block();
    block = ib->block;
    *((int*) block) = new_instance_block;
    *wd->tail = new_instance_block;
  }
}


void mdb_init(void) {
  if ((instance_file = open(INSTANCE_FILE, O_RDWR|O_CREAT, 0644)) == -1)
    merror();
}

void mdb_report(void) {
  printf("Total number of extension blocks allocated: %d\n",
	 num_word_extension_blocks);
}
