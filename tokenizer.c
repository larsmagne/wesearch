/*
  The purpose of this program is to decode text parts of a message
  and remove all attachemnts before running the results into a
  search engine indexer.

  The function to use is parse_file(file_name).  It returns a static
  document structure, which has the author, subject, newsgroup, article
  number and time, as well as a list of word structures.  Each word
  structure has a string and a count.

  The caller doesn't have to allocate or free any structures.  They
  are all defined staticly in this file.

  A process should call 

    init_tokenizer();

  once before colling the parse_file() function.  This will set up the
  proper structures for downcasing Latin-1, and for using English stop
  words.

  This tokenizer has been tested with gmime 1.0.6 and glib 1.2.
 */

#include "tokenizer.h"
#include "config.h"
#include "stop_words.h"
#include "util.h"
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdlib.h>

#if defined(__FreeBSD__)
#define FSYNC(fd) fsync(fd)
#else
#define FSYNC(fd) fdatasync(fd)
#endif

#define DEBUG 0

unsigned char *downcase_rule[] =
  {"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏĞÑÒÓÔÕÖØÙÚÛÜİŞABCDEFGHIJKLMNOPQRSTUVWXYZ",
   "àáâãäåæçèéêëìíîïğñòóôõöøùúûüışabcdefghijklmnopqrstuvwxyz"};

unsigned char *equivalence_rule[] = 
  {"àáâãäçèéêëìíîïñòóôõöùúûüı",
   "aaaaaceeeeiiiinooooouuuuy"};

static char textType[] = "text";
static int textTypeLen = sizeof(textType) - 1;
static char plainSubType[] = "plain";
static int plainSubTypeLen = sizeof(plainSubType) - 1;
static char htmlSubType[] = "html";
static int htmlSubTypeLen = sizeof(htmlSubType) - 1;

static char buffer[MAX_MESSAGE_SIZE];
static unsigned char* bufp;
static document doc;

static unsigned char downcase[256];
static GHashTable *table = NULL;
static GHashTable *stop_words_table = NULL;
static word_count dword_table[MAX_WORDS_PER_DOCUMENT];
static char saved_body[MAX_SAVED_BODY_LENGTH];
static int saved_body_length = 0;

char *print_stop(void) {
  //printf("%x\n", stop_words_table);
  return (char*) stop_words_table;
}

void populate_stop_words_table(char **words) {
  char *word;

  stop_words_table = g_hash_table_new(g_str_hash, g_str_equal);
  while ((word = *words++) != 0)
    g_hash_table_insert(stop_words_table, word, (gpointer)1);    
}

void populate_downcase(void) {
  int i;
  unsigned char* text_characters = TEXT_CHARACTERS;

  for (i = 0; i<256; i++)
    downcase[i] = 0;

  for (i = 0; i<strlen(text_characters); i++)
    downcase[text_characters[i]] = text_characters[i];

  for (i = 0; i<strlen(downcase_rule[0]); i++) 
    downcase[downcase_rule[0][i]] = downcase_rule[1][i];

  /*
  for (i = 0; i<256; i++)
    printf("%c", downcase[i]);
  */
}

int stop_word_p(const char *word) {
  if (g_hash_table_lookup(stop_words_table, (gconstpointer)word))
    return 1;
  else
    return 0;
}

int count_word(const char* word) {
  int count = 0;
  if (! stop_word_p(word)) {
    count = (int)g_hash_table_lookup(table, (gconstpointer)word);
    /*
    if (count == 0)
        printf("%s\n", word);
    */
    count++;
    g_hash_table_insert(table, (gpointer)word, (gpointer)count);
  }
  return count;
}

/* Words that are too short, too long, or consist purely of numbers,
   or are all 8-bits, are ignored. */

