#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <gmime/gmime.h>
#include <stdio.h>
#include <string.h>

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
#define MAX_HEADER_LENGTH 80

/* How many bytes of text from the body that will be saved in the data
   base.  This is used for making pretty search results that displays
   a section of the matching article. */
#define MAX_SAVED_BODY_LENGTH 340

typedef struct {
  char *word;
  int count;
} word_count;

typedef struct {
  char author[MAX_HEADER_LENGTH];
  char address[MAX_HEADER_LENGTH];
  char subject[MAX_HEADER_LENGTH];
  char body[MAX_SAVED_BODY_LENGTH];
  time_t time;
  int num_words;
  word_count *words;
} document;

document* parse_file(const char *file_name);
void tokenizer_init(void);
int stop_word_p(const char *word);

#endif
