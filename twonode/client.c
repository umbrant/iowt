#include "node.h"



int send_request(request_t request, int destination)
{
    int sockfd, n, rv;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    long start_usecs, end_usecs;

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

    start_usecs = get_time_usecs();

    char s[INET6_ADDRSTRLEN];
    inet_ntop(rp->ai_family, get_in_addr((struct sockaddr *)rp->ai_addr),
            s, sizeof(s));
    PRINTF("client connected to %s on port %s\n", s, PORT_STR);

    // Have to free the linked list of addrs
    freeaddrinfo(result);

    //print_request(request);
    PRINTF("Sending request...");
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

    PRINTF("response was size %d\n", bytes_read);
    printf("Rate (MB/s): %f\n", request_size_mb/diff_secs);

    close(sockfd);
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


void benchmark(request_t request, const int destination, const int num_requests, 
        const int num_threads) 
{
    int requests_per_thread = num_requests / num_threads;
    int i;
    long start_usec, end_usec;
    pthread_t workers[num_threads];

    print_request(request);
    printf("num requests: %d, num threads: %d\n", num_requests, num_threads);

    start_usec = get_time_usecs();

    for(i=0; i<num_threads; i++) {
    	// Set up benchmark parameters to be passed to workers
    	benchmark_t* bench = (benchmark_t*)malloc(sizeof(benchmark_t));
    	bench->request = request;
    	bench->destination = destination;
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
    printf("Total time: %.4f\n", diff);
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
