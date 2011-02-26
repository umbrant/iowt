#include "node.h"


int main (int argc, char *argv[])
{
    // Read and initialize configuration settings
    init_config();

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
    serv_addr.sin_port = htons(atoi(PORT_STR));
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
    int epfd = epoll_create(EPOLL_QUEUE_LEN);
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


int disk_request(request_t request, int* in_fd)
{
    char filename[1024];
    memset(filename, '\0', 1024);
    char suffix[6];
    memset(suffix, '\0', 6);

    strcat(filename, FILE_DIR);

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
	strcat(filename, "/256/none/al");
	mmap_file(filename, &mmapfiles.raw_256);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/gzip/al.gz");
	mmap_file(filename, &mmapfiles.gzip_256);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/lzo/al.lzo");
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
    char* buffer = mmap(NULL, size, PROT_READ, 
    		MAP_PRIVATE|MAP_LOCKED, fd, 0);
    if(buffer == MAP_FAILED) {
    	fprintf(stderr, "You might need to change the \"max locked memory\" ulimit:\n");
    	fprintf(stderr, "    ulimit -l 1048676\n");
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
