DEBUG=-DDEBUG -g -ggdb

LIBS=-lpthread -lconfig
CFLAGS=-Wall -O2

node: node.h node.c
	gcc -o node node.c ${LIBS} ${CFLAGS} ${DEBUG}

clean:
	rm -f node

