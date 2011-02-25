#include "node.h"



int send_request(request_t request, int destination)
{
    int sockfd, n, rv;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

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
