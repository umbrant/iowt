#include "node.h"


void* manager_main(void *threadid) {

    int listen_sock;
    int i;

    // Clear out the page cache, make sure disk reads go to disk
    flush_page_cache();

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
       error("Could not set TCP_CORK!\n"); 
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
                fprintf(stderr, "ERROR reading from socket\n");
                continue;
            }
            print_request(request);
            // skip deprecated options
            if(request.compression == COMPRESSION_LZO ||
                    request.size == SIZE_256) {
                printf("Unsupported request type (lzo or 256)\n");
                close(sockfd);
                continue;
            }
            if(request.storage == STORAGE_DISK) {
        		// Get file descriptor of requested file
        		int in_fd;
        		size = disk_request(request, &in_fd);
        		// zero-copy I/O send to the socket
        		rv = sendfile(sockfd, in_fd, 0, size);
        		if(rv == -1) {
            		fprintf(stderr, "ERROR sendfile to socket\n");
        		}
        		close(in_fd);
        	}
        	else if(request.storage == STORAGE_MEMORY 
        	        && request.compression == COMPRESSION_NONE) {
				iovec_t memfile;
				size = memory_request(request, &memfile);
				// We use vmsplice() and splice() to do the equivalent
				// of zero-copy and sendfile() from a userspace buffer
                int pipefd[2]; // 0 is read end, 1 is write end
                if(pipe(pipefd) == -1) {
                    error("pipe error");
                }
                int bytes_to_vmsplice = memfile.iov_len;
                int bytes_to_splice = memfile.iov_len;
                while(bytes_to_vmsplice > 0) {
                    // vmsplice() memory into a pipe
                    int vmsplice_bytes = vmsplice(pipefd[1], &memfile, 1, SPLICE_F_NONBLOCK); 
                    if(vmsplice_bytes == -1) {
                        error("vmsplice failed");
                    } else {
                        bytes_to_vmsplice -= vmsplice_bytes;
                    }

                    // splice() pipe fd into socket fd
                    if(bytes_to_vmsplice < bytes_to_splice) {
                        int splice_bytes = splice(pipefd[0], NULL, sockfd, NULL, memfile.iov_len, 0);
				        if(splice_bytes == -1) {
				            //printf("%lu bytes sent so far\n", memfile.iov_len - bytes_to_splice);
					        fprintf(stderr,"ERROR splice to socket\n");
					        break;
				        } else {
                            bytes_to_splice -= splice_bytes;
                        }
                    }
                }
                // Splice the remainder
                while(bytes_to_splice > 0) {
                    int splice_bytes = splice(pipefd[0], NULL, sockfd, NULL, memfile.iov_len, 0);
				    if(splice_bytes == -1) {
				        //printf("%lu bytes sent so far\n", memfile.iov_len - bytes_to_splice);
					    error("ERROR splice to socket");
				    }
                    bytes_to_splice -= splice_bytes;
                }
				close(pipefd[0]);
				close(pipefd[1]);
        	}
        	else if(request.storage == STORAGE_MEMORY
        	        && request.compression == COMPRESSION_GZIP) {
				iovec_t memfile;
				size = memory_request(request, &memfile);
                rv = send(sockfd, memfile.iov_base, size, 0);
                if(rv == -1) {
                    error("ERROR sending gzip to socket");
                }
        	}
        	else {
        	    print_request(request);
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


int memory_request(request_t request, iovec_t* memfile)
{
	int s = request.size;
	int c = request.compression;
	if(c == COMPRESSION_NONE) {
		if(s == SIZE_64) {
			*memfile = mmapfiles.none_64;
		}
		if(s == SIZE_256) {
			*memfile = mmapfiles.none_256;
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
    return memfile->iov_len;
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
    // 64MB goes from xaa to xcs
    // xcs is the one that is mmap'd in, so it should not be
    // returned by this function
    strcat(filename, "/x");
    char c1 = '\0';
    char c2 = '\0';
    if(request.size == SIZE_64) {
        pthread_mutex_lock(&filecount_64_mutex);
        c1 = filecount_64_1;
        c2 = filecount_64_2;

        filecount_64_2++;

        // Fixup filecounts to rollover correctly
        if(filecount_64_2 > 'z') {
            filecount_64_2 = 'a';
            filecount_64_1++;
        } 
        // Flush and reset if we're at the end
        if(filecount_64_1 == 'c' &&
                filecount_64_2 == 's') {
            flush_page_cache();
            filecount_64_1 = 'a';
            filecount_64_2 = 'a';
        }
        pthread_mutex_unlock(&filecount_64_mutex);
    } else if(request.size == SIZE_256) {
        pthread_mutex_lock(&filecount_256_mutex);
        c1 = filecount_256;
        if(filecount_256 == 'j') {
            filecount_256 = 'a';
            // Flush page cache
            flush_page_cache();
        } else {
            filecount_256++;
        }
        pthread_mutex_unlock(&filecount_256_mutex);
    }

    strncat(filename, &c1, 1);
    strncat(filename, &c2, 1);
    strcat(filename, suffix);

    return 0;
}


void init_mmap_files()
{
    char filename[1025];
    memset(filename, '\0', 1024);

	// Lock the 64M files
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/none/xcs");
	mmap_file(filename, &mmapfiles.none_64, SHM_NONE_64);
	
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/gzip/xcs.gz");
	mmap_file(filename, &mmapfiles.gzip_64, SHM_GZIP_64);

    /*
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/64/lzo/xak.lzo");
	mmap_file(filename, &mmapfiles.lzo_64);
	*/

	// Lock the 256M files
	/*
	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/none/ak");
	mmap_file(filename, &mmapfiles.nona_256);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/gzip/ak.gz");
	mmap_file(filename, &mmapfiles.gzip_256);

	strcpy(filename, FILE_DIR);  
	strcat(filename, "/256/lzo/ak.lzo");
	mmap_file(filename, &mmapfiles.lzo_256);
	*/
}


void mmap_file(char* filename, iovec_t* memfile, int shmkey)
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
    		MAP_PRIVATE|MAP_LOCKED|MAP_POPULATE, fd, 0);
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

    // Stick it in shared memory too
    int shmid = shmget(shmkey, size, IPC_CREAT|0444);
    if(shmid < 0) {
        printf("You might need to increase shared memory limits:\n");
        printf("echo 1073741824 > /proc/sys/kernel/shmmax\n");
        error("shmget");
    }
    char* shmbuffer = shmat(shmid, NULL, SHM_RDONLY);
    if(shmbuffer < 0) {
        error("shmat");
    }
    // mlock it to keep it in memory
    if(mlock(shmbuffer, size)) {
        error("mlock");
    }

    memfile->iov_base = buffer;
    memfile->iov_len = size;

    close(fd);
}
