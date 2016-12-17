#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "network.h"

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
