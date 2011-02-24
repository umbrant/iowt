#include "node.h"


void* manager_main(void *threadid) {

    int listen_sock;
    int i;

	// Initialize memfiles, pinned into RAM
	init_mmap_files();

    // Initialize socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        error("ERROR opening listener socket");
    }

    // Ignore SIG_PIPE's (HACK HACK XXX FIXME?)
    signal(SIGPIPE, SIG_IGN);

    // Set socket to release bind
    int optVal = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, 
            (void*)&optVal, (socklen_t)sizeof(optVal));

    // Bind
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT_INT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    int rv = bind(listen_sock, 
            (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if(rv < 0) {
        error("ERROR on binding listener socket");
    }

    // Listen
    rv = listen(listen_sock, 5);
    if(rv < 0) {
        error("ERROR on listening on listener socket");
    }

    // Set up epoll to watch the socket file descriptor
    epfd = epoll_create(EPOLL_QUEUE_LEN);
    if(epfd < 0) {
        error("ERROR creating epoll");
    }
    static struct epoll_event ev;
    ev.events = EPOLLIN; // | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.fd = listen_sock;
    int res = epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &ev);
    if(res == -1) {
        error("ERROR in epoll_ctl setting up listener socket\n");
    }

    // Create worker threads
    // Each worker has their own epoll fd, RR load balance them
    pthread_t workers[NUM_WORKER_THREADS];
    int worker_epfds[NUM_WORKER_THREADS];
    for(i=0; i<NUM_WORKER_THREADS; i++) {
        worker_epfds[i] = epoll_create(EPOLL_QUEUE_LEN);
        if(worker_epfds[i] < 0) {
            error("ERROR creating worker epoll");
        }
        pthread_create(&workers[i], NULL, request_handler, 
                &worker_epfds[i]);
    }

    // Loop: wait for new new connections, add them to worker epolls
    printf("starting manager loop\n");

    int counter = 0;
    while(1) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, EPOLL_TIMEOUT);
        if(nfds == -1) {
            perror("epoll_pwait");
            exit(-1);
        }

        for(i=0; i<nfds; i++) {
            if(events[i].data.fd == listen_sock) {
                // Accept new connection
                int conn_sock = accept(listen_sock, NULL, 0); 
                if(conn_sock == -1) {
                    perror("accept");
                    exit(-1);
                } 

                // add conn_sock to a worker's epoll
                ev.events = EPOLLIN; // | EPOLLPRI | EPOLLERR | EPOLLHUP;
                ev.data.fd = conn_sock;
                PRINTF("Adding conn_sock (%d) to worker epfd (%d)\n",
                        conn_sock, worker_epfds[counter]);
                int rv = epoll_ctl(worker_epfds[counter++], 
                        EPOLL_CTL_ADD, conn_sock, &ev);
                if(rv == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(-1);
                }
                // Wrap load balance counter around
                counter = counter%NUM_WORKER_THREADS;
            }
        }
    }

    close(listen_sock);
    pthread_exit(0); 
}

void* request_handler(void *fd_ptr)
{
    int i;
    int epfd = *(int*)fd_ptr;
    printf("Worker started with epfd %d\n", epfd);

    while(1) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, EPOLL_TIMEOUT);
        if(nfds == -1) {
            perror("epoll_wait");
            continue;
            //exit(-1);
        }
        for(i=0; i<nfds; i++) {
            int sockfd = events[i].data.fd;
            request_t request;
            // Read request from the socket
            int rv = recv(sockfd, &request, sizeof(request), 0);
            if (rv < 0) {
                error("ERROR reading from socket\n");
            }
            if(request.storage == STORAGE_DISK) {
        		// Get file descriptor of requested file
        		int in_fd;
        		int size = disk_request(request, &in_fd);
        		// zero-copy I/O send to the socket
        		rv = sendfile(sockfd, in_fd, 0, size);
        		if(rv == -1) {
            		error("ERROR sendfile to socket");
        		}
        	}
        	else if(request.storage == STORAGE_MEMORY) {
				memfile_t memfile;
				int size = memory_request(request, &memfile);
				// send it normally to the socket
				rv = send(sockfd, memfile.buffer, size, 0);
				if(rv == -1) {
					error("ERROR send to socket");
				}
        	}
        	else {
        		error("Invalid request.storage");
        	}
        	close(sockfd);
        }
    }

    return 0;
}