int word_ignore(unsigned char *beg, unsigned char *end) {
  unsigned char *num_check;
  int length = end - beg;
  int num_digits = 0;
  int has_ascii_p = 0;
  int base64_p = 1;
  
  if (length < MIN_WORD_LENGTH ||
      length > MAX_WORD_LENGTH)
    return 1;

  for (num_check = beg; num_check<end; num_check++) {
    if (isdigit(*num_check)) {
      if (num_digits++ > 4)
	return 1;
    } else {
      if (*num_check < 128)
	has_ascii_p = 1;
    }

    if (! (isdigit(*num_check) ||
	   (*num_check >= 'a' && *num_check <= 'f')))
      base64_p = 0;

  }

  if (base64_p)
    return 1;

  if (has_ascii_p)
    return 0;
  
  return 1;
}

/* Save some text from the body of a message.  The idea here is that
   we ignore all lines that start with ">" to avoid saving bits of
   quoted text. */
void save_body_bits(const char *text, int start, int end) {
  char c;
  int i;
  int nl = 1;
  int save = 0;
  
  for (i = start; i<end; i++) {
    c = text[i];
    if (nl && c == '>')
      save = 0;
    if (save) {
      if (saved_body_length == MAX_SAVED_BODY_LENGTH - 1) {
	saved_body[saved_body_length] = 0;
	return;
      }
      if (c == '\n')
	saved_body[saved_body_length++] = ' ';
      else
	saved_body[saved_body_length++] = c;
    }
    if (c == '\n') {
      nl = 1;
      save = 1;
    } else
      nl = 0;
  }
}

/* Count unique words */
int tally(const gchar* itext, int start, int end, int tallied_length,
	  int remove_ids) {
  unsigned char c;
  unsigned char *word = bufp;
  int i;
  int blank = 0;
  unsigned char *text = (char*)itext;

  save_body_bits(itext, start, end);
  
  /* Remove all text before @ signs.  These are most likely Message-IDs
     and just pollute the word table. */
  if (remove_ids) {
    for (i = end-1; i >= start; i--) {
      if ((c = text[i]) == '@') {
	blank = 1;
      } else if (blank == 1) {
	if (c == '<' || c == ' ' || c == '\n') 
	  blank = 0;
	else 
	  text[i] = ' ';
      }
    }
  }

  if ((tallied_length + end - start) >= MAX_MESSAGE_SIZE) {
    fprintf(stderr, "Max message size reached.\n");
    return 0;
  }
  
  for (i = start; i<end; i++) {
    if ((c = downcase[text[i]]) != 0) {
      *bufp++ = c;
    } else {
      if (word != bufp) {
	if (! word_ignore(word, bufp)) {
	  *bufp++ = 0;
	  count_word(word);
	} 
	word = bufp;
      }
    }
  }
  if (word != bufp &&
      ! word_ignore(word, bufp)) {
    *bufp++ = 0;
    count_word(word);
  }
  return end - start;
}

int tally_string(const char *string, int tallied_length) {
  return tally(string, 0, strlen(string), tallied_length, 0);
}

void partFound(GMimePart* part, gpointer tallied_length) {
  const GMimeContentType* ct = 0;
  const gchar* content = 0;
  int contentLen = 0;
  int i = 0;
  int n = *(int*)tallied_length;

  ct = g_mime_part_get_content_type(part);
  /* printf("%s/%s\n", ct->type, ct->subtype); */
  if (ct != 0 &&
      strncasecmp(ct->type, textType, textTypeLen) == 0 ) {
    if (strncasecmp(ct->subtype, plainSubType, plainSubTypeLen) == 0) {
      content = g_mime_part_get_content(part, &contentLen);
      n += tally(content, 0, contentLen, n, 0);
    } else if (strncasecmp(ct->subtype, htmlSubType, htmlSubTypeLen) == 0) {
      gchar curChar = '\000';
      int beg = 0;
      content = g_mime_part_get_content(part, &contentLen);
      for (i=0; i<contentLen; ++i) {
	curChar = content[i];

	if (curChar == '<') {
	  if (i != beg) 
	    n += tally(content, beg, i, n, 0);
	}

	if (curChar == '>') 
	  beg = i+1;
      }
      if (i != beg) 
	n += tally(content, beg, i, n, 0);
    }
  }
  *((int*)tallied_length) = n;
}

