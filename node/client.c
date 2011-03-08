#include "node.h"



int send_request(request_t request, int destination)
{
    int sockfd, n, rv;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    long start_usecs, end_usecs;

    // Used for aggregating printf output until the end
    char outstr[1024];
    memset(outstr, '\0', 1024);

    // Lookup server in the destination table
    const char* server = SERVERS[destination];

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

    int megabytes = 0;
    if(request.size == SIZE_64) {
        megabytes = 64;
    }
    else if(request.size == SIZE_256) {
        megabytes = 256;
    }
    int bufsize = megabytes * (1<<20);
    char * buffer = (char*)calloc(1, bufsize);


    char s[INET6_ADDRSTRLEN];
    inet_ntop(rp->ai_family, get_in_addr((struct sockaddr *)rp->ai_addr),
            s, sizeof(s));
    freeaddrinfo(result); // Have to free the linked list of addrs

    start_usecs = get_time_usecs();

    sprintf(outstr, "%lu, Host %s, ", start_usecs, s);
    //strcat(outstr, "Sending request...");


    n = send(sockfd, &request, sizeof(request_t), 0);
    if (n < 0) 
        error("ERROR writing to socket");

    int bytes_read = 0;
    // Read the response, stream decompression if necessary
    switch(request.compression) {
        case COMPRESSION_NONE:
            bytes_read = read_uncompressed(sockfd, buffer, bufsize);
            break;
        case COMPRESSION_GZIP:
            bytes_read = read_gzip(sockfd, buffer, bufsize);
            break;
        case COMPRESSION_LZO:
            bytes_read = read_lzo(sockfd, buffer, bufsize);
            break;
    }

    end_usecs = get_time_usecs();

    free(buffer);

    double diff_secs = (double)(end_usecs - start_usecs) / (double)1000000;
    double request_size_mb = (double)bytes_read / (double)(1<<20);

    //sprintf(outstr+strlen(outstr), "response was size %d\n", bytes_read);
    sprintf(outstr+strlen(outstr), "rate: %f, size: %d\n", 
            request_size_mb/diff_secs, bytes_read);

    printf("%s", outstr);

    close(sockfd);
    return 0;
}


int send_local_request(request_t request)
{
    long start_usecs, end_usecs;

    // Used for aggregating printf output until the end
    char outstr[1024];
    memset(outstr, '\0', 1024);

    int megabytes = 0;
    int shmkey = 0;
    if(request.size == SIZE_64) {
        megabytes = 64;
        switch(request.compression) {
            case COMPRESSION_NONE:
                shmkey = SHM_NONE_64;
                break;
            case COMPRESSION_GZIP:
                shmkey = SHM_GZIP_64;
                break;
            case COMPRESSION_LZO:
                shmkey = SHM_LZO_64;
                break;
        }
    }
    else if(request.size == SIZE_256) {
        megabytes = 256;
        switch(request.compression) {
            case COMPRESSION_NONE:
                shmkey = SHM_NONE_256;
                break;
            case COMPRESSION_GZIP:
                shmkey = SHM_GZIP_256;
                break;
            case COMPRESSION_LZO:
                shmkey = SHM_LZO_256;
                break;
        }
    }

    int bufsize = megabytes * (1<<20);
    char * buffer = (char*)calloc(1, bufsize);

    start_usecs = get_time_usecs();

    sprintf(outstr, "%lu, Host %s, ", start_usecs, ipaddress);
    //strcat(outstr, "Sending request...");

    int bytes_read = 0;
    // Read the response, stream decompression if necessary
    switch(request.compression) {
        case COMPRESSION_NONE:
            bytes_read = read_local_uncompressed(shmkey, buffer, bufsize);
            break;
        case COMPRESSION_GZIP:
            bytes_read = read_local_gzip(shmkey, buffer, bufsize);
            break;
        case COMPRESSION_LZO:
            bytes_read = read_local_lzo(shmkey, buffer, bufsize);
            break;
    }

    end_usecs = get_time_usecs();

    free(buffer);

    double diff_secs = (double)(end_usecs - start_usecs) / (double)1000000;
    double request_size_mb = (double)bytes_read / (double)(1<<20);

    sprintf(outstr+strlen(outstr), "rate: %f, size: %d\n", 
            request_size_mb/diff_secs, bytes_read);

    printf("%s", outstr);

    return 0;
}


