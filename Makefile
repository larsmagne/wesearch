CPPFLAGS+=-g -I/usr/include/glib-1.2 -I/usr/lib/glib/include -I/usr/local/include/gmime -I/usr/local/include/gmime/gmime -I/usr/local/include -I/usr/local/BerkeleyDB.4.1/include
LDFLAGS+=-L/usr/local/lib -L/usr/local/BerkeleyDB.4.1/lib -lgmime -lglib -ldb

all: indexer

tokenizer.o: tokenizer.c tokenizer.h

mdb.o: mdb.c mdb.h config.h

indexer: indexer.o mdb.o tokenizer.o
	gcc $(CPPFLAGS) -o indexer $(LDFLAGS) tokenizer.o mdb.o indexer.o

clean:
	$(RM) indexer *.o

