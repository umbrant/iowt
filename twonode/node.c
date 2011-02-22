#include <node.h>


void error(const char *msg)
{
    perror(msg);
    exit(1);
}


int main (int argc, char *argv[])
{
    pthread_t manager;
    int rc;

    // Create manager thread, which spans more handlers
    rc = pthread_create(server, NULL, manager_main, NULL);
    if(rc) {
        printf("Error, could not start server manager thread.\n");
        exit(-1);
    }

    // Start off some request workload here

    pthread_exit(NULL);
}


void manager(void *threadid) {

    int sockfd, newsockfd;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    // Initialize socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NUMBER);

    if (bind(sockfd, (struct sockaddr *) &serv_addr,
                sizeof(serv_addr)) < 0) 
        error("ERROR on binding");

    // Set up epoll to watch the socket file descriptor
    epfd = epoll_create(EPOLL_QUEUE_LEN);
    static struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.fd = sockfd;
    int res = epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);



    // Listen for and accept new connections
    listen(sockfd,5);
    clilen = sizeof(cli_addr);

    newsockfd = accept(sockfd, 
            (struct sockaddr *) &cli_addr, 
            &clilen);
    if (newsockfd < 0) 
        error("ERROR on accept");
    bzero(buffer,256);

    // Read, write, and close
    n = read(newsockfd,buffer,255);
    if (n < 0) error("ERROR reading from socket");
    printf("Here is the message: %s\n",buffer);
    n = write(newsockfd,"I got your message",18);
    if (n < 0) error("ERROR writing to socket");
    close(newsockfd);
    close(sockfd);
    pthread_exit(0); 
}

void request_handler(void *threadid)
{
    // Loop and wait for epoll to trigger
    while (1) {
        int nfds = epoll_wait(epfd, events,
                MAX_EPOLL_EVENTS_PER_RUN,
                EPOLL_RUN_TIMEOUT);
        if (nfds < 0) die("Error in epoll_wait!");

        // for each ready socket
        for(int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            //handle_io_on_socket(fd);

            // Handle request here
        }
    }
}

void request_sender(int destination)
{
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    // Lookup server in the destination table
    // should use getaddrinfo, not deprecated gethostbyname
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
            (char *)&serv_addr.sin_addr.s_addr,
            server->h_length);
    serv_addr.sin_port = htons(PORT_NUMBER);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");
    printf("Please enter the message: ");
    bzero(buffer,256);
    fgets(buffer,255,stdin);
    n = write(sockfd,buffer,strlen(buffer));
    if (n < 0) 
        error("ERROR writing to socket");
    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
        error("ERROR reading from socket");
    printf("%s\n",buffer);
    close(sockfd);
    pthread_exit(0); 
}
