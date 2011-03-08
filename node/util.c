#include "node.h"


int main (int argc, char *argv[])
{
    int rv;
    // Read and initialize configuration settings
    init_config();


	request_t request;
	int dest;

    // Get local machine's eth0 ip address
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


    if(argc < 3) {
        usage();
        exit(1);
    }
    // Server
    if(strcmp(argv[1],"server") == 0) {
        // Init the mutexs used for determining filename
        pthread_mutex_init(&filecount_64_mutex, NULL);
        pthread_mutex_init(&filecount_256_mutex, NULL);
        // Init their counters too
        filecount_64_1 = 'a';
        filecount_64_2 = 'a';
        filecount_256 = 'a';
        NUM_WORKER_THREADS = atoi(argv[2]);
        pthread_t manager;
        // Create manager thread, which spans more handlers
        rv = pthread_create(&manager, NULL, manager_main, NULL);
        if(rv) {
            printf("Error, could not start server manager thread.\n");
            exit(-1);
        }
        pthread_join(manager, NULL);
    }
    // Client
    else if(strcmp(argv[1],"client") == 0) {
		make_request(argc, argv, &request, &dest);
        send_request(request, dest);
    }
    // Benchmark
    else if(strcmp(argv[1], "benchmark") == 0) {
        // Init crypt mutex
        pthread_mutex_init(&crypt_mutex, NULL);
        if(argc != 7) {
            usage();
        } else {
            // We have to pad argv/argc with a dummy destination
            // so make_request gets what it expects
            int targc = 8;
            char dummy_char = '0';
            char* targv[8];
            targv[0] = argv[0];
            targv[1] = &dummy_char; // unused
            targv[2] = argv[1];
            targv[3] = argv[2];
            targv[4] = argv[3];
            targv[5] = argv[4];
            targv[6] = argv[5];
            targv[7] = argv[6];
		    make_request(targc, targv, &request, &dest);
            int num_requests = atoi(argv[5]);
            int num_threads = atoi(argv[6]);
            benchmark(request, num_requests, num_threads);
        }
    }
    // Default to showing usage()
    else {
        usage();
    }
    pthread_exit(NULL);
}


int make_request(int argc, char* argv[], request_t* request, int* destination) {
    int dest, size, compression, storage;
    if(argc < 6) {
        usage();
        exit(1);
    }
    dest = atoi(argv[2]);
    if(dest > NUM_SERVERS-1 || dest < 0) {
        printf("Invalid destination server number (must be between 0 and %d)\n",
                NUM_SERVERS);
        exit(1);
    }
	// size
	if(strcmp(argv[3], "64") == 0)
		size = SIZE_64;
	else if(strcmp(argv[3], "256") == 0)
		size = SIZE_256;
	else {
		usage();
		printf("\nInvalid size: %s\n", argv[3]);
		exit(1);
	}
	// compression
	if(strcmp(argv[4], "none")==0)
		compression = COMPRESSION_NONE;
	else if(strcmp(argv[4], "gzip")==0)
		compression = COMPRESSION_GZIP;
	else if(strcmp(argv[4], "lzo")==0)
		compression = COMPRESSION_LZO;
	else {
		usage();
		printf("\nInvalid compression: %s\n", argv[4]);
		exit(1);
	}
	// storage
	if(strcmp(argv[5], "disk") == 0)
		storage = STORAGE_DISK;
	else if(strcmp(argv[5], "memory") == 0)
		storage = STORAGE_MEMORY;
	else {
		usage();
		printf("\nInvalid storage: %s\n", argv[5]);
		exit(1);
	}

	request->size = size;
	request->compression = compression;
	request->storage = storage;

	*destination = dest;

	return 0;
}


void print_request(request_t request) {
    char request_str[1024];
    memset(request_str, '\0', 1024);

    strcat(request_str, "request size: ");
    switch(request.size)
    {
        case(SIZE_64):
            strcat(request_str, "64");
            break;
        case(SIZE_256):
            strcat(request_str, "256");
            break;
    }
    strcat(request_str, ", ");
    strcat(request_str, "compression: ");
    switch(request.compression)
    {
        case(COMPRESSION_NONE):
            strcat(request_str, "none");
            break;
        case(COMPRESSION_GZIP):
            strcat(request_str, "gzip");
            break;
        case(COMPRESSION_LZO):
            strcat(request_str, "lzo");
            break;
    }
    strcat(request_str, ", ");
    strcat(request_str, "storage: ");
    switch(request.storage)
    {
        case(STORAGE_DISK):
            strcat(request_str, "disk");
            break;
        case(STORAGE_MEMORY):
            strcat(request_str, "mem");
            break;
    }
    printf("%s\n", request_str);
}


int init_config()
{
    char filename[] = "iowt.cfg";
    PRINTF("Loading config file %s...\n", filename);
    int rv;
    config_t config;
    config_setting_t* config_setting;

    config_init(&config);
    if(!config_read_file(&config, filename)) {
        printf("Error reading line %d: %s\n", config_error_line(&config),
                config_error_text(&config));
        exit(-1);
    }
    // Get FILE_DIR
    int config_lookup_string (const config_t * config, const char * path, const char ** value);
    rv = config_lookup_string(&config, "file_dir", &FILE_DIR);
    PRINTF("FILE_DIR is %s\n", FILE_DIR);
    // Get SERVERS list
    config_setting = config_lookup (&config, "servers");
    // Loop through once to count, malloc, loop again to populate
    char* temp;
    int count = -1;
    do {
        count++;
        temp = (char*)config_setting_get_string_elem(config_setting, count);
    } while(temp != NULL);
    SERVERS = (const char**)malloc(count*sizeof(char*));
    int i;
    PRINTF("SERVERS list:\n");
    for(i=0; i<count; i++) {
        SERVERS[i] = config_setting_get_string_elem(config_setting, i);
        PRINTF("    [%02d]: %s\n", i, SERVERS[i]);
    }
    NUM_SERVERS = count;

    PRINTF("Done parsing config!\n");

    return 0;
}


void error(const char *msg)
{
    perror(msg);
    exit(1);
}


void usage()
{
    printf("node <server/client/benchmark> [destination]\n");
    printf("\n");
    printf("node server <num_worker_threads>\n");
    printf("\n");
    printf("node client <destination> <size> <compression> <storage>\n");
    printf("destination: integer index of server in SERVERS (see iowt.cfg)\n");
    printf("size: 64 or 256\n");
    printf("compression: none or gzip or lzo\n");
    printf("storage: memory or disk\n");
    printf("\n");
    printf("node benchmark <size> <compress> <storage> <requests> <threads>\n");
    printf("Benchmark mode takes almost the same arguments as client, and also the desired number of requests and threads to use.\n");
    printf("Benchmark threads randomly pick servers from SERVERS to connect to.\n");
    printf("\n");
}


int set_nonblocking(int sockfd) {
    int flags;
    if((flags = fcntl(sockfd, F_GETFL, 0)) == -1) {
        flags = 0;
    }
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}


void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


void flush_page_cache() {
    //return;
    printf("Flushing read cache\n");
    if(system("echo 1 > /proc/sys/vm/drop_caches") == -1) {
        error("system() drop_caches failed!");
    }
}
