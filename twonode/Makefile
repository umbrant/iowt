DEBUG=-DDEBUG -g -ggdb

CC=gcc
LIBS=-lpthread `pkg-config --libs libconfig`
CFLAGS=-Wall -O2 `pkg-config --cflags libconfig`

node: node.h node.c
	${CC} -o node node.c ${LIBS} ${CFLAGS} ${DEBUG}

clean:
	rm -f node

