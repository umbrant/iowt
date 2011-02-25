#ifndef IOWT_H
#define IOWT_H

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h> 

#include <libconfig.h>

#define PORT_STR "8001"
#define PORT_INT 8001


#ifdef DEBUG
	#define PRINTF(...) printf(__VA_ARGS__)
#else
	#define PRINTF(...)
#endif

int epfd;

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
int NUM_SERVERS;
const char ** SERVERS;/*[] = {
   "127.0.0.1",
   "192.168.99.20"
};*/

// Directory where test files are stored
const char * FILE_DIR;//[] = "/home/andrew/Downloads/enwiki";

// Set of files mmap'd into memory
typedef struct memfile {
	char* buffer;
	size_t size;
} memfile_t;
struct mmapfiles {
	memfile_t raw_64;
	memfile_t gzip_64;
	memfile_t lzo_64;
	memfile_t raw_256;
	memfile_t gzip_256;
	memfile_t lzo_256;
} mmapfiles;

// Strut and enums for defining a request
typedef struct request { 
    unsigned char size;
    unsigned char compression; 
    unsigned char storage; 
} request_t; 

enum SIZE {
    SIZE_64,
    SIZE_256
};
enum COMPRESSION {
    COMPRESSION_NONE,
    COMPRESSION_GZIP,
    COMPRESSION_LZO
};
enum STORAGE {
    STORAGE_DISK,
    STORAGE_MEMORY
};

typedef struct benchmark {
	request_t request;
	int destination;
	int iterations;
} benchmark_t;


// Main and server functions
int main(int argc, char *argv[]);
void* manager_main(void *threadid);
void* request_handler(void *fd_ptr);
int fd_from_request(request_t request, int* in_fd);
int disk_request(request_t request, int* in_fd);
int memory_request(request_t request, memfile_t* memfile);
void init_mmap_files();
void mmap_file(char* filename, memfile_t* memfile);

// Client and benchmark functions
int send_request(request_t request, int destination);
void benchmark(request_t request, int destination);
void* benchmark_worker(void* num_ptr);

// More generic utility functions
int make_request(int argc, char* argv[], request_t* request, int* destination);
void print_request(request_t request);
int init_config();
void error(const char *msg);
void usage();
int set_nonblocking(int sockfd);
void* get_in_addr(struct sockaddr *sa);

#endif
