#include <stdlib.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>

#include "search.h"
#include "mdb.h"
#include "index.h"

union sock {
  struct sockaddr s;
  struct sockaddr_in i;
};

struct option long_options[] = {
  {"index", 1, 0, 'i'},
  {"help", 1, 0, 'h'},
  {"port", 1, 0, 'p'},
  {0, 0, 0, 0}
};

void closedown(int);

#define BUFFER_SIZE 4096

int server_socket = 0;
static int port = 8010;

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hi:p:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'i':
      index_dir = optarg;
      break;
      
    case 'p':
      port = atoi(optarg);
      break;
      
    case 'h':
      printf ("Usage: we:index [--spool <directory>] <directories ...>\n");
      break;

    default:
      break;
    }
  }

  return optind;
}

int main(int argc, char **argv) {
  int wsd;
  int addlen, peerlen;
  FILE *client;
  time_t now;
  char *s;
  char buffer[BUFFER_SIZE];
  char *expression[MAX_SEARCH_ITEMS];
  struct sockaddr_in sin, caddr;
  int nitems = 0;
  int i;
  static int so_reuseaddr = TRUE;
  int dirn;
  int nfiles_indexed = 0;

  dirn = parse_args(argc, argv);

  mdb_init();
  tokenizer_init();

  if (signal(SIGHUP, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (signal(SIGINT, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("No socket");
    exit(1);
  }

  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, 
	     sizeof(so_reuseaddr));

  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(server_socket, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
    perror("Bind");
    exit(1);
  }

  if (listen(server_socket, 0) == -1) {
    perror("Bad listen");
    exit(1);
  }

  while (TRUE) {
    nitems = 0;
    printf("Accepting...\n");
    wsd = accept(server_socket, (struct sockaddr*)&caddr, &addlen);
    peerlen = sizeof(struct sockaddr);

    time(&now);
    client = fdopen(wsd, "r+");

    fgets(buffer, BUFFER_SIZE, client);

    printf("Got '%s'\n", buffer);

    s = strtok(buffer, " \n");

    while (s && nitems < MAX_SEARCH_ITEMS) {
      expression[nitems++] = s;
      s = strtok(NULL, " \n");
    }
    
    expression[nitems] = NULL;

    for (i = 0; i<nitems; i++) {
      printf("%d '%s'\n", i, expression[i]);
    }

    if (nitems > 1) {
      if (!strcmp(expression[0], "search")) {
	printf("Searching...\n");
	search(expression + 1, wsd);
      } else if (!strcmp(expression[0], "index")) {
	printf("Indexing...\n");
	index_file(expression[1]);
	if (! (nfiles_indexed++ % 100)) {
	  soft_flush();
	}
      }
    }

    fclose(client);
    close(wsd);

    time(&now);
    printf("Connection closed at %s",ctime(&now));
  }

  exit(1);
}

void closedown(int i) {
 time_t now = time(NULL);

 if (server_socket)
   close(server_socket);
 soft_flush();
 printf("Closed down at %s", ctime(&now));
 exit(0);
}