/* 
 * We can read directly into the final output buffer, since
 * no decompression needs to be done.
 *
 * Have to call recv multiple times, since it will return early
 */
int read_uncompressed(int sockfd, char* buffer, int bufsize)
{
    int bytes_read = 0;
    int bytes_to_read = READ_CHUNKSIZE;
    int n = 0;
    do {
        n = recv(sockfd, buffer+bytes_read, bytes_to_read*sizeof(char), MSG_WAITALL);
        if(n < 0)
            error("ERROR reading from socket");
        bytes_read += n;
    } while(n == bytes_to_read && bytes_read < bufsize);

    return bytes_read;
}

// This just does a straight copy into the buffer
int read_local_uncompressed(int shmkey, char* buffer, int bufsize) {
    int shmid = shmget(shmkey, bufsize, 0444);
    if(shmid < 0) {
        error("read_local_uncompressed shmget");
    }
    char* source = shmat(shmid, NULL, SHM_RDONLY);
    if(shmat < 0) {
        error("read_local_uncompressed shmat");
    }
    memcpy(buffer, source, bufsize);
    shmdt(source);

    return bufsize;
}

/*
 * gzip decompression, courtesy of zlib.
 * this code so much shorter than the uncompressed case even, gotta love
 * zlib doing buffering internally.
 *
 * I did check the resultant file too, it's correct.
 */
int read_gzip(int sockfd, char* buffer, int bufsize)
{
    int bytes_read = 0;
    gzFile gfile = gzdopen(sockfd, "r");
    bytes_read = gzread(gfile, buffer, bufsize);

    return bytes_read;
}

int read_local_gzip(int shmid, char* buffer, int bufsize) {
    return bufsize;
}

/*
 * LZO decompression, courtesy of liblzo2.
 * The documentation on this library is quite sparse, meaning that there only
 * seems to be one function to call for decompression (and thus no way of
 * doing it on-the-fly).
 */
int read_lzo(int sockfd, char* buffer, int bufsize)
{
    int rv;

    unsigned char* temp_buffer = (unsigned char*)calloc(bufsize, sizeof(unsigned char));

    rv = lzo_init();
    if(rv != LZO_E_OK) {
        printf("Could not init LZO!\n");
        exit(-1);
    }

    lzo_int bytes_read = 0;
    int bytes_to_read = READ_CHUNKSIZE;
    int n = 0;
    do {
        n = recv(sockfd, temp_buffer+bytes_read, bytes_to_read*sizeof(char), MSG_WAITALL);
        if(n < 0)
            error("ERROR reading from socket");
        bytes_read += n;
    } while(n == bytes_to_read && bytes_read < bufsize);

    FILE* outfile = fopen("temp.lzo", "w");
    fwrite(temp_buffer, 1, bytes_read, outfile);
    fclose(outfile);

    lzo_uint new_size = bufsize;
    rv = lzo1x_decompress((const unsigned char*)temp_buffer, bytes_read, 
            (unsigned char*)buffer, &new_size, NULL);
    printf("Old size: %lu New: %lu\n", (unsigned long)bytes_read, (unsigned long)new_size);
    if(rv != LZO_E_OK) {
        printf("internal LZO error - decompression failed: %d\n", rv);
        exit(-2);
    }

    free(temp_buffer);

    return (int)bytes_read;
}

int read_local_lzo(int shmid, char* buffer, int bufsize) {
    return bufsize;
}

