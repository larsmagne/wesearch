#ifndef CONFIG_H
#define CONFIG_H

/* Where the index files will be stored. */
#define INDEX_DIRECTORY "/index/we-work"

/* The root of the news spool. */
#define NEWS_SPOOL "/mirror/var/spool/news/articles/"

/* The file where the word entries are stored.  This file will be
   WORD_SLOTS times BLOCK_SIZE big. */
#define	WORD_FILE "words.db"

/* The file where overflow from the hash buckets from the table above
   are stored.  This file will grow as needed. */
#define	WORD_EXTENSION_FILE "word_extensions.db"

/* The file where word instances are stored.  This file will grow in
   leaps and bounds when doing indexing, and has a size pretty much
   correlated with the size of the number of documents. */
#define INSTANCE_FILE "instances.db"
#define NEW_INSTANCE_FILE "instances.db.tmp"

/* The file where some information about articles is stored. */
#define ARTICLE_FILE "articles.db"

/* The file where some information about groups is stored. */
#define GROUP_FILE "groups.db"

/* The number of word slots.  Must be a power of two.  This is the
   main factor in determining the size of the memory usage. */
#define WORD_SLOTS (524288 * 8)

/* The maximum number of extension slots.  If you need more of them,
   add them. */
#define WORD_EXTENSION_SLOTS (1024*200)

/* The block size used in the tables above. */
#define BLOCK_SIZE 1024
#define WORD_BLOCK_SIZE 256

/* The maximum number of instance blocks.  This is kept on disk, and
   should be set to something that is big enough for your data set. */
#define INSTANCE_TABLE_SIZE (1024*1024*40)

/* This is an in-memory buffer of the table above.  The bigger this
   buffer is, the less disk traffic is needed. */
#define INSTANCE_BUFFER_SIZE ((int)(1024*1024*1))

/* This variable says how many distinct words per document we should
   accept.  Articles with a whole lot of distinct words are usually
   not very interesting, and may well be bogus. */
#define MAX_DISTINCT_WORDS 1000

/* If you have a Linux with O_STREAMING, use the following define. */
#define O_STREAMING    04000000
/* If not, uncomment the following. */
/* #define O_STREAMING    0 */

/* Ignore words longer than this */
#define MAX_WORD_LENGTH 24

/* Ignore words shorter than this */
#define MIN_WORD_LENGTH 3

/* To determine how much memory that will be allocated, it's basically:

   WORD_SLOTS * BLOCK_SIZE + INSTANCE_BUFFER_SIZE * BLOCK_SIZE

   In addition, if your word slots are overflowing, you will get word
   extension blocks allocated dynamically, and this will lead to
   memory growth.  If the number of WORD_SLOTS is small, you'll get
   more growth, and the performance will suffer, since the searches
   will tend more towards linear searches instead of hash lookups.

   Note that neither the word slots or the instance buffers are
   allocated before they are needed, so the indexer will usually start
   off with a pretty small footprint, before exploding pretty rapidly
   until it reaches equilibrium, and then it will grow slowly up to
   the boundaries defined by these config options above. */

#endif
