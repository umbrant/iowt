#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT_NUMBER 8001

static char *SERVERS[] = {
   "127.0.0.1",
   "10.0.0.1"
}

int main(int argc, char *argv[]);
void error(const char *msg);

void manager_main(void *threadid) {
void request_handler(void *threadid)
void request_maker(void *threadid)
