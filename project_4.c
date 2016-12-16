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
#include <netdb.h>
#include <unistd.h>

#define BACKLOG 10 //how many pending connections the queue will hold
#define MAX_BUF 8192 //the max size of messages


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

int
connect_host(char *hostname);

int
parse_response(char *response, struct response *r_ptr);

int
parse_request(char *request, struct request * rptr);

ssize_t
send_request(int servconn, struct request req);

void
handle_request(struct request req, struct thread_params *p);

void
*get_in_addr(struct sockaddr *sa);

void
setup_server(int *listener, char *port);


const char *ERROR_MSG = "HTTP/1.1 403 Forbidden\r\n\r\n";
int MAX_CONN; //number of maximum concurrent connections
int MAX_CACHE; //maximum size of cache in MB

int count = 0; //total number of requests
int thread_count = 0; //total number of running threads
sem_t mutex; //semaphore for mutual exclusion


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

	sem_wait(&mutex);
	thread_count--;
	sem_post(&mutex);
	return NULL;
}


/*
 * Returns a new socket having connected to <hostname> on port 80
 */
int
connect_host(char *hostname)
{
	//printf("Attempting to connect to: %s\n", hostname);

	struct addrinfo hints, *res, *res0;
	int error;
	int s;
	int yes = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((error = getaddrinfo(hostname, "80", &hints, &res0)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(error));
		exit(1);
	}

	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype,
				res->ai_protocol);
		if (s == -1) {
			perror("ERROR: socket() failed");
			continue;
		}

		//allow port reuse
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			perror("ERROR: setsockopt() failed");
			exit(1);
		}
		//don't crash when writing to closed socket
		if (setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) == -1) {
			perror("ERROR: setsockopt() failed");
			exit(1);
		}

		if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
			perror("ERROR: connect() failed");
			close(s);
			continue;
		}

		break;  /* okay we got one */
	}
	if (s < 0) {
		fprintf(stderr, "Couldn't connect to the host: %s\n", hostname);
		exit(1);
	}
	freeaddrinfo(res0);

	return s;
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

	printf("[CLI --- PRX ==> SRV]\n");
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
	sem_wait(&mutex);
	printf("-----------------------------------------------\n");
	printf("%d [Conn: %d/%d] [Cache: X/%dMB] [Items: X]\n\n",
			++count, thread_count, MAX_CONN, MAX_CACHE);
	printf("[CLI connected to %s:%s]\n", p->hoststr, p->portstr);
	printf("[CLI ==> PRX --- SRV]\n");
	printf("> GET %s%s\n", req.host, req.path);
	printf("> %s\n", req.useragent);
	sem_post(&mutex);

	char *host = req.host;
	int servconn = connect_host(host);
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

	int connfd = p->connfd;

	struct response res;

	nbytes = recv(servconn, buf, MAX_BUF,0);
	bytes_in += nbytes;

	if (nbytes > 0) {
		header_length = parse_response(buf, &res);
		bytes_out += write(connfd, buf, nbytes);

		if (res.has_length) {
			//we know exactly how many bytes we're expecting
			long bytes_left = atoll(res.c_length);
			bytes_left -= (nbytes - header_length);

			while (bytes_left > 0) {
				memset(&buf, 0, sizeof(buf));
				nbytes = recv(servconn, buf, MAX_BUF,0);
				bytes_in += nbytes;
				bytes_out += write(connfd, buf, nbytes);
				bytes_left -= nbytes;
			}
		}
		else {
			//we have no idea how many bytes to expect... uh oh
			memset(&buf, 0, sizeof(buf));
			while ((nbytes = recv(servconn, buf, MAX_BUF,0)) > 0) {
				bytes_in += nbytes;
				bytes_out += write(connfd, buf, nbytes);

				//check the last five characters to see if it's terminated
				if (strcmp(&buf[nbytes-5], "0\r\n\r\n") == 0) {
					break;
				}
				memset(&buf, 0, sizeof(buf));
			}
		}
		printf("[CLI --- PRX <== SRV]\n");
		printf("> %d %s\n", res.status_no, res.status);
		printf("> %s %ldbytes\n", res.c_type, bytes_in);

		printf("[CLI <== PRX --- SRV]\n");
		printf("> %d %s\n", res.status_no, res.status);
		printf("> %s %ldbytes\n", res.c_type, bytes_out);
	}
	close(connfd);
	printf("[CLI disconnected]\n");

	close(servconn);
	printf("[SRV disconnected]\n");
	return;
}

