#ifndef INDEX_H
#define INDEX_H

#define MAX_FILE_NAME 1024
#define INDEXED_FILES_FILE "indexed_files.db"

void soft_flush(void);
int index_file(const char *file_name);
void indexer_init(void);
void read_indexed_files_table(void);
void flush_indexed_file(void);
void index_word(char *word, int count, int article_id);

extern int total_unique_words;
extern int total_files;
extern time_t start_time;
extern char *news_spool;
extern char *from_file;
extern int instances;
extern int suppress_duplicate_files;

#endif
