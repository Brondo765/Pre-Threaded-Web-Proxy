CC = gcc
CFLAGS = -g -Wall

HEADERS = csapp.h dict.h sbuf.h
SOURCES = csapp.c dict.c sbuf.c 
OBJECTS = $(SOURCES:.c=.o)

all: libcsapp.a

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

libcsapp.a: $(OBJECTS)
	ar rcs $@ $^

clean:
	rm -f *~ *.o libcsapp.a