void add_word_to_table(gpointer key, gpointer value, gpointer num_words) {
  int n = *(int*)num_words;

  if (n >= MAX_WORDS_PER_DOCUMENT) {
    fprintf(stderr, "Too many words in document.\n");
    return;
  }
  (*((int*)num_words))++;
  dword_table[n].word = (char*) key;
  dword_table[n].count = (int) value;
}

void downcase_string (char *string) {
  unsigned char c, newc;
  while ((c = *string) != 0) {
    newc = downcase[c];
    if (newc != 0)
      *string = newc;
    string++;
  }
}

document* parse_file(const char *file_name) {
  int tallied_length = 0;
  GMimeStream *stream;
  GMimeMessage *msg = 0;
  // struct stat stat_buf;
  const char *author, *subject, *xref, *xref_end;
  time_t date;
  int offset;
  int num_words = 0;
  int file;
  InternetAddress *iaddr;
  InternetAddressList *iaddr_list;
  char *address;

#if DEBUG
  printf("%s\n", file_name);
#endif

  /*
  if ((file = stat(file_name, &stat_buf)) == -1) {
    perror("tokenizer");
    return NULL;
  }
  */

  // |O_STREAMING
  if ((file = open(file_name, O_RDONLY|O_STREAMING)) == -1) {
    perror("tokenizer");
    return NULL;
  }

#ifdef POSIX_FADV_NOREUSE
  no_reuse(file);
#endif

  stream = g_mime_stream_fs_new(file);
  msg = g_mime_parser_construct_message(stream);
  g_mime_stream_unref(stream);

  if (msg != 0) {
    table = g_hash_table_new(g_str_hash, g_str_equal);
    bufp = buffer;
    dword_table[0].word = NULL;
    bzero(saved_body, MAX_SAVED_BODY_LENGTH);
    saved_body_length = 0;
    author = g_mime_message_get_sender(msg);
    subject = g_mime_message_get_subject(msg);
    xref = g_mime_message_get_header(msg, "Xref");
    g_mime_message_get_date(msg, &date, &offset);
    if (author != NULL && subject != NULL && xref != NULL) {
      tallied_length = tally_string(author, tallied_length);
      strncpy(doc.author, author, MAX_HEADER_LENGTH-1);

      /* Get the address from the From header. */
      if ((iaddr_list = internet_address_parse_string(author)) != NULL) {
	iaddr = iaddr_list->address;
	internet_address_set_name(iaddr, NULL);
	address = internet_address_to_string(iaddr, FALSE);
	strncpy(doc.address, address, MAX_HEADER_LENGTH-1);
	downcase_string(doc.address);
	free(address);
	internet_address_list_destroy(iaddr_list);
      } else {
	*doc.address = 0;
      }

      tallied_length = tally_string(subject, tallied_length);
      strncpy(doc.subject, subject, MAX_HEADER_LENGTH-1);

      doc.time = date;

      if ((xref = strchr(xref, ' ')) != NULL) {
	xref++;
	xref_end = strchr(xref, ':');
	*doc.group = 0;
	strncat(doc.group, xref, min(xref_end-xref, MAX_HEADER_LENGTH-1));
	xref_end++;
	sscanf(xref_end, "%d", &doc.article);
      }

      g_mime_message_foreach_part(msg, partFound, (gpointer) &tallied_length);

      strncpy(doc.body, saved_body, MAX_SAVED_BODY_LENGTH);

      g_hash_table_foreach(table, add_word_to_table, (gpointer) &num_words);
      dword_table[num_words].word = NULL;
      g_hash_table_destroy(table);
      g_mime_object_unref(GMIME_OBJECT(msg));
    } else {
      close(file);
      return NULL;
    }
  }
  close(file);
  doc.words = dword_table;
  doc.num_words = num_words;

  return &doc;
}

void print_words(word_count *words) {
  word_count *word;
  while (words->word) {
    word = words;
    printf("%s: %d\n", word->word, word->count);
    words++;
  }
}

void tokenizer_init(void) {
  populate_downcase();
  populate_stop_words_table(stop_words_en);
}

