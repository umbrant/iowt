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

    int bufsize = request.size * (1<<20);
    char *buffer = (char*)calloc(1, bufsize);

    start_usecs = get_time_usecs();

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

    int bytes_read = 0;
    // Read the response, stream decompression if necessary
    switch(request.compression) {
        case COMPRESSION_NONE:
            bytes_read = read_uncompressed(sockfd, buffer, bufsize);
            break;
        case COMPRESSION_GZIP:
            break;
        case COMPRESSION_LZO:
            break;
    }

    end_usecs = get_time_usecs();

    free(buffer);

    PRINTF("response was size %d\n", bytes_read);

    close(sockfd);
    return 0;
}


/* 
 * We can read directly into the final output buffer, since
 * no decompression needs to be done.
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
 * gzip is a streaming decompression protocol, so use a READ_CHUNKSIZE read buffer,
 * decompress it to the final output buffer, and then repeat.
 *
 * This will probably need to be optimized, to get the network streaming and gzip
 * streaming working together without blocking each other. Maybe spawn another thread?
 */
int read_gzip(int sockfd, char* buffer, int bufsize)
{
    int rv = 0;
    int bytes_read = 0;
    int bytes_deflated = 0;
    int bytes_to_read = READ_CHUNKSIZE;
    char read_buffer[READ_CHUNKSIZE];

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    rv = inflateInit(&strm);
    if(rv != Z_OK) {
        error("zlib deflateInit");
    }

    int n = 0;
    do {
        // Read bytes_to_read bytes into read_buffer
        n = recv(sockfd, read_buffer, bytes_to_read*sizeof(char), MSG_WAITALL);
        if(n < 0)
            error("ERROR reading from socket");
        // Point zlib at the read and write buffers, with right sizes
        strm.next_in = (unsigned char*)read_buffer;
        strm.avail_in = n;
        strm.next_out = (unsigned char*)(buffer+bytes_read);
        strm.avail_out = bufsize - bytes_read;
        // Inflate to the output buffer
        rv = inflateEnd(&strm);
        if(rv == Z_STREAM_ERROR) {
            error("zlib stream error");
        }
        
        bytes_deflated += READ_CHUNKSIZE - strm.avail_out;
        bytes_read += n;
    } while(n == bytes_to_read && bytes_read < bufsize);

    // clean up the stream
    inflateEnd(&strm);
    if(bytes_read != bytes_deflated) {
        printf("MISMATCH: Read: %d Deflated: %d\n", bytes_read, bytes_deflated);
    }

    return bytes_read;
}


int read_lzo(int sockfd, char* buffer, int bufsize)
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
    long start_usec, end_usec;
    pthread_t workers[NUM_BENCH_THREADS];

    start_usec = get_time_usecs();

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

    end_usec = get_time_usecs();

    long diff_usec = end_usec - start_usec;
    double diff = (double)diff_usec / (double)1000000;

    double req_per_sec = (double)(NUM_BENCH_REQUESTS) / (double)diff;
    printf("start: %ld end: %ld diff: %ld\n", start_usec, 
            end_usec, diff_usec);
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
