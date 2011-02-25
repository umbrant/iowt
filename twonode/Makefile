DEBUG=-DDEBUG -g -ggdb

CC=gcc
LIBS=-lpthread -lconfig -L/usr/lib
CFLAGS=-Wall -O2

node: node.h node.c
	${CC} -o node node.c ${LIBS} ${CFLAGS} ${DEBUG}

clean:
	rm -f node

