#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <gmime/gmime.h>
#include <stdio.h>
#include <string.h>

#define TEXT_CHARACTERS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏĞÑÒÓÔÕÖØÙÚÛÜİŞßàáâãäåæçèéêëìíîïğñòóôõöøùúûüış"

typedef struct {
  char *word;
  int count;
} word_count;

#define MAX_MESSAGE_SIZE (1024*512)  /* We only do the first 8Ks */
#define MAX_WORDS_PER_DOCUMENT 32768
#define MAX_HEADER_LENGTH 1024
#define MAX_WORD_LENGTH 24 /* Ignore words longer than this */
#define MIN_WORD_LENGTH 3  /* Ignore words shorter than this */

typedef struct {
  char author[MAX_HEADER_LENGTH];
  char subject[MAX_HEADER_LENGTH];
  time_t time;
  word_count *words;
} document;

document* parse_file(const char *file_name);
void tokenizer_init(void);
void populate_stop_words_table(char **words);
void populate_downcase(void);

#endif
