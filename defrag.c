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
  int i;
  char *block;
  int nwords = 0;

  new_instance_block_number = 1;

  if ((new_instance_file = open64("/opt/tmp/instances.db.tmp",
				  O_RDWR|O_CREAT|O_TRUNC, 0644)) == -1)
    merror("Opening the instance file");

  /* Write one dummy block first, since zero is an invalid instance
     array index. */
  block = cmalloc(BLOCK_SIZE);
  write_from(new_instance_file, block, BLOCK_SIZE);
  free(block);

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

