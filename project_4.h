#ifndef PROJECT_4_H
#define PROJECT_4_H

#include <stdio.h>
#include <netdb.h>

#define MAX_BUF 8192 //the max size of messages
#define BYTESINMB 1048576 //how many bytes are in a megabyte


struct request {
	char method[8]; //http request method
	char url[2048];
	char http_v[10];
	char host[2048];
	char path[2048];
	char useragent[256];
	char encoding[256];
	char connection[256];
	int has_connection;
	int has_encoding;
};

struct response {
	char http_v[10];
	int status_no;
	char status[256];
	char c_type[256]; //content type
	char c_length[256]; //content length
	int has_type;
	int has_length;
};

struct options {
	int max_conn;
	int max_size;
	int comp_enabled;
	int chunk_enabled;
	int pc_enabled;
};

struct response_block {
	unsigned char *response;
	long size;
	void *next; //NULL if complete
};

struct cache_block {
	char host[2048];
	char path[2048];
	struct response_block *response;
	int lru;
	long size;
	int status_no;
	int has_type;
	char status[256];
	char c_type[256]; //content type
	void *next; //NULL
};

struct thread_params {
	int connfd;
	char hoststr[NI_MAXHOST]; //readable client address
	char portstr[NI_MAXSERV]; //readable client port
};


void *
search_cache(char *host, char *path);

long
remove_cache(long nbytes);

struct cache_block*
add_cache(char *host, char *path, char *reference, long nbytes, struct response res);

int
add_response_block(struct cache_block *c_block_ptr, char *response, long nbytes);

void*
thread_main(void *params);

int
check_cache(char* host, char* path, int connfd, struct timeval* start);

int
parse_response(char *response, struct response *r_ptr);

int
parse_request(char *request, struct request * rptr);

ssize_t
send_request(int servconn, struct request req);

void
handle_request(struct request req, struct thread_params *p);


#endif
