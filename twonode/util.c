#include "node.h"



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
    printf("request size: ");
    switch(request.size)
    {
        case(SIZE_64):
            printf("64");
            break;
        case(SIZE_256):
            printf("256");
            break;
    }
    printf(", ");
    printf("compression: ");
    switch(request.compression)
    {
        case(COMPRESSION_NONE):
            printf("none");
            break;
        case(COMPRESSION_GZIP):
            printf("gzip");
            break;
        case(COMPRESSION_LZO):
            printf("lzo");
            break;
    }
    printf(", ");
    printf("storage: ");
    switch(request.storage)
    {
        case(STORAGE_DISK):
            printf("disk");
            break;
        case(STORAGE_MEMORY):
            printf("mem");
            break;
    }
    printf("\n");
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
    printf("node client <destination> <size> <compression> <storage>\n");
    printf("destination: integer index of server in SERVERS (see iowt.cfg)\n");
    printf("size: 64 or 256\n");
    printf("compression: none or gzip or lzo\n");
    printf("storage: memory or disk\n");
    printf("\n");
    printf("Benchmark mode chooses the first server in SERVERS by default.\n");
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
