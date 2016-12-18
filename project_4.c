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
#include "cache.h"
#include "project_4.h"

const char* ERROR_MSG = "HTTP/1.1 403 Forbidden\r\n\r\n";

int count = 0; //total number of requests
int thread_count = 0; //total number of running threads
struct options opt;

sem_t mutex; //semaphore for mutual exclusion

/*
 * Make at least nbytes of free space in the cache
 */
int
make_space(long nbytes) {
	//return if we couldn't fit nbytes of data even if we tried
	if (!could_fit(nbytes)) return -1;

	//keep removing the lowest LRU until we good
	while (!can_fit(nbytes)) {
		C_block* min = find_lru();
		if (min == NULL) {
			return -1;
		}

		struct timeval tv;
		gettimeofday(&tv, NULL);

		printf("################# CACHE REMOVED #################\n");
		printf("> %s%s %.2fMB @ ", min->host, min->path,
				(float)min->size/BYTESINMB);
		print_time(&tv);
		printf("> This file has been removed due to LRU!\n");
		free_cache_block(min);
	}

	return 0;
}

C_block*
safe_add_cache(char* host, char* path, char* res_text, long nbytes, struct response res)
{
	long total_size = res.has_length ? atof(res.c_length) : nbytes;
	struct timeval tv;

	//if we can't fit the entire file then just return
	if (!could_fit(total_size)) {
		printf("################## CACHE SKIP ###################\n");
		printf("> %s%s %.2fMB @ ", host, path, (float)total_size/BYTESINMB);
		printf("> This file is too big for the cache!\n");
		gettimeofday(&tv, NULL);
		print_time(&tv);
		printf("#################################################\n");
		return NULL;
	}

	//we can fit the entire file but we'll need to free up some space first
	if (!can_fit(total_size) && make_space(total_size) == -1) {
		return NULL;
	}

	C_block* block = add_cache(host, path, res_text, nbytes, res.status_no, res.status, res.has_type, res.c_type);
	if (block != NULL) {
		printf("################## CACHE ADDED ##################\n");
		printf("> %s%s %.2fMB @ ", host, path, (float)total_size/BYTESINMB);
		gettimeofday(&tv, NULL);
		print_time(&tv);
		printf("> This file has been added to the cache\n");
		printf("#################################################\n");
	}

	return block;
}


