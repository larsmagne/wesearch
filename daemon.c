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

#include "search.h"
#include "mdb.h"

union sock {
  struct sockaddr s;
  struct sockaddr_in i;
};

void closedown(int);

#define BUFFER_SIZE 4096

int server_socket = 0;

int main(int argc, char **argv) {
  int port = 8010;
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
      printf("%d %s\n", i, expression[i]);
    }

    if (nitems < 2 && !strcmp(expression[0], "search")) {
      search(expression + 1);
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
 printf("Closed down at %s", ctime(&now));
 exit(0);
}
