#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"
#include "config.h"
#include "mdb.h"
#include "util.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__FreeBSD__)
#  define INSTANCES_DB_TMP "/var/tmp/instances.db.tmp"
#else
#  define INSTANCES_DB_TMP "/mirror/tmp/instances.db.tmp"
#endif

/* De-fragment the instance table. */

static int new_instance_block_number = 1;
static int new_instance_file = 0;
static int nnew_blocks_written = 0;

void defragment_word(word_descriptor *wd) {
  instance_block *ib = get_instance_block(*wd->head);
  int next;
  int nblocks = 0;

  *wd->head = new_instance_block_number;

  while (ib != NULL) {
    nblocks++;
    *wd->tail = new_instance_block_number;
    new_instance_block_number++;
    if ((next = *(int*)(ib->block)) != 0) {
      *(int*)(ib->block) = new_instance_block_number;
      nnew_blocks_written++;
      write_from(new_instance_file, ib->block, BLOCK_SIZE);
      ib = get_instance_block(next);
    } else {
      nnew_blocks_written++;
      write_from(new_instance_file, ib->block, BLOCK_SIZE);
      ib = NULL;
    }
  }

  if (nblocks != 1)
    printf("Wrote %d blocks for %s\n", nblocks, wd->word);
}

int defragment_word_block(char *word_block) {
  const char *b = word_block + BLOCK_HEADER_SIZE; /* Skip past the header. */
  const char *word;
  static word_descriptor dword;
  int nwords = 0;

  dirty_block(word_block);

  while (TRUE) {
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
	dirty_block(word_block);
	b = word_block + BLOCK_HEADER_SIZE;
      } else 
	return nwords;
    }

    if (word_block == NULL) {
      printf("Went to a non-existing block here.\n");
      exit(1);
    } 

    nwords++;
    word = b;
    while (*b++)
      ;

    dword.word = word;
    dword.word_id = *((int*)b);
    b += 4;
    dword.head = ((int*)b);
    b += 4;
    dword.tail = ((int*)b);
    b += 4;
    if (*dword.head)
      defragment_word(&dword);
  }
}


void defragment_instance_table(void) {
  int i = 0;
  char *block;
  int nwords = 0;

  new_instance_block_number = 1;

  if ((new_instance_file = open64(INSTANCES_DB_TMP,
				  O_RDWR|O_CREAT|O_TRUNC, 0644)) == -1)
    merror("Opening the instance file");

  /* Write one dummy block first, since zero is an invalid instance
     array index. */
  block = cmalloc(BLOCK_SIZE);
  write_from(new_instance_file, block, BLOCK_SIZE);
  free(block);

  /*
  printf("Priming cache...\n");
  while (id < current_instance_block_number && i++ < 1024*512)
    get_instance_block(id++);
  printf("done\n");
  */

  for (i = 0; i<WORD_SLOTS; i++) {
    if ((block = (char*)word_table[i]) != NULL) {
      nwords += defragment_word_block(block);
      if (! (nwords % 100))
	printf("Total words: %d, read: %d, written: %d\n", nwords,
	       ninstance_blocks_read, nnew_blocks_written);
    }
  }
  printf("Total words: %d\n", nwords);

  flush();

}


/* The following functions are the beginning of a reimplementation
   of the entire thing, but they aren't used yet. */

typedef struct {
  int new_id;
  int orig_next;
  int new_next;
  int current_id;
} instance_order;

