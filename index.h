#ifndef INDEX_H
#define INDEX_H

#define MAX_FILE_NAME 1024

void soft_flush(void);
int index_file(const char *file_name);

extern int total_unique_words;
extern int total_files;
extern time_t start_time;
extern char *news_spool;
extern char *from_file;
extern int instances;


#endif
