DEBUG=-DDEBUG -g -ggdb

CC=gcc
LIBS=-lpthread `pkg-config --libs libconfig zlib`
CFLAGS=-Wall -O2 `pkg-config --cflags libconfig zlib`

node: server.o client.o util.o
	${CC} -o node server.o client.o util.o ${LIBS} ${CFLAGS} ${DEBUG}

server.o: server.c node.h
	${CC} -c -o server.o server.c ${CFLAGS} ${DEBUG}

client.o: client.c node.h
	${CC} -c -o client.o client.c ${CFLAGS} ${DEBUG}

util.o: util.c node.h
	${CC} -c -o util.o util.c ${CFLAGS} ${DEBUG}


clean:
	rm -f node server.o client.o util.o