instance_order *find_instance_order(void) {
  instance_order *instance_chain;
  char *buffer = NULL;
  int nblocks = 1024;
  int i = 0;
  int next;
  int instance_file;

  if ((instance_file = open64(index_file_name(INSTANCE_FILE),
			      O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the instance file");

  instance_chain = (instance_order*)cmalloc(current_instance_block_number
					    * sizeof(instance_order));
  buffer = cmalloc(nblocks * BLOCK_SIZE);

  while (i < current_instance_block_number) {
    if (! (i % nblocks)) 
      read_block(instance_file, buffer, nblocks * BLOCK_SIZE);

    next = (int)(buffer[(i % nblocks) * BLOCK_SIZE]);
    if (next != 0)
      instance_chain[i].orig_next = next;
    i++;
  }

  return instance_chain;
}

void old_sort_instance_chain(instance_order *instance_chain) {
  int i;
  int new_id = 0;
  instance_order *io;

  for (i = 0; i<current_instance_block_number; i++) {
    io = instance_chain+i;
    if (io->new_id == 0) {
      do {
	io->new_id = new_id++;
	if (io->orig_next != 0) {
	  io->new_next = new_id;
	  io = instance_chain + io->orig_next;
	}
      } while (io->orig_next != 0);
    }
  }
}



typedef struct {
  char *word_entry;
  int next;
} instance_chain;

typedef struct {
  int id;
  int location;
} instance_sort;

int supposed_to_be[INSTANCE_TABLE_SIZE];
instance_sort where_it_is[INSTANCE_TABLE_SIZE];
instance_chain ic[INSTANCE_TABLE_SIZE];

void read_instance_chain(void) {
  char *buffer = NULL;
  int nblocks = 1024;
  int i = 0;
  int next;
  int instance_file;

  if ((instance_file = open64(index_file_name(INSTANCE_FILE),
			      O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the instance file");

  buffer = cmalloc(nblocks * BLOCK_SIZE);

  while (i < current_instance_block_number) {
    if (! (i % nblocks)) 
      read_block(instance_file, buffer, nblocks * BLOCK_SIZE);

    next = (int)(buffer[(i % nblocks) * BLOCK_SIZE]);
    if (next != 0)
      ic[i].next = next;
    i++;
  }

  close(instance_file);
}

void fill_instance_chain_with_word_block(char *word_block) {
  char *b = word_block + BLOCK_HEADER_SIZE; /* Skip past the header. */
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
	  exit(1);
	}
	b = word_block + BLOCK_HEADER_SIZE;
      } else 
	return;
    }

    if (word_block == NULL) {
      printf("Went to a non-existing block here.\n");
      exit(1);
    } 

    while (*b++)
      ;

    printf("Word: %s\n", b);

    b += 4;
    head = *((int*)b);
    ic[head].word_entry = b;
    b += 4;
    b += 4;
  }
}

void fill_instance_chain_with_word_entries(void) {
  int i;
  char *block;

  for (i = 0; i<WORD_SLOTS; i++) {
    if ((block = (char*)word_table[i]) != NULL) {
      fill_instance_chain_with_word_block(block);
    }
  }
}

void sort_instance_chain (void) {
  int search = 1, next;
  int i;

  bzero((char*)supposed_to_be, sizeof(int)*INSTANCE_TABLE_SIZE);

  for (i = 1; i<current_instance_block_number; i++) {
    next = search + 1;
    if (! supposed_to_be[search]) {
      supposed_to_be[i] = search;
      while (ic[search].next) 
	search = ic[search].next;
    } else {
      search++;
    }
  }

  /* Initialize where_it_is as well. */
  for (i = 0; i<current_instance_block_number; i++) {
    where_it_is[i].id = i;
  }  
}

int sort_buffer_less(const void *sr1, const void *sr2) {
  return (supposed_to_be[((instance_sort*)sr1)->id] - 
	  supposed_to_be[((instance_sort*)sr2)->id]);
}

void sort_buffer(int length, int start_id) {
  int i;

  for (i = 0; i<length; i++) 
    where_it_is[start_id + i].location = i;

  qsort(where_it_is + start_id, sizeof(instance_sort), length, sort_buffer_less);
}

void save_sorted(int fd, char* buffer, int length, int start_id) {
  int i;

  for (i = 0; i<length; i++) 
    write_from(fd, buffer + where_it_is[start_id + i].location * BLOCK_SIZE, BLOCK_SIZE);
}

void move_sorted(char* buffer, int length, int start_id, int to_start_id) {
  int i;

  for (i = 0; i<length; i++) 
    memcpy(buffer + i * BLOCK_SIZE,
	   buffer + where_it_is[start_id + i].location * BLOCK_SIZE, BLOCK_SIZE);

  memcpy(where_it_is + to_start_id, where_it_is + start_id, 
	 length * sizeof(instance_sort));
}

void one_sort_pass (char *input_file_name, char *output_file_name) {
  int input, output;
  char *buffer;
  int half = 512;
  int read, rest;
  int start_id = 0;
  int length;

  if ((input = open64(input_file_name, O_RDONLY)) == -1)
    merror("Opening the instance file");

  if ((output = open64(output_file_name, O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the instance file");

  buffer = cmalloc(half * BLOCK_SIZE * 2);
  read_block(input, buffer, half * BLOCK_SIZE);
  read = half;

  do {
    rest = min(read + half, current_instance_block_number);
    length = rest-read;
    read_block(input, buffer + half * BLOCK_SIZE, length * BLOCK_SIZE);
    sort_buffer(half + length, start_id);
    save_sorted(output, buffer, half, start_id);
    move_sorted(buffer, length, start_id + half, start_id);
    start_id += half;
  } while (read == current_instance_block_number);

  save_sorted(output, buffer + half * BLOCK_SIZE, length, start_id + half);

  close(input);
  close(output);
}

void defragment_sort (void) {
  char *input = index_file_name(INSTANCE_FILE);
  char *output1 = "/opt/tmp/instances1.tmp";
  char *output2 = "/opt/tmp/instances2.tmp";
  char *output = output1;
  int i;

  for (i = 0; i<7; i++) {
    one_sort_pass(input, output);
    if (i % 2) {
      input = output2;
      output = output1;
    } else {
      input = output1;
      output = output2;
    }
  }
}