/*
 * Get socket address irrespective of IPv4 or IPv6
 * Shamelessly taken from:
 * http://www.beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 * Credits to Brian "Beej Jorgensen" Hall
 */
void
*get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 * Set up the server on socket <listener> using port <port>
 */
void
setup_server(int *listener, char *port)
{
	//set up the structs we need
	struct addrinfo hints, *p;
	struct addrinfo *servinfo; //will point to the results

	memset(&hints, 0, sizeof(hints)); //make sure the struct is empty
	hints.ai_family = AF_UNSPEC; //IPv4 or IPv6 is OK (protocol agnostic)
	hints.ai_socktype = SOCK_STREAM; //TCP stream sockets
	hints.ai_flags = AI_PASSIVE; //fill in my IP for me

	int status;
	if ((status = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		exit(1);
	}

	//servinfo now points to a linked list of struct addrinfos
	//each of which contains a struct sockaddr of some kind
	int yes = 1;
	for (p = servinfo; p != NULL; p = p->ai_next) {
		*listener = socket(servinfo->ai_family, servinfo->ai_socktype,
				servinfo->ai_protocol);
		if (*listener == -1) {
			perror("ERROR: socket() failed");
			//keep going to see if we can connect to something else
			continue; 
		}

		//allow port reuse
		if (setsockopt(*listener, SOL_SOCKET, SO_REUSEADDR, &yes,
					sizeof(yes)) == -1) {
			perror("ERROR: setsockopt() failed");
			exit(1);
		}

		//don't crash when writing to closed socket
		if (setsockopt(*listener, SOL_SOCKET, SO_NOSIGPIPE, &yes,
					sizeof(yes)) == -1) {
			perror("ERROR: setsockopt() failed");
			exit(1);
		}

		if (bind(*listener, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
			perror("ERROR: bind() failed");
			//keep going to see if we can connect to something else
			continue;
		}

		//we have something, let's move on
		break;
	}

	//free the linked list since we don't need it anymore
	freeaddrinfo(servinfo);

	if (p == NULL) {
		fprintf(stderr, "ERROR: Failed to bind to anything!\n");
		exit(1);
	}

	//listen time
	if (listen(*listener, BACKLOG) == -1) {
		perror("ERROR: listen() failed");
		exit(1);
	}
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
	MAX_CONN = atol(argv[2]); //max no. connections
	MAX_CACHE = atol(argv[3]); //max cache size

	int opt_comp = 0; //compression enabled
	int opt_chunk = 0; //chunking enabled
	int opt_pc = 0; //persistant connection enabled

	//check for optional arguments
	for (int i = 4; i < argc; i++) {
		if (strcmp(argv[i], "-comp") == 0) {
			opt_comp = 1;
		} else if (strcmp(argv[i], "-chunk") == 0) {
			opt_chunk = 1;
		} else if (strcmp(argv[i], "-pc") == 0) {
			opt_pc = 1;
		}
	}

	int connfd;
	int listener; //file descriptor of listening socket
	struct sockaddr_storage their_addr; //connector's address info
	socklen_t sin_size;

	//set up the server on the specified port
	setup_server(&listener, port);

	printf("Starting proxy server on port %s\n", port);

	sem_init(&mutex, 0, 0);

	while(1) {
		sin_size = sizeof(their_addr);
		connfd = accept(listener, (struct sockaddr *) &their_addr,
				&sin_size);
		if (connfd == -1) {
			perror("ERROR: accept() failed");
			continue;
		}

		//don't create a new thread if we already have too many running
		while (MAX_CONN > 0 && thread_count >= MAX_CONN);

		pthread_t thread_id;
		struct thread_params *params = malloc(sizeof(struct thread_params));
		params->connfd = connfd;

		//store the ip address and port into params too
		getnameinfo((struct sockaddr *)&their_addr, sin_size, params->hoststr,
				sizeof(params->hoststr), params->portstr, sizeof(params->portstr),
				NI_NUMERICHOST | NI_NUMERICSERV);

		pthread_create(&thread_id, NULL, &thread_main, (void*) params);

		sem_wait(&mutex);
		thread_count++;
		sem_post(&mutex);
	}
	close(listener);
	return 0;
}

