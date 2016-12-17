/*
 * project_4 -- cache proxy server
 *
 * The fourth and final project of the 2016 Fall Semester course
 * CSI4106-01: Computer Networks at Yonsei University.
 *
 * Author: Vincent Au (2016840200)
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>

#include "time.h"
#include "network.h"

#define MAX_BUF 8192 //the max size of messages
#define BYTESINMB 1048576 //how many bytes are in a megabyte


struct request {
	char method[8]; //http request method
	char url[2048];
	char http_v[10];
	char host[2048];
	char path[2048];
	char useragent[256];
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

struct thread_params {
	int connfd;
	char hoststr[NI_MAXHOST]; //readable client address
	char portstr[NI_MAXSERV]; //readable client port
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


int
parse_response(char *response, struct response *r_ptr);

int
parse_request(char *request, struct request * rptr);

ssize_t
send_request(int servconn, struct request req);

void
handle_request(struct request req, struct thread_params *p);


const char *ERROR_MSG = "HTTP/1.1 403 Forbidden\r\n\r\n";

int count = 0; //total number of requests
int thread_count = 0; //total number of running threads
struct options opt;

sem_t mutex; //semaphore for mutual exclusion

void *cache_start = NULL;
void *cache_end = NULL;

int cache_count = 0;
long cache_size = 0;
int lru_count = 0;


void *
search_cache(char *host, char *path)
{
	void *ref_ptr;
	ref_ptr = cache_start;

	while (ref_ptr != NULL) {
		struct cache_block *reference;
		reference = (struct cache_block*) ref_ptr;

		if (strcmp(reference->host, host) == 0 &&
				strcmp(reference->path, path) == 0) {
			reference->lru = ++lru_count;
			return (void*)reference;
		}
		ref_ptr = reference->next;
	}
	return NULL;
}


long
remove_cache(long nbytes)
{
	if (cache_start == NULL ||
			opt.max_size * BYTESINMB - cache_size > nbytes) {
		//there's nothing in the cache or we already have enough space
		return 0;
	}

	void *ref_ptr;
	void *prev_ptr = NULL;
	void *min_prev_ptr = NULL;

	struct cache_block *min;
	struct cache_block *curr_c_block;

	//we want to find the item with the lowest LRU and make sure we can
	//free up at least nbytes worth of space

	//set the first element as the minimum
	min = (struct cache_block*) cache_start;
	ref_ptr = min->next;

	//while there is a next
	while (ref_ptr != NULL) {
		curr_c_block = (struct cache_block*)ref_ptr;

		if (curr_c_block->lru < min->lru) {
			min = curr_c_block;
			min_prev_ptr = prev_ptr;
		}

		prev_ptr = ref_ptr;
		ref_ptr = curr_c_block->next;
	}

	//we should have found min by now
	long space_freed = min->size;
	cache_size -= space_freed;
	if (min_prev_ptr == NULL) {
		//first element is the smallest
		cache_start = min->next;
	} else {
		//set min's prev, to min's next
		((struct cache_block*)min_prev_ptr)->next = min->next;
		if (min->next == NULL) {
			//the min is at the end so update cache_end
			cache_end = min_prev_ptr;
		}
	}

	cache_count--;
	if (cache_count == 0) {
		cache_end = NULL;
	}

	
	struct timeval tv;
	gettimeofday(&tv, NULL);

	printf("################# CACHE REMOVED #################\n");
	printf("> %s%s %.2fMB @ ", min->host, min->path,
			(float)min->size/BYTESINMB);
	print_time(&tv);
	printf("> This file has been removed due to LRU!\n");

	free(min->response);
	free(min);

	return space_freed + remove_cache(nbytes-space_freed);
}

struct cache_block*
add_cache(char *host, char *path, char *reference, long nbytes,
		struct response res)
{
	if (opt.max_size > 0 && cache_size + nbytes > opt.max_size * 1000000) {
		remove_cache(nbytes);
	}

	unsigned char* response_text = malloc(sizeof(char)*nbytes+1);
	if (response_text == NULL) {
		perror("Failed to allocate memory for response text");
		exit(1);
	}
	memcpy(response_text, reference, nbytes);

	struct response_block *r_block = malloc(sizeof(struct response_block));
	if (r_block == NULL) {
		perror("Failed to allocate memory for cache's response block");
		exit(1);
	}
	r_block->response = response_text;
	r_block->size = nbytes;
	r_block->next = NULL;

	struct cache_block *c_block = malloc(sizeof(struct cache_block));
	if (c_block == NULL) {
		perror("Failed to allocate memory for cache block");
		exit(1);
	}
	strcpy(c_block->host, host);
	strcpy(c_block->path, path);
	c_block->response = r_block;
	c_block->lru = ++lru_count;
	c_block->size = nbytes;
	c_block->status_no = res.status_no;
	c_block->has_type = res.has_type;
	strcpy(c_block->status, res.status);
	strcpy(c_block->c_type, res.c_type);
	c_block->next = NULL;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	float size = res.has_length ? atof(res.c_length) : (float)nbytes;

	printf("################## CACHE ADDED ##################\n");
	printf("> %s%s %.2fMB @ ", host, path, size/BYTESINMB);
	print_time(&tv);
	printf("> This file has been added to the cache\n");
	printf("#################################################\n");

	cache_size += nbytes;
	cache_count++;

	if (cache_start == NULL) {
		cache_start = c_block;
		cache_end = c_block;
	} else {
		((struct cache_block*)cache_end)->next = c_block;
		cache_end = c_block;
	}

	return c_block;
}


int
add_response_block(struct cache_block *c_block_ptr, char *response, long nbytes)
{
	if (opt.max_size > 0 && cache_size + nbytes > opt.max_size * BYTESINMB) {
		remove_cache(nbytes);
	}

	struct response_block* curr = c_block_ptr->response;
	while (curr->next != NULL) {
		curr = (struct response_block*)curr->next;
	}
	struct response_block* n_block = malloc(sizeof(struct response_block));
	if (n_block == NULL) {
		perror("Failed to allocate memory for additional response block");
		exit(1);
	}
	unsigned char* response_text = malloc(sizeof(char)*nbytes+1);
	if (response_text == NULL) {
		perror("Failed to allocate memory for response text");
		exit(1);
	}
	memcpy(response_text, response, nbytes);

	n_block->response = response_text;
	n_block->size = nbytes;
	n_block->next = NULL;
	curr->next = n_block;
	c_block_ptr->size += nbytes;
	cache_size += nbytes;

	return 0;
}


void*
thread_main(void *params)
{
	/* Cast the pointer to the correct type. */ 
    struct thread_params *p = (struct thread_params*) params; 
	pthread_detach(pthread_self());

	char buf[MAX_BUF]; //buffer for messages
	int nbytes; //the number of received bytes
	struct request req;
	memset(&req, 0, sizeof(req));

	if ((nbytes = recv(p->connfd, buf, MAX_BUF, 0)) > 0) {
		//we received a request!
		if (parse_request(buf, &req) != -1 && strcmp(req.method, "GET") == 0) {
			handle_request(req, p);
		} else {
			//Return a 403 Forbidden error if they attempt to load
			//something needing SSL/HTTPS
			write(p->connfd, ERROR_MSG, strlen(ERROR_MSG));
			close(p->connfd);
		}
	}

	//sem_wait(&thread_count);
	sem_wait(&mutex);
	thread_count--;
	sem_post(&mutex);
	free(params);
	return NULL;
}

