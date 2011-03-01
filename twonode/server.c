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
    int flag = 1;
    if(setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, 
            (void*)&flag, (socklen_t)sizeof(flag))) {
        error("Could not set socket to release bind!\n");
    }
    // Set TCP_CORK, wait for full frames before sending (better tput)
    if(setsockopt(listen_sock, IPPROTO_TCP, TCP_CORK, 
                (char *)&flag, sizeof(flag) )) {
       error("Could not set TCP_NODELAY!\n"); 
    }
    /*
    if(setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, 
                (char *)&flag, sizeof(flag) )) {
       error("Could not set TCP_NODELAY!\n"); 
    }
    */



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
        int size = -1;
        for(i=0; i<nfds; i++) {
            int sockfd = events[i].data.fd;
            request_t request;
            // Read request from the socket
            int rv = recv(sockfd, &request, sizeof(request), 0);
            if (rv < 0) {
                error("ERROR reading from socket\n");
            }
            print_request(request);
            if(request.storage == STORAGE_DISK) {
        		// Get file descriptor of requested file
        		int in_fd;
        		size = disk_request(request, &in_fd);
        		// zero-copy I/O send to the socket
        		rv = sendfile(sockfd, in_fd, 0, size);
        		if(rv == -1) {
            		error("ERROR sendfile to socket");
        		}
        	}
        	else if(request.storage == STORAGE_MEMORY) {
				memfile_t memfile;
				size = memory_request(request, &memfile);
				// send it normally to the socket
				rv = send(sockfd, memfile.buffer, size, 0);
				if(rv == -1) {
					error("ERROR send to socket");
				}
        	}
        	else {
        		error("Invalid request.storage");
        	}
            PRINTF("Size of file: %d\n", size);
        	close(sockfd);
        }
    }

    return 0;
}


int disk_request(request_t request, int* in_fd)
{
    char* filename = (char*) calloc(1024, sizeof(char));
    get_request_filename(request, filename);

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

    free(filename);

    return size;
}


int memory_request(request_t request, memfile_t* memfile)
{
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


int get_request_filename(request_t request, char* filename)
{
    char suffix[6];
    memset(suffix, '\0', 6);

    strcat(filename, FILE_DIR);

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
            return -1;
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
            return -1;
            break;
    }

    // Finally, the file basename
    strcat(filename, "/a");
    char c;
    if(request.size == SIZE_64) {
        pthread_mutex_lock(&filecount_64_mutex);
        c = filecount_64;
        if(filecount_64 == 'q') {
            filecount_64 = 'a';
        } else {
            filecount_64++;
        }
        pthread_mutex_unlock(&filecount_64_mutex);
    } else if(request.size == SIZE_256) {
        pthread_mutex_lock(&filecount_256_mutex);
        c = filecount_256;
        if(filecount_256 == 'k') {
            filecount_256 = 'a';
        } else {
            filecount_256++;
        }
        pthread_mutex_unlock(&filecount_256_mutex);
    }

    strncat(filename, &c, 1);
    strcat(filename, suffix);

    return 0;
}


void init_mmap_files()
{
    char filename[1024];
    memset(filename, '\0', 1024);

	// Lock the 64M files
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/none/ar");
	mmap_file(filename, &mmapfiles.raw_64);
	
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/gzip/ar.gz");
	mmap_file(filename, &mmapfiles.gzip_64);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/lzo/ar.lzo");
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
    // Tell the kernel that this is going to be read sequentially
    // Without this, the kernel doesn't do readahead?
    // Does not seem to have any performance improvement though
    rv = madvise(buffer, size, MADV_SEQUENTIAL);
    if(rv) {
        fprintf(stderr, "Could not madvise mmap'd pages! Performance might suffer.\n");
    }

    memfile->buffer = buffer;
    memfile->size = size;

    // Forcibly touch each page (please don't get optimized out)
    int pagesize = 4096;
    int i;
    char temp = 0;
    for(i=0; i<size/pagesize; i++) {
		temp += memfile->buffer[i*pagesize];
    }

    close(fd);
}
