# Generated automatically from Makefile.in by configure.
GLIB_CFLAGS = -I/usr/include/glib-1.2 -I/usr/lib/glib/include
GLIB_CONFIG = /usr/bin/glib-config
GLIB_LIBS = -L/usr/lib -lglib

CPPFLAGS+=-g $(GLIB_CFLAGS) $(GMIME_CFLAGS) -I/usr/local/include
LDFLAGS+=-L/usr/local/lib -lgmime -lglib

all: indexer

tokenizer.o: tokenizer.c tokenizer.h

mdb.o: mdb.c mdb.h config.h

indexer: indexer.o mdb.o tokenizer.o
	gcc $(CPPFLAGS) -o indexer $(LDFLAGS) tokenizer.o mdb.o indexer.o

clean:
	$(RM) indexer *.o