int
check_cache(char* host, char* path, int connfd, struct timeval* start) {
	void* res = search_cache(host, path);
	if (res == NULL) return 0;

	struct cache_block* c_block = (struct cache_block*)res;
	c_block->lru = ++lru_count; //update recently used count

	struct response_block* r_block = c_block->response;
	write(connfd, r_block->response, r_block->size);
	while (r_block->next != NULL) {
		r_block = (struct response_block*)(r_block->next);
		write(connfd, r_block->response, r_block->size);
	}
	struct timeval end;
	gettimeofday(&end, NULL);

	printf("@@@@@@@@@@@@@@@@@@ CACHE HIT @@@@@@@@@@@@@@@@@@@@\n");
	printf("[CLI <== PRX --- SRV] @ ");
	print_time(&end);
	printf("> %d %s\n", c_block->status_no, c_block->status);
	if (c_block->has_type) {
		printf("> %s\n", c_block->c_type);
	}
	printf("# %ldms\n", ms_elapsed(start, &end));
	return 1;
}


/*
 * Traverses <response> and stores attribute information in the global
 * variable <res>. Returns the length of the response header.
 */
int
parse_response(char *response, struct response *r_ptr)
{
	//printf("\n\nPARSE RESPONSE: <%s>\n\n", response);
	r_ptr->has_length = 0;
	r_ptr->has_type = 0;
	//scan the method and url into the pointer
	if (sscanf(response, "%s %d %[^\r\n]\r\n", r_ptr->http_v,
			&r_ptr->status_no, r_ptr->status) < 3) {
		return 0;
	}

	char *token, *string, *tofree;
	tofree = string = strdup(response);
	//loop through the request line by line (saved to token)
	while ((token = strsep(&string, "\r\n")) != NULL) {
		if (strncmp(token, "Content-Type: ", 14) == 0) {
			char *type = token + 14;
			strncpy(r_ptr->c_type, type, sizeof(r_ptr->c_type));
			r_ptr->has_type = 1;
		}
		else if (strncmp(token, "Content-Length: ", 16) == 0) {
			char *len = token + 16;
			strncpy(r_ptr->c_length, len, sizeof(r_ptr->c_length));
			r_ptr->has_length = 1;
		}
		else if (strlen(token) == 0) {
			//we've reached the end of the header, expecting body now
			break;
		}
		//skip over the \n character and break when we reach the end
		if (strlen(string) <= 2) {
			break;
		}
		string += 1;
	}
	free(tofree);

	//calculate header length (the plus one for the \n i believe)
	long header_length = strlen(response) - strlen(string) + 1;
	return header_length;
}

