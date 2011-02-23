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
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define PORT_STR "8001"
#define PORT_INT 8001


#ifdef DEBUG
	#define PRINTF(...) printf(__VA_ARGS__)
#else
	#define PRINTF(...)
#endif

static int epfd;

// epoll settings
#define EPOLL_QUEUE_LEN 10
#define MAX_EVENTS 10
#define EPOLL_TIMEOUT -1

// thread settings
#define NUM_WORKER_THREADS 20 

// benchmark settings, these should be divisible
// NUM_BENCH_REQUESTS should also be a multiple of 1000
#define NUM_BENCH_THREADS 5
#define NUM_BENCH_REQUESTS 100*1000

#define NUM_SERVERS 2
static char *SERVERS[] = {
   "127.0.0.1",
   "10.0.0.1"
};

int main(int argc, char *argv[]);

void* manager_main(void *threadid);
void* request_handler(void *fd_ptr);
int handle_io_on_socket(int fd);
int send_request(int destination);
void benchmark();

void error(const char *msg);
void usage();
int set_nonblocking(int sockfd);
void* get_in_addr(struct sockaddr *sa);

#endif
