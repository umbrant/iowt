#ifndef IOWT_H
#define IOWT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT_STR "8001"
#define PORT_INT 8001

static int epfd;

// epoll settings
#define EPOLL_QUEUE_LEN 5
#define MAX_EVENTS 1
#define EPOLL_TIMEOUT -1

// thread settings
#define NUM_WORKER_THREADS 5

#define NUM_SERVERS 2
static char *SERVERS[] = {
   "127.0.0.1",
   "10.0.0.1"
};

int main(int argc, char *argv[]);
void error(const char *msg);

void* manager_main(void *threadid);
void* request_handler(void *epfd_ptr);
int handle_io_on_socket(int fd);
void request_sender(int destination);

#endif