/*
 * Reads through the request and extracts any useful information
 * into the global struct request <req> variable.
 * Returns 0 if successful, -1 otherwise.
 */
int
parse_request(char *request, struct request * rptr)
{
	//printf("\n\nPARSE REQUEST: <%s>\n\n", request);
	//scan the method and url into the pointer
	if (sscanf(request, "%s %s %s\r\n", rptr->method, rptr->url,
				rptr->http_v) < 3) {
		return -1;
	}

	char *token, *string, *tofree;
	tofree = string = strdup(request);
	//loop through the request line by line (saved to token)
	while ((token = strsep(&string, "\r\n")) != NULL) {
		if (strncmp(token, "Host: ", 6) == 0) {
			char *host = token + 6;
			strncpy(rptr->host, host, sizeof(rptr->host));

			char *path_offset = strstr(rptr->url, host);
			path_offset+=strlen(host);
			strncpy(rptr->path, path_offset, sizeof(rptr->path));
		}
		else if (strncmp(token, "User-Agent: ", 12) == 0) {
			char *userag = token + 12;
			strncpy(rptr->useragent, userag, sizeof(rptr->useragent));
		}
		else if (strlen(token) == 0) {
			//we've reached the end of the header, expecting body now
			break;
		}
		//skip over the \n character and break when we reach the end
		if (strlen(string) <= 2) {
			break;
		}
		string += 1;
	}
	free(tofree);

	return 0;
}

/*
 * Generates a custom request and sends it to the socket at <servconn>.
 */
ssize_t
send_request(int servconn, struct request req)
{
	char request[2048];
	char *ua = req.useragent;
	char *host = req.host;
	char *path = req.path;

	snprintf(request, sizeof(request),
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"User-Agent: %s\r\n"
			"\r\n", path, host, ua);

	struct timeval tv;
	gettimeofday(&tv, NULL);

	printf("[CLI --- PRX ==> SRV] @ ");
	print_time(&tv);
	printf("> GET %s%s\n", host, path);
	printf("> %s\n", ua);
	//printf("%s\n", request);

	return write(servconn, request, strlen(request));
}

/*
 * Actually process the request. Ensures that we actually send and receive all
 * the required bytes.
 */
