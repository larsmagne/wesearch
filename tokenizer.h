#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <gmime/gmime.h>
#include <stdio.h>
#include <string.h>

/* Ignore words longer than this */
#define MAX_WORD_LENGTH 24

/* Ignore words shorter than this */
#define MIN_WORD_LENGTH 3

/* This determines which characters are seen as text characters.  It
   basically includes all letters in Latin-1, as well as the
   digits. */
#define TEXT_CHARACTERS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏĞÑÒÓÔÕÖØÙÚÛÜİŞßàáâãäåæçèéêëìíîïğñòóôõöøùúûüış"

/* This is the size of the internal buffer for doing message handling.
   Articles that are bigger than this will have their ends chopped off
   before they are tokenized.  */
#define MAX_MESSAGE_SIZE (1024*512)

/* If there are more distinct words in a document than this, the
   overflow words are ignored. */
#define MAX_WORDS_PER_DOCUMENT 32768

/* Only this many characters from a header are considered. */
#define MAX_HEADER_LENGTH 1024

typedef struct {
  char *word;
  int count;
} word_count;

typedef struct {
  char author[MAX_HEADER_LENGTH];
  char subject[MAX_HEADER_LENGTH];
  time_t time;
  word_count *words;
} document;

document* parse_file(const char *file_name);
void tokenizer_init(void);

#endif
