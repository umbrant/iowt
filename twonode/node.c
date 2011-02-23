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
    socklen_t cli_len;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int i;

    // Initialize socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) 
        error("ERROR opening socket");
    memset(&serv_addr, '\0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_INT);

    int rv = bind(listen_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if(rv < 0)
        error("ERROR on binding");

    // Set up socket for listening
    listen(listen_sock, 5);
    cli_len = sizeof(cli_addr);

    // Set up epoll to watch the socket file descriptor
    epfd = epoll_create(EPOLL_QUEUE_LEN);
    static struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.fd = listen_sock;
    int res = epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &ev);
    if(res == -1) {
        fprintf(stderr, "error in epoll_ctl setting up listener socket\n");
        exit(-1);
    }

    // Loop: wait for new new connections, add them to epoll
    printf("starting manager loop\n");
    while(1) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, EPOLL_TIMEOUT);
        if(nfds == -1) {
            perror("epoll_pwait");
            exit(-1);
        }

        for(i=0; i<nfds; i++) {
            if(events[i].data.fd == listen_sock) {
                int conn_sock = accept(listen_sock, 
                        (struct sockaddr *) &cli_addr, &cli_len);

                if(conn_sock == -1) {
                    perror("accept");
                    exit(-1);
                } 

                // add conn_sock to epoll
                ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
                ev.data.fd = conn_sock;
                int rv = epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &ev);
                if(rv == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(-1);
                }
            } else {
                // Spawn new thread to handle request
                pthread_t temp;
                pthread_create(&temp, NULL, request_handler, events[i].data.fd);
            }
        }
    }

    close(listen_sock);
    pthread_exit(0); 
}

void* request_handler(void *epfd_ptr)
{
    int conn_sock = (int)epfd_ptr;
    char buffer[256];

    // Read from the socket
    int ret = 0;
    int rv = recv(conn_sock, buffer, 255, NULL);
    if (rv < 0) {
        fprintf(stderr, "ERROR reading from socket\n");
        pthread_exit(0);
    } else {
        printf("Here is the message: %s\n", buffer);
    }

    // Write to the socket
    char outmsg[] = "I got your message";
    rv = send(conn_sock, outmsg, strlen(outmsg), NULL);
    
    if(rv == -1) {
        fprintf(stderr, "ERROR writing to socket\n");
        pthread_exit(0);
    }

    pthread_exit(0); 
}

int handle_io_on_socket(int fd) {
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
    printf("Sending: %s\n", outmsg);
    n = write(sockfd, outmsg, strlen(outmsg));
    if (n < 0) 
        error("ERROR writing to socket");
    printf("LOL\n");

    // Read the response
    char buffer[256];
    memset(buffer, '\0', 256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
        error("ERROR reading from socket");
    printf("The response: %s\n",buffer);

    close(sockfd);
}
