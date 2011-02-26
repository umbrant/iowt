DEBUG=-DDEBUG -g -ggdb
#OPTS=-O2

CC=gcc
LIBS=-lpthread `pkg-config --libs libconfig zlib`
CFLAGS=${OPTS} ${DEBUG} -Wall `pkg-config --cflags libconfig zlib`

node: server.o client.o util.o
	${CC} -o node server.o client.o util.o ${LIBS} ${CFLAGS}

server.o: server.c node.h
	${CC} -c -o server.o server.c ${CFLAGS}

client.o: client.c node.h
	${CC} -c -o client.o client.c ${CFLAGS}

util.o: util.c node.h
	${CC} -c -o util.o util.c ${CFLAGS}


clean:
	rm -f node server.o client.o util.o
