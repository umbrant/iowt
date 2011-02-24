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
#include <sys/sendfile.h>
#include <sys/stat.h>

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
#define NUM_WORKER_THREADS 5

// benchmark settings, these should be divisible
// NUM_BENCH_REQUESTS should also be a multiple of 1000
#define NUM_BENCH_THREADS 1
#define NUM_BENCH_REQUESTS 1*1000

// Lookup table for destinations
// Make sure to increment NUM_SERVERS appropriately
#define NUM_SERVERS 2
static char *SERVERS[] = {
   "127.0.0.1",
   "10.0.0.1"
};

// Directory where test files are stored
static char FILE_DIR[] = "/home/awang/Downloads/enwiki";

// Strut and enums for defining a request
typedef struct request { 
    unsigned char size;
    unsigned char compression; 
    unsigned char storage; 
} request_t; 

enum STORAGE {
    STORAGE_DISK,
    STORAGE_MEMORY
};
enum COMPRESSION {
    COMPRESSION_NONE,
    COMPRESSION_GZIP,
    COMPRESSION_LZO
};
enum SIZE {
    SIZE_64,
    SIZE_256
};



int main(int argc, char *argv[]);

void* manager_main(void *threadid);
void* request_handler(void *fd_ptr);
int handle_io_on_socket(int fd);
int send_request(int destination);
void benchmark();

int fd_from_request(request_t request, int* in_fd);
void print_request(request_t request);
void error(const char *msg);
void usage();
int set_nonblocking(int sockfd);
void* get_in_addr(struct sockaddr *sa);

#endif