int send_request(request_t request, int destination)
{
    int sockfd, n, rv;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    // Lookup server in the destination table
    char* server = SERVERS[destination];

    // Convert from human readable -> addrinfo

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    rv = getaddrinfo(server, PORT_STR, &hints, &result);
    if (rv != 0)
    {   
        fprintf(stderr, "error in getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // Traverse result linked list, until we connect
    for (rp = result; rp != NULL; rp=rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(sockfd == -1) {
            perror("client: socket");
            continue;
        }
        if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) != 0) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break; // Success!
    }

    if(rp == NULL) {
        fprintf(stderr, "Could not connect to server %s\n", server);
        return -1;
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(rp->ai_family, get_in_addr((struct sockaddr *)rp->ai_addr),
            s, sizeof(s));
    PRINTF("client connected to %s on port %s\n", s, PORT_STR);

    // Have to free the linked list of addrs
    freeaddrinfo(result);

    print_request(request);
    PRINTF("Sending request...\n");
    n = send(sockfd, &request, sizeof(request_t), 0);
    if (n < 0) 
        error("ERROR writing to socket");

    // Read the response
    int response_size = 0;
    static int bufsize = 1024*10;
    char buffer[bufsize];
    memset(buffer, '\0', bufsize);
    int bytes_to_read = bufsize;
    do {
        n = recv(sockfd, buffer, bytes_to_read*sizeof(char), MSG_WAITALL);
        if (n < 0) 
            error("ERROR reading from socket");
        response_size += n;
    } while (n == bytes_to_read);

    PRINTF("response was size %d\n", response_size);

    close(sockfd);
    return 0;
}


int main (int argc, char *argv[])
{
	request_t request;
	int dest;

    if(argc < 2) {
        usage();
        exit(1);
    }
    // Server
    if(strcmp(argv[1],"server") == 0) {
        pthread_t manager;
        // Create manager thread, which spans more handlers
        int rv = pthread_create(&manager, NULL, manager_main, NULL);
        if(rv) {
            printf("Error, could not start server manager thread.\n");
            exit(-1);
        }
        pthread_join(manager, NULL);
    }
    // Client
    else if(strcmp(argv[1],"client") == 0) {
		make_request(argc, argv, &request, &dest);
        send_request(request, dest);
    }
    // Benchmark
    else if(strcmp(argv[1], "benchmark") == 0) {
		make_request(argc, argv, &request, &dest);
        benchmark(request, dest);
    }
    // Else just print out the args...debug
    else {
        int i;
        for(i=0; i<argc; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");
    }
    pthread_exit(NULL);
}

int make_request(int argc, char* argv[], request_t* request, int* destination) {
    int dest, size, compression, storage;
    if(argc < 6) {
        usage();
        exit(1);
    }
    dest = atoi(argv[2]);
    if(dest > NUM_SERVERS-1 || dest < 0) {
        printf("Invalid destination server number (must be between 0 and %d)\n",
                NUM_SERVERS);
        exit(1);
    }
	// size
	if(strcmp(argv[3], "64") == 0)
		size = SIZE_64;
	else if(strcmp(argv[3], "256") == 0)
		size = SIZE_256;
	else {
		usage();
		exit(1);
	}
	// compression
	if(strcmp(argv[4], "none")==0)
		compression = COMPRESSION_NONE;
	else if(strcmp(argv[4], "gzip")==0)
		compression = COMPRESSION_GZIP;
	else if(strcmp(argv[4], "lzo")==0)
		compression = COMPRESSION_LZO;
	else {
		usage();
		exit(1);
	}
	// storage
	if(strcmp(argv[5], "disk") == 0)
		storage = STORAGE_DISK;
	else if(strcmp(argv[5], "memory") == 0)
		storage = STORAGE_MEMORY;
	else {
		usage();
		exit(1);
	}

	request->size = size;
	request->compression = compression;
	request->storage = storage;

	*destination = dest;

	return 0;
}

void* benchmark_worker(void* num_ptr)
{
	benchmark_t bench = *(benchmark_t*)num_ptr;
    int i;
    for(i=0; i<bench.iterations; i++) {
    	int rv = 0;
    	do {
        	rv = send_request(bench.request, bench.destination);
        } while(rv);
    }
	free(num_ptr);
    return 0;
}


void benchmark(request_t request, int destination) 
{
    int requests_per_thread = NUM_BENCH_REQUESTS / NUM_BENCH_THREADS;
    int i;
    struct timeval start, end;
    struct timezone tz;
    tz.tz_minuteswest = 480;
    tz.tz_dsttime = 0;

    pthread_t workers[NUM_BENCH_THREADS];


    gettimeofday(&start, &tz);
    for(i=0; i<NUM_BENCH_THREADS; i++) {
    	// Set up benchmark parameters to be passed to workers
    	benchmark_t* bench = (benchmark_t*)malloc(sizeof(benchmark_t));
    	bench->request = request;
    	bench->destination = destination;
    	bench->iterations = requests_per_thread;
        pthread_create(&workers[i], NULL, benchmark_worker, 
                (void*)bench);
    }
    for(i=0; i<NUM_BENCH_THREADS; i++) {
        pthread_join(workers[i], NULL);
    }
    gettimeofday(&end, &tz);

    long start_usec = start.tv_sec*1000000 + start.tv_usec;
    long end_usec = end.tv_sec*1000000 + end.tv_usec;

    long diff_usec = end_usec - start_usec;
    double diff = (double)diff_usec / (double)1000000;

    double req_per_sec = (double)(NUM_BENCH_REQUESTS) / (double)diff;
    printf("start: %ld end: %ld diff: %ld\n", start_usec, 
            end_usec, diff_usec);
    printf("Requests per second: %f\n", req_per_sec);
}


void init_mmap_files()
{
    char filename[1024];
    memset(filename, '\0', 1024);

	// Lock the 64M files
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/none/ap");
	mmap_file(filename, &mmapfiles.raw_64);
	
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/gzip/ap.gz");
	mmap_file(filename, &mmapfiles.gzip_64);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/lzo/ap.lzo");
	mmap_file(filename, &mmapfiles.lzo_64);

	// Lock the 256M files
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/none/ap");
	mmap_file(filename, &mmapfiles.raw_256);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/gzip/ap.gz");
	mmap_file(filename, &mmapfiles.gzip_256);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/lzo/ap.lzo");
	mmap_file(filename, &mmapfiles.lzo_256);
}

void mmap_file(char* filename, memfile_t* memfile)
{
	PRINTF("mmap'ing file %s\n", filename);

	// Open file
	int fd = open(filename, O_RDONLY);
	if(fd < 0) {
		error("open of file in mmap_file failed");
	}
    struct stat s;
    int rv = fstat(fd, &s);
    if(rv <0) {
        error("Stat of file failed");
    }
    int size = s.st_size;

    // mmap into memory, MAP_LOCKED effectively mlock's the pages
    //char* buffer = (char*)malloc(size*sizeof(char));
    char* buffer = mmap(NULL, size, PROT_READ, 
    		MAP_PRIVATE|MAP_LOCKED, fd, 0);
    if(buffer == MAP_FAILED) {
    	error("mmap of file failed");
    }

    memfile->buffer = buffer;
    memfile->size = size;

    // Forcibly touch each page (please don't get optimized out)
    int pagesize = 4096;
    int i;
    char temp;
    for(i=0; i<size/pagesize; i++) {
		temp += memfile->buffer[i*pagesize];
    }
}


int disk_request(request_t request, int* in_fd)
{
    char filename[1024];
    memset(filename, '\0', 1024);
    strcat(filename, FILE_DIR);
    char suffix[6];

    print_request(request);

    // Construct filename based on request options
    switch(request.size)
    {
        case SIZE_64:
            strcat(filename, "/64");
            break;
        case SIZE_256:
            strcat(filename, "/256");
            break;
        default:
            error("Invalid request.size!");
            break;
    }
    switch(request.compression)
    {
        case COMPRESSION_NONE:
            strcat(filename, "/none");
            break;
        case COMPRESSION_GZIP:
            strcat(filename, "/gzip");
            strcpy(suffix,".gz");
            break;
        case COMPRESSION_LZO:
            strcat(filename, "/lzo");
            strcpy(suffix,".lzo");
            break;
        default:
            error("Invalid request.compression!");
            break;
    }

    // Finally, the file basename
    strcat(filename, "/aa");
    strcat(filename, suffix);

    PRINTF("Filename: %s\n", filename);

    // Open the filename, set in_fd
    int fd = open(filename, O_RDONLY);
    *in_fd = fd;

    // Get and return the file size
    struct stat s;
    int rv = fstat(fd, &s);
    if(rv <0) {
        error("Stat of file failed.");
    }
    int size = s.st_size;
    PRINTF("Size of file: %d\n", size);

    return size;
}


int memory_request(request_t request, memfile_t* memfile)
{
    print_request(request);

	int s = request.size;
	int c = request.compression;
	if(c == COMPRESSION_NONE) {
		if(s == SIZE_64) {
			*memfile = mmapfiles.raw_64;
		}
		if(s == SIZE_256) {
			*memfile = mmapfiles.raw_256;
		}
	}
	else if(c == COMPRESSION_GZIP) {
		if(s == SIZE_64) {
			*memfile = mmapfiles.gzip_64;
		}
		if(s == SIZE_256) {
			*memfile = mmapfiles.gzip_256;
		}
	}
	else if(c == COMPRESSION_LZO) {
		if(s == SIZE_64) {
			*memfile = mmapfiles.lzo_64;
		}
		if(s == SIZE_256) {
			*memfile = mmapfiles.lzo_256;
		}
	}
	else {
		error("Invalid request.compression");
	}
	if(s != SIZE_64 && s != SIZE_256) {
		error("Invalid request.size");
	}

    // Get and return the file size
    return memfile->size;
}


void error(const char *msg)
{
    perror(msg);
    exit(1);
}


void usage()
{
    printf("node <server/client/benchmark> [destination]\n");
    printf("\n");
    printf("Must provide an integer destination when running in client mode.\n");
    printf("See SERVERS array in node.h for the list of servers.\n");
    printf("\n");
    printf("Benchmark mode chooses the first server in SERVERS by default.\n");
    printf("\n");
}

void print_request(request_t request) {
    printf("request size: ");
    switch(request.size)
    {
        case(SIZE_64):
            printf("64");
            break;
        case(SIZE_256):
            printf("256");
            break;
    }
    printf(", ");
    printf("compression: ");
    switch(request.compression)
    {
        case(COMPRESSION_NONE):
            printf("none");
            break;
        case(COMPRESSION_GZIP):
            printf("gzip");
            break;
        case(COMPRESSION_LZO):
            printf("lzo");
            break;
    }
    printf(", ");
    printf("storage: ");
    switch(request.storage)
    {
        case(STORAGE_DISK):
            printf("disk");
            break;
        case(STORAGE_MEMORY):
            printf("mem");
            break;
    }
    printf("\n");
}


int set_nonblocking(int sockfd) {
    int flags;
    if((flags = fcntl(sockfd, F_GETFL, 0)) == -1) {
        flags = 0;
    }
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
