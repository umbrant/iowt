#include "node.h"


void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void usage()
{
    printf("node <server/client> [destination]\n");
}


int main (int argc, char *argv[])
{
    
    if(argc < 2) {
        usage();
        exit(1);
    }

    // Server
    if(argv[1] == "server") {

        pthread_t manager;

        // Create manager thread, which spans more handlers
        int rv = pthread_create(&manager, NULL, manager_main, NULL);
        if(rv) {
            printf("Error, could not start server manager thread.\n");
            exit(-1);
        }
    }
    else if(argv[1] == "client") {
        if(argc < 3) {
            usage();
            exit(1);
        }
        int dest = atoi(argv[2]);
        if(dest > NUM_SERVERS-1) {
            printf("Invalid destination server number (must be less than %d)\n",
                    NUM_SERVERS);
            exit(1);
        }
        request_sender(dest);
    }


    // Start off some request workload here

    pthread_exit(NULL);
}


void* manager_main(void *threadid) {

    int sockfd;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int i;

    // Set up epoll to watch the socket file descriptor
    int epfd = epoll_create(EPOLL_QUEUE_LEN);
    static struct epoll_event ev;

    // Start off worker threads
    pthread_t workers[NUM_WORKER_THREADS];
    for(i=0; i<NUM_WORKER_THREADS; i++) {
        int rv = pthread_create(&workers[i], NULL, request_handler, &epfd);
        if(rv) {
            printf("Error, could not start worker thread %d\n", i);
            exit(-1);
        }
    }

    // Initialize socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    memset(&serv_addr, '\0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_INT);

    int rv = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if(rv < 0)
        error("ERROR on binding");

    // Set up socket for listening
    listen(sockfd,10);
    clilen = sizeof(cli_addr);

    // Loop: accept new connections, add them to epoll
    while(1) {
        // accept blocks for new connection
        int newsockfd = accept(sockfd, 
                (struct sockaddr *) &cli_addr, 
                &clilen);
        if (newsockfd < 0) 
            error("ERROR on accept");

        // add connection to epoll
        ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
        ev.data.fd = newsockfd;
        int res = epoll_ctl(epfd, EPOLL_CTL_ADD, newsockfd, &ev);
    }

    close(sockfd);
    pthread_exit(0); 
}

void* request_handler(void *epfd_ptr)
{
    int epfd = (int)&epfd_ptr;
    int i;
    static struct epoll_event events[MAX_EPOLL_EVENTS_PER_RUN];
    memset(events, '\0', sizeof(struct epoll_event)*MAX_EPOLL_EVENTS_PER_RUN);
    // Loop and wait for epoll to trigger
    while (1) {
        int nfds = epoll_wait(epfd, events,
                MAX_EPOLL_EVENTS_PER_RUN,
                EPOLL_RUN_TIMEOUT);
        if (nfds < 0) error("Error in epoll_wait!");

        // for each ready socket
        for(i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            int rv = handle_io_on_socket(fd);
            if(rv<0) {
                printf("Error in handle_io_on_socket: %d\n", rv);
            }
        }
    }
    pthread_exit(0); 
}

int handle_io_on_socket(int fd) {
    // init buffer
    char buffer[256];
    memset(buffer, '\0', 256);

    // Read and write
    int n = read(fd,buffer,255);
    int rv = 0;
    if (n < 0) {
        printf("ERROR reading from socket\n");
        rv = -1;
    }
    printf("Here is the message: %s\n",buffer);

    n = write(fd,"I got your message\n",18);
    if (n < 0) {
        printf("ERROR writing to socket\n");
        rv = -2;
    }

    // close fd
    close(fd);

    return rv;
}

void request_sender(int destination)
{
    int sockfd, n, rv;
    struct addrinfo hints;
    struct addrinfo *result, *rp;


    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket\n");

    // Lookup server in the destination table
    char* server = SERVERS[destination];

    // Convert from human readable -> addrinfo
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    rv = getaddrinfo(server, PORT_STR, &hints, &result);
    if (rv != 0)
    {   
        fprintf(stderr, "error in getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    // Traverse result linked list, until we connect
    for (rp = result; rp != NULL; rp=rp->ai_next) {
        int sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(sockfd == -1)
            continue;
        if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; // Success!
        close(sockfd);
    }

    if(rp == NULL) {
        fprintf(stderr, "Could not connect to server %s\n", server);
    }

    // Have to free the linked list now
    freeaddrinfo(result);

    // Write a message
    char outmsg[] = "Hello world from the client!";
    n = write(sockfd,outmsg,strlen(outmsg));
    if (n < 0) 
        error("ERROR writing to socket");

    // Read the response
    char buffer[256];
    memset(buffer, '\0', 256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
        error("ERROR reading from socket");
    printf("%s\n",buffer);

    close(sockfd);
}