void
handle_request(struct request req, struct thread_params *p)
{
	struct timeval start;
	gettimeofday(&start, NULL);

	sem_wait(&mutex);
	printf("-----------------------------------------------\n");
	printf("%d [Conn: %d/%d] [Cache: %.2f/%dMB] [Items: %d]\n\n",
			++count, thread_count, opt.max_conn, (float)cache_size/BYTESINMB,
			opt.max_size, cache_count);
	printf("[CLI connected to %s:%s]\n", p->hoststr, p->portstr);
	printf("[CLI ==> PRX --- SRV] @ ");
	print_time(&start);
	printf("> GET %s%s\n", req.host, req.path);
	printf("> %s\n", req.useragent);
	sem_post(&mutex);

	int connfd = p->connfd;

	//if it's in the cache serve it from there
	if (check_cache(req.host, req.path, connfd, &start)) {
		close(connfd);
		printf("[CLI disconnected]\n");
		return;
	}

	char *host = req.host;
	int servconn = connect_host(host);

	printf("################## CACHE MISS ###################\n");
	printf("[SRV connected to %s:80]\n", host);
	if (send_request(servconn, req) == -1) {
		perror("Error writing to socket");
		exit(1);
	}

	char buf[MAX_BUF]; //buffer for messages
	int nbytes; //the number of received bytes
	long bytes_in = 0;
	long bytes_out = 0;
	long header_length;

	struct response res;
	struct cache_block* c_block_ptr;

	struct timeval tv;

	nbytes = recv(servconn, buf, MAX_BUF,0);
	bytes_in += nbytes;

	if (nbytes > 0) {
		header_length = parse_response(buf, &res);
		bytes_out += write(connfd, buf, nbytes);

		gettimeofday(&tv, NULL);
		printf("[CLI --- PRX <== SRV] @ ");
		print_time(&tv);
		printf("> %d %s\n", res.status_no, res.status);
		printf("> %s\n", res.c_type);


		if (res.has_length) {

			//add this to the cache
			sem_wait(&mutex);
			c_block_ptr = add_cache(req.host, req.path, buf, nbytes, res);
			sem_post(&mutex);

			//we know exactly how many bytes we're expecting
			long bytes_left = atoll(res.c_length);
			bytes_left -= (nbytes - header_length);

			while (bytes_left > 0) {
				memset(&buf, 0, sizeof(buf));
				nbytes = recv(servconn, buf, MAX_BUF,0);
				bytes_in += nbytes;
				bytes_out += write(connfd, buf, nbytes);
				bytes_left -= nbytes;

				//add this to cache too
				sem_wait(&mutex);
				add_response_block(c_block_ptr, buf, nbytes);
				sem_post(&mutex);
			}
		}
		else {

			if (opt.chunk_enabled) {
				//add this to the cache
				sem_wait(&mutex);
				c_block_ptr = add_cache(req.host, req.path, buf, nbytes, res);
				sem_post(&mutex);
			}

			//we have no idea how many bytes to expect... uh oh
			memset(&buf, 0, sizeof(buf));
			while ((nbytes = recv(servconn, buf, MAX_BUF,0)) > 0) {
				bytes_in += nbytes;
				bytes_out += write(connfd, buf, nbytes);

				if (opt.chunk_enabled) {
					//add this to cache too
					sem_wait(&mutex);
					add_response_block(c_block_ptr, buf, nbytes);
					sem_post(&mutex);
				}

				//check the last five characters to see if it's terminated
				if (strcmp(&buf[nbytes-5], "0\r\n\r\n") == 0) {
					break;
				}
				memset(&buf, 0, sizeof(buf));
			}
		}

		gettimeofday(&tv, NULL);
		printf("[CLI <== PRX --- SRV @ ");
		print_time(&tv);
		printf("> %d %s\n", res.status_no, res.status);
		printf("> %s\n", res.c_type);
		printf("# %ldms\n", ms_elapsed(&start, &tv));
	}
	close(connfd);
	printf("[CLI disconnected]\n");

	close(servconn);
	printf("[SRV disconnected]\n");
	return;
}


int
main(int argc, char **argv)
{
	//make sure we have the right number of arguments
	if (argc < 4) {
		//remember: the name of the program is the first argument
		fprintf(stderr, "ERROR: Missing required arguments!\n");
		printf("Usage: %s <port> <maxConn> <maxSize>\n", argv[0]);
		printf("e.g. %s 9001 20 16\n", argv[0]);
		exit(1);
	}

	char *port = argv[1]; //port we're listening on
	opt.max_conn = atol(argv[2]); //max no. connections
	opt.max_size = atol(argv[3]); //max cache size

	opt.comp_enabled = 0; //compression enabled
	opt.chunk_enabled = 0; //chunking enabled
	opt.pc_enabled = 0; //persistant connection enabled

	//check for optional arguments
	for (int i = 4; i < argc; i++) {
		if (strcmp(argv[i], "-comp") == 0) {
			opt.comp_enabled = 1;
		} else if (strcmp(argv[i], "-chunk") == 0) {
			opt.chunk_enabled = 1;
		} else if (strcmp(argv[i], "-pc") == 0) {
			opt.pc_enabled = 1;
		}
	}

	int connfd;
	int listener; //file descriptor of listening socket
	struct sockaddr_storage their_addr; //connector's address info
	socklen_t sin_size;

	//set up the server on the specified port
	setup_server(&listener, port);

	printf("Starting proxy server on port %s\n", port);

	sem_init(&mutex, 0, 1);

	while(1) {
		sin_size = sizeof(their_addr);
		connfd = accept(listener, (struct sockaddr *) &their_addr,
				&sin_size);
		if (connfd == -1) {
			perror("ERROR: accept() failed");
			continue;
		}

		//don't create a new thread if we already have too many running
		while (opt.max_conn > 0) {
			sem_wait(&mutex);
			if (thread_count < opt.max_conn) break;
			sem_post(&mutex);
		}

		pthread_t thread_id;
		struct thread_params *params = malloc(sizeof(struct thread_params));
		if (params == NULL) {
			perror("Couldn't allocate memory for thread parameters");
			exit(1);
		}
		params->connfd = connfd;

		//store the ip address and port into params too
		getnameinfo((struct sockaddr *)&their_addr, sin_size, params->hoststr,
				sizeof(params->hoststr), params->portstr, sizeof(params->portstr),
				NI_NUMERICHOST | NI_NUMERICSERV);

		sem_wait(&mutex);
		thread_count++;
		sem_post(&mutex);
		pthread_create(&thread_id, NULL, &thread_main, (void*) params);

	}
	close(listener);
	return 0;
}

