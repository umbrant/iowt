#ifndef IOWT_H
#define IOWT_H

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <crypt.h>
#include <assert.h>

#include <arpa/inet.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h> 
#include <sys/uio.h>

#include <libconfig.h>
#include "zlib.h"
#include <lzo/lzo1x.h>

#define PORT_STR "8001"
#define PORT_INT 8001


#ifdef DEBUG
	#define PRINTF(...) printf(__VA_ARGS__)
#else
	#define PRINTF(...)
#endif


// Server settings
#define EPOLL_QUEUE_LEN 10
#define MAX_EVENTS 10
#define EPOLL_TIMEOUT -1
int NUM_WORKER_THREADS;

// Client settings
#define READ_CHUNKSIZE 1024*16

// benchmark settings, these should be divisible
// NUM_BENCH_REQUESTS should also be a multiple of 1000
//int NUM_BENCH_THREADS, NUM_BENCH_REQUESTS;
//#define NUM_BENCH_THREADS 1
//#define NUM_BENCH_REQUESTS 1*1000

// Lookup table for destinations
// Make sure to increment NUM_SERVERS appropriately
int NUM_SERVERS;
const char ** SERVERS;

// Directory where test files are stored
const char * FILE_DIR;//[] = "/home/andrew/Downloads/enwiki";

// Set of files mmap'd into memory
/*
typedef struct memfile {
	char* buffer;
	size_t size;
} memfile_t;
*/
typedef struct iovec iovec_t;
struct mmapfiles {
	iovec_t raw_64;
	iovec_t gzip_64;
	iovec_t lzo_64;
	iovec_t raw_256;
	iovec_t gzip_256;
	iovec_t lzo_256;
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
	int iterations;
	int thread_id;
} benchmark_t;

// I should make one count per permutation of request, but I'm lazy and that
// is a lot of typing and hassle for something that is not going to bottleneck.
pthread_mutex_t filecount_64_mutex;
pthread_mutex_t filecount_256_mutex;
char filecount_64_1;
char filecount_64_2;
char filecount_256;

// Main and server functions
int main(int argc, char *argv[]);
void* manager_main(void *threadid);
void* request_handler(void *fd_ptr);
int fd_from_request(request_t request, int* in_fd);
int disk_request(request_t request, int* in_fd);
int memory_request(request_t request, iovec_t* memfile);
int get_request_filename(request_t request, char* filename);
void init_filenames();
void init_mmap_files();
void mmap_file(char* filename, iovec_t* memfile);

// Client and benchmark functions
int send_request(request_t request, int destination);
int read_uncompressed(int sockfd, char* buffer, int bufsize);
int read_gzip(int sockfd, char* buffer, int bufsize);
int read_lzo(int sockfd, char* buffer, int bufsize);
long get_time_usecs();
void benchmark(request_t request, const int num_requests, const int num_threads);
void* benchmark_worker(void* num_ptr);

// More generic utility functions
int make_request(int argc, char* argv[], request_t* request, int* destination);
void print_request(request_t request);
int init_config();
void error(const char *msg);
void usage();
int set_nonblocking(int sockfd);
void* get_in_addr(struct sockaddr *sa);
void flush_page_cache();

#endif