void*
thread_main(void* params)
{
	/* Cast the pointer to the correct type. */ 
    struct thread_params* p = (struct thread_params*) params; 
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
	C_block* c_block = search_cache(host, path);
	if (c_block == NULL) return 0;

	R_block* r_block = c_block->response;
	write(connfd, r_block->text, r_block->size);
	while (r_block->next != NULL) {
		r_block = r_block->next;
		write(connfd, r_block->text, r_block->size);
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
parse_response(char* response, struct response* r_ptr)
{
	//printf("\n\nPARSE RESPONSE: <%s>\n\n", response);
	r_ptr->has_length = 0;
	r_ptr->has_type = 0;
	//scan the method and url into the pointer
	if (sscanf(response, "%s %d %[^\r\n]\r\n", r_ptr->http_v,
			&r_ptr->status_no, r_ptr->status) < 3) {
		return 0;
	}

	char* token, * string, * tofree;
	tofree = string = strdup(response);
	//loop through the request line by line (saved to token)
	while ((token = strsep(&string, "\r\n")) != NULL) {
		if (strncmp(token, "Content-Type: ", 14) == 0) {
			char* type = token + 14;
			strncpy(r_ptr->c_type, type, sizeof(r_ptr->c_type));
			r_ptr->has_type = 1;
		}
		else if (strncmp(token, "Content-Length: ", 16) == 0) {
			char* len = token + 16;
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

	//calculate header length (the plus one for the \n i believe)
	long header_length = strlen(response) - strlen(string) + 1;

	free(tofree);
	return header_length;
}

/*
 * Reads through the request and extracts any useful information
 * into the global struct request <req> variable.
 * Returns 0 if successful, -1 otherwise.
 */
int
parse_request(char* request, struct request* rptr)
{
	//printf("\n\nPARSE REQUEST: <%s>\n\n", request);
	//scan the method and url into the pointer
	if (sscanf(request, "%s %s %s\r\n", rptr->method, rptr->url,
				rptr->http_v) < 3) {
		return -1;
	}

	rptr->has_connection = 0;
	rptr->has_encoding = 0;

	char* token, * string, * tofree;
	tofree = string = strdup(request);
	//loop through the request line by line (saved to token)
	while ((token = strsep(&string, "\r\n")) != NULL) {
		if (strncmp(token, "Host: ", 6) == 0) {
			char* host = token + 6;
			strncpy(rptr->host, host, sizeof(rptr->host));

			char* path_offset = strstr(rptr->url, host);
			path_offset+=strlen(host);
			strncpy(rptr->path, path_offset, sizeof(rptr->path));
		}
		else if (strncmp(token, "Connection: ", 12) == 0) {
			char* conn = token + 12;
			strncpy(rptr->connection, conn, sizeof(rptr->connection));
			rptr->has_connection = 1;
		}
		else if (strncmp(token, "Accept-Encoding: ", 17) == 0) {
			char* enc = token + 17;
			strncpy(rptr->encoding, enc, sizeof(rptr->encoding));
			rptr->has_encoding= 1;
		}
		else if (strncmp(token, "User-Agent: ", 12) == 0) {
			char* userag = token + 12;
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
	char enc[256];
	if (req.has_encoding) {
		snprintf(enc, sizeof enc, "Accept-Encoding: %s\r\n", req.encoding);
	}
	char* extra1 = opt.comp_enabled ? enc : "";

	char conn[256];
	if (req.has_connection) {
		snprintf(conn, sizeof conn, "Connection: %s\r\n", req.encoding);
	}
	char* extra2 = opt.pc_enabled ? conn : "";

	char request[2048];
	snprintf(request, sizeof(request),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"User-Agent: %s\r\n"
			"%s"
			"%s"
			"\r\n", req.path, req.host, req.useragent, extra1, extra2);

	struct timeval tv;
	gettimeofday(&tv, NULL);

	printf("[CLI --- PRX ==> SRV] @ ");
	print_time(&tv);
	printf("> GET %s%s\n", req.host, req.path);
	printf("> %s\n", req.useragent);
	//printf("%s\n", request);

	return write(servconn, request, strlen(request));
}

/*
 * Actually process the request. Ensures that we actually send and receive all
 * the required bytes.
 */
void
handle_request(struct request req, struct thread_params* p)
{
	int connfd = p->connfd;

	struct timeval start;
	gettimeofday(&start, NULL);

	sem_wait(&mutex);
	printf("-----------------------------------------------\n");
	printf("%d [Conn: %d/%d] [Cache: %.2f/%dMB] [Items: %d]\n\n",
			++count, thread_count, opt.max_conn,
			(float)get_current_cache_size()/BYTESINMB,
			opt.max_size, get_cache_count());
	printf("[CLI connected to %s:%s]\n", p->hoststr, p->portstr);
	printf("[CLI ==> PRX --- SRV] @ ");
	print_time(&start);
	printf("> GET %s%s\n", req.host, req.path);
	printf("> %s\n", req.useragent);

	//if it's in the cache serve it from there
	//if found, the LRU is increased which is why we need to have it in
	//a mutex block
	if (check_cache(req.host, req.path, connfd, &start)) {
		close(connfd);
		printf("[CLI disconnected]\n");
		return;
	}
	sem_post(&mutex);

	char* host = req.host;
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

		C_block* c_block;
		int failed = 0;

		if (res.has_length) {

			//add this to the cache
			sem_wait(&mutex);
			c_block = safe_add_cache(req.host, req.path, buf, nbytes, res);
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
				if (c_block != NULL) {
					failed |= add_response_block(c_block, buf, nbytes);
				}
				sem_post(&mutex);
			}
		}
		else {

			if (opt.chunk_enabled) {
				//add this to the cache
				sem_wait(&mutex);
				c_block = safe_add_cache(req.host, req.path, buf, nbytes, res);
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
					if (c_block != NULL) {
						failed |= add_response_block(c_block, buf, nbytes);
					}
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

		if (failed) {
			sem_wait(&mutex);
			free_cache_block(c_block);
			sem_post(&mutex);
		}
	}
	close(connfd);
	printf("[CLI disconnected]\n");

	close(servconn);
	printf("[SRV disconnected]\n");
	return;
}


int
main(int argc, char** argv)
{
	//make sure we have the right number of arguments
	if (argc < 4) {
		//remember: the name of the program is the first argument
		fprintf(stderr, "ERROR: Missing required arguments!\n");
		printf("Usage: %s <port> <maxConn> <maxSize>\n", argv[0]);
		printf("e.g. %s 9001 20 16\n", argv[0]);
		exit(1);
	}

	char* port = argv[1]; //port we're listening on
	opt.max_conn = atol(argv[2]); //max no. connections
	opt.max_size = atol(argv[3]); //max cache size
	set_max_cache_size(opt.max_size);

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
		connfd = accept(listener, (struct sockaddr*) &their_addr,
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
		struct thread_params* params = calloc(1, sizeof(struct thread_params));
		if (params == NULL) {
			perror("Couldn't allocate memory for thread parameters");
			exit(1);
		}
		params->connfd = connfd;

		//store the ip address and port into params too
		getnameinfo((struct sockaddr* )&their_addr, sin_size, params->hoststr,
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