void* benchmark_worker(void* num_ptr)
{
	benchmark_t bench = *(benchmark_t*)num_ptr;
    int i;

    // Make a good random seed, different per machine
    // Do this by hashing the machine's hostname
    char myhostname[100];
    memset(myhostname, '\0', 100);
    gethostname(myhostname, 100);
    char * hostname_trimmed = myhostname;
    int host_len = strlen(myhostname);
    // We want to get the end for a unique salt, 
    // since on aws the beginning is prefixed
    if(host_len > 14) {
        hostname_trimmed = myhostname + host_len - 14;
    }
    // Also need to incorporate a per-thread uniqueness 
    char tid[5];
    memset(tid,'\0', 5);
    sprintf(tid, "%d", bench.thread_id);

    // Every thread now will have a unique salt
    char salt[25];
    memset(salt, '\0', 25);
    strcat(salt, "$6$");
    strcat(salt, tid);
    strcat(salt, hostname_trimmed);
    strcat(salt, "$");

    char *hash = crypt(myhostname, salt);
    unsigned int hash_sum = time(NULL); // artificial sum, just need something
    int hash_uint_len = strlen(hash)/(sizeof(unsigned int));
    unsigned int* hash_uint = (unsigned int*)hash;
    for(i=0; i<hash_uint_len; i++) {
        hash_sum ^= hash_uint[i];
    }

    srandom(hash_sum);

    char rand_str[100];
    for(i=0; i<bench.iterations; i++) {
        // Generate a new hash for the actual selection
        // with the thread's unique salt
        memset(rand_str, '\0', 100);
        sprintf(rand_str, "%lu", random());
        unsigned int destination = time(NULL);
        pthread_mutex_lock(&crypt_mutex);
        hash = crypt(rand_str, salt);
        pthread_mutex_unlock(&crypt_mutex);
        hash_uint = (unsigned int*)hash;
        hash_uint_len = strlen(hash)/(sizeof(unsigned int));
        int k;
        for(k=0; k<hash_uint_len; k++) {
            destination ^= hash_uint[k];
        }
    	// Choose a random server from SERVERS to connect to
    	destination = destination%NUM_SERVERS;
    	int rv = 0;
    	// Do a shm read if we're going to ourself
    	if(!strcmp(ipaddress, SERVERS[destination])) {
            rv = send_local_request(bench.request);    
    	}
    	// Else, we go out to the remote server, with retries
    	do {
        	rv = send_request(bench.request, destination);
        } while(rv);
    }
	free(num_ptr);
    return 0;
}


void benchmark(request_t request, const int num_requests, const int num_threads) 
{
    int requests_per_thread = num_requests / num_threads;
    int i;
    long start_usec, end_usec;
    pthread_t workers[num_threads];


    // Get local machine's eth0 ip address
    //char ipaddress[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddress = NULL;
    getifaddrs(&ifaddress);
    for(getifaddrs(&ifaddress); ifaddress != NULL; ifaddress = ifaddress->ifa_next) {
        if(ifaddress->ifa_addr->sa_family == AF_INET) {
            if(!strcmp(ifaddress->ifa_name, "eth0")) {
                void* tmp_ptr = &((struct sockaddr_in *)ifaddress->ifa_addr)->sin_addr;
                inet_ntop(AF_INET, tmp_ptr, ipaddress, INET_ADDRSTRLEN);
                break; 
            }
        }
    }
    printf("%s\n", ipaddress);

    print_request(request);
    printf("num requests: %d, num threads: %d\n", num_requests, num_threads);
    printf("\n");

    start_usec = get_time_usecs();

    for(i=0; i<num_threads; i++) {
    	// Set up benchmark parameters to be passed to workers
    	benchmark_t* bench = (benchmark_t*)malloc(sizeof(benchmark_t));
    	bench->request = request;
    	bench->thread_id = i;
    	bench->iterations = requests_per_thread;
        pthread_create(&workers[i], NULL, benchmark_worker, 
                (void*)bench);
    }
    for(i=0; i<num_threads; i++) {
        pthread_join(workers[i], NULL);
    }

    end_usec = get_time_usecs();

    long diff_usec = end_usec - start_usec;
    double diff = (double)diff_usec / (double)1000000;

    double req_per_sec = (double)(num_requests) / (double)diff;
    printf("\nTotal time: %.4f\n", diff);
    printf("Requests per second: %f\n", req_per_sec);
}


long get_time_usecs()
{
    struct timeval time;
    struct timezone tz;
    memset(&tz, '\0', sizeof(timezone));
    gettimeofday(&time, &tz);
    long usecs = time.tv_sec*1000000 + time.tv_usec;

    return usecs;
}
