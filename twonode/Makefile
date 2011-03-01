DEBUG=#-DDEBUG -g -ggdb
OPTS=-O2

CC=gcc
LIBS=-lpthread -lconfig -lz -llzo2
CFLAGS=${OPTS} ${DEBUG} -Wall

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
