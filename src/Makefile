CC = gcc
CFLAGS = -g -Wall -I../lib
LDLIBS = -lpthread -L../lib -lcsapp
HEADERS = proxy.h cache.h
SOURCES = proxy.c
OBJECTS = $(SOURCES:.c=.o)

all: proxy
	@ln -rfs proxy ../proxy

proxy: $(OBJECTS) ../lib/libcsapp.a

$(OBJECTS): $(HEADERS)

clean:
	rm -f *~ *.o proxy ../proxy
