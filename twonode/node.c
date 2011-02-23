#include "node.h"


void* manager_main(void *threadid) {

  	int listen_sock;
  	socklen_t cli_len;
  	char buffer[256];
  	int i;

  	// Initialize socket
  	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  	if (listen_sock < 0) {
    	error("ERROR opening listener socket");
    }
    // Set socket to release bind
    int optVal = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, 
    		(void*)&optVal, (socklen_t)sizeof(optVal));

	struct sockaddr_in serv_addr;
  	serv_addr.sin_family = AF_INET;
  	serv_addr.sin_port = htons(PORT_INT);
  	serv_addr.sin_addr.s_addr = INADDR_ANY;

  	int rv = bind(listen_sock, 
  			(struct sockaddr *) &serv_addr, sizeof(serv_addr));
  	if(rv < 0) {
    	error("ERROR on binding listener socket");
    }

  	// Set up socket for listening
  	rv = listen(listen_sock, 5);
  	if(rv < 0) {
  		error("ERROR on listening on listener socket");
  	}
    //set_nonblocking(listen_sock);

  	// Set up epoll to watch the socket file descriptor
  	epfd = epoll_create(EPOLL_QUEUE_LEN);
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
  	// Each worker has their own epoll fd, loadbalance among them
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

  	// Loop: wait for new new connections, add them to epoll
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
        		printf("Adding conn_sock (%d) to worker epfd (%d)\n",
        				conn_sock, worker_epfds[counter]);
        		int rv = epoll_ctl(worker_epfds[counter++], 
        				EPOLL_CTL_ADD, conn_sock, &ev);
        		if(rv == -1) {
          			perror("epoll_ctl: conn_sock");
          			exit(-1);
        		}
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
			exit(-1);
		}
    	for(i=0; i<nfds; i++) {
    		int sockfd = events[i].data.fd;
  			char buffer[256];
  			// Read from the socket
  			int ret = 0;
  			int rv = recv(sockfd, buffer, 256, 0);
  			if (rv < 0) {
    			error("ERROR reading from socket\n");
  			} else {
    			printf("Here is the message (%d): %s\n", epfd, buffer);
  			}
  			// Write to the socket
  			char outmsg[] = "I got your message";
  			rv = send(sockfd, outmsg, strlen(outmsg), 0);

  			if(rv == -1) {
    			error("ERROR writing to socket\n");
  			}
  			close(sockfd);
    	}
	}

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
    	exit(-1);
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
  	/*
  	char buffer[256];
  	memset(buffer, '\0', 256);
  	n = read(sockfd,buffer,255);
  	if (n < 0) 
    	error("ERROR reading from socket");
  	printf("The response: %s\n",buffer);
  	*/

  	close(sockfd);
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


void error(const char *msg)
{
  	perror(msg);
  	exit(1);
}


void usage()
{
  	printf("node <server/client> [destination]\n");
}


int set_nonblocking(int sockfd) {
	int flags;
	if((flags = fcntl(sockfd, F_GETFL, 0)) == -1) {
		flags = 0;
	}
	return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}
