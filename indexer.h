#ifndef INDEXER_H
#define INDEXER_H

int index_file(const char *file_name);
void index_article(const char* group, int article);
void index_directory(const char* dir_name);

struct option long_options[] = {
  {"spool", 1, 0, 's'},
  {"help", 1, 0, 'h'},
  {0, 0, 0, 0}
};


#endif
