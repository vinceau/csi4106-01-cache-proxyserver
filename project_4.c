/*
 * project_3 -- a simple proxy server
 *
 * The third project of the 2016 Fall Semester course
 * CSI4106-01: Computer Networks at Yonsei University.
 *
 * Author: Vincent Au (2016840200)
 */

#define _GNU_SOURCE

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

struct modes {
	int is_mobile;
	int is_falsify;
	int is_redirect;
	char red_host[2048];
	char red_path[2048];
	char colour[6];
};


void
check_modes(char *path);

int
falsify(char *string, int nbytes, int *result);

int
connect_host(char *hostname);

int
parse_response(char *response);

int
parse_request(char *request);

ssize_t
send_request(int servconn);

void
handle_request();

void
*get_in_addr(struct sockaddr *sa);

void
setup_server(int *listener, char *port);


static int count = 1;
struct request req; //latest request info
struct response res; //latest response info
struct modes m; //current mode settings

int connfd; //socket of the connected client
char hoststr[NI_MAXHOST]; //readable client address
char portstr[NI_MAXSERV]; //readable client port

char *MOBILE_UA = "Mozilla/5.0 (Linux; Android 5.1.1; Nexus 5 Build/LMY48B; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/43.0.2357.65 Mobile Safari/537.36";
char *ERROR_MSG = "HTTP/1.1 403 Forbidden\r\n\r\n";


/*
 * Reads <path> and determines the changing of modes if any. Only works with
 * one mode change at a time.
 * Note: <path> is modified to remove the mode option from the path once
 * complete.
 */
void
check_modes(char *path)
{
	char *ptr;
	char temp[2048];
	ptr = strchr(path, '?');
	if (ptr != NULL) {
		ptr += 1;
		if (strcmp(ptr, "start_mobile") == 0) {
			m.is_mobile = 1;
		} else if (strcmp(ptr, "stop_mobile") == 0) {
			m.is_mobile = 0;
		} else if (strcmp(ptr, "stop_redirect") == 0) {
			m.is_redirect = 0;
		} else if (strcmp(ptr, "stop_falsify") == 0) {
			m.is_falsify = 0;
		} else if (sscanf(ptr, "start_redirect=%s", temp) == 1) {
			strncpy(m.red_host, temp, sizeof(m.red_host));
			m.is_redirect = 1;
			//printf("Will now redirect to: %s\n", m.red_host);
		} else if (sscanf(ptr, "start_falsify=%s", temp) == 1) {
			strncpy(m.colour, temp, sizeof(m.colour));
			m.is_falsify = 1;
			//printf("Will now set all colour to: %s\n", temp);
		} else {
			//none of the options were achieved so just exit
			return;
		}
		//remove the mode changing from the actual url
		*(ptr-1) = '\0';
	}
}

/*
 * Writes <nbytes> bytes of data from <string> to connfd.
 *
 * If the deferenced value of <status> is false and the type of the HTTP
 * response in <string> is HTML, attempts at falsification will occur where it
 * looks through <string> to find the body tag and adds the style attribute
 * to it, changing the background colour to whatever is set in the global
 * mode.color attribute. If the body tag is found, true is written to <status>.
 *
 * Returns the number of bytes that were successfully written to connfd.
 */
int
falsify(char *string, int nbytes, int *status)
{
	//if it's already falsified, or it's not a html document just write it
	if (*status || (!*status && strstr(res.c_type, "text/html") == NULL)) {
		return write(connfd, string, nbytes);
	}

	int find_body = 0;
	int i = 0;
	int bytes_sent = 0;
	char *ptr = string;
	for (; i < nbytes - 4; i++) {
		if (strncmp(ptr, "<body", 5) == 0) {
			find_body = 1;
			break;
		}
		ptr++;
	}
	if (!find_body) { //we reached the end without finding <body
		*status = 0; //not found
		return write(connfd, string, nbytes);
	}
	//we found "<body " so send up til that point
	bytes_sent += write(connfd, string, i+5);

	//send the style part
	char style[64];
	snprintf(style, sizeof(style), " style=\"background-color: #%s\"", m.colour);
	bytes_sent += write(connfd, style, strlen(style));

	//mark status as successful and send the rest
	*status = 1;
	bytes_sent += write(connfd, ptr, strlen(ptr+5));
	return bytes_sent;
}

/*
 * Returns a new socket having connected to <hostname> on port 80
 */
int
connect_host(char *hostname)
{
	struct hostent *he;
	struct in_addr **addr_list;
	struct sockaddr_in server;

	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("couldn't open socket");
		exit(1);
	}

	if ((he = gethostbyname(hostname)) == NULL) {
		perror("Couldn't call gethostbyname()");
		exit(1);
	}

	int yes = 1;
	//allow port reuse
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		perror("ERROR: setsockopt() failed");
		exit(1);
	}
	//don't crash when writing to closed socket
	if (setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) == -1) {
		perror("ERROR: setsockopt() failed");
		exit(1);
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
	server.sin_family = AF_INET;
	server.sin_port = htons(80);

	if (connect(sockfd, (struct sockaddr *)&server, sizeof(server))) {
		perror("Couldn't connect socket to client");
		exit(1);
	}

	return sockfd;
}

/*
 * Traverses <response> and stores attribute information in the global
 * variable <res>. Returns the length of the response header.
 */
int
parse_response(char *response)
{
	//printf("\n\nPARSE RESPONSE: <%s>\n\n", response);
	res.has_length = 0;
	res.has_type = 0;
	//scan the method and url into the pointer
	if (sscanf(response, "%s %d %[^\r\n]\r\n", res.http_v,
			&res.status_no, res.status) < 3) {
		return 0;
	}

	char *token, *string, *tofree;
	tofree = string = strdup(response);
	//loop through the request line by line (saved to token)
	while ((token = strsep(&string, "\r\n")) != NULL) {
		if (strncmp(token, "Content-Type: ", 14) == 0) {
			char *type = token + 14;
			strncpy(res.c_type, type, sizeof(res.c_type));
			res.has_type = 1;
		}
		else if (strncmp(token, "Content-Length: ", 16) == 0) {
			char *len = token + 16;
			strncpy(res.c_length, len, sizeof(res.c_length));
			res.has_length = 1;
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
parse_request(char *request)
{
	//printf("\n\nPARSE REQUEST: <%s>\n\n", request);
	//scan the method and url into the pointer
	if (sscanf(request, "%s %s %s\r\n", req.method, req.url,
				req.http_v) < 3) {
		return -1;
	}

	char *token, *string, *tofree;
	tofree = string = strdup(request);
	//loop through the request line by line (saved to token)
	while ((token = strsep(&string, "\r\n")) != NULL) {
		if (strncmp(token, "Host: ", 6) == 0) {
			char *host = token + 6;
			strncpy(req.host, host, sizeof(req.host));

			char *path_offset = strstr(req.url, host);
			path_offset+=strlen(host);
			strncpy(req.path, path_offset, sizeof(req.path));
		}
		else if (strncmp(token, "User-Agent: ", 12) == 0) {
			char *userag = token + 12;
			strncpy(req.useragent, userag, sizeof(req.useragent));
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
send_request(int servconn)
{
	char request[2048];
	char *ua = (m.is_mobile) ? MOBILE_UA : req.useragent;
	char *host = req.host;
	char *path = req.path;

	if (m.is_redirect && strstr(req.host, m.red_host) == NULL) {
		host = m.red_host;
		path = "/";
	}

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
handle_request()
{
	printf("-----------------------------------------------\n");
	printf("%d [%s] Redirection [%s] Mobile [%s] Falsification\n", count++,
			m.is_redirect ? "O" : "X",
			m.is_mobile ? "O" : "X",
			m.is_falsify ? "O" : "X"
			);

	printf("[CLI connected to %s:%s]\n", hoststr, portstr);
	printf("[CLI ==> PRX --- SRV]\n");
	printf("> GET %s%s\n", req.host, req.path);
	printf("> %s\n", req.useragent);

	char *host = req.host;
	if (m.is_redirect && strstr(req.host, m.red_host) == NULL) {
		host = m.red_host;
	}

	int servconn = connect_host(host);
	printf("[SRV connected to %s:80]\n", host);
	if (send_request(servconn) == -1) {
		perror("Error writing to socket");
		exit(1);
	}

	char buf[MAX_BUF]; //buffer for messages
	int nbytes; //the number of received bytes
	long bytes_in = 0;
	long bytes_out = 0;
	long header_length;
	int falsified = !m.is_falsify;

	nbytes = recv(servconn, buf, MAX_BUF,0);
	bytes_in += nbytes;

	if (nbytes > 0) {
		header_length = parse_response(buf);
		bytes_out += falsify(buf, nbytes, &falsified);

		if (res.has_length) {
			//we know exactly how many bytes we're expecting
			long bytes_left = atoll(res.c_length);
			bytes_left -= (nbytes - header_length);

			while (bytes_left > 0) {
				memset(&buf, 0, sizeof(buf));
				nbytes = recv(servconn, buf, MAX_BUF,0);
				bytes_in += nbytes;
				bytes_out += falsify(buf, nbytes, &falsified);
				bytes_left -= nbytes;
			}
		}
		else {
			//we have no idea how many bytes to expect... uh oh
			memset(&buf, 0, sizeof(buf));
			while ((nbytes = recv(servconn, buf, MAX_BUF,0)) > 0) {
				bytes_in += nbytes;
				bytes_out += falsify(buf, nbytes, &falsified);

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
	if (argc != 2) {
		//remember: the name of the program is the first argument
		fprintf(stderr, "ERROR: Missing required arguments!\n");
		printf("Usage: %s <port>\n", argv[0]);
		printf("e.g. %s 9001\n", argv[0]);
		exit(1);
	}

	char *port = argv[1]; //port we're listening on
	int listener; //file descriptor of listening socket
	struct sockaddr_storage their_addr; //connector's address info
	socklen_t sin_size;
	char buf[MAX_BUF]; //buffer for messages
	int nbytes; //the number of received bytes

	//set up the server on the specified port
	setup_server(&listener, port);

	printf("Starting proxy server on port %s\n", port);

	while(1) {
		memset(&req, 0, sizeof(req));
		memset(&buf, 0, sizeof(buf));

		sin_size = sizeof(their_addr);
		connfd = accept(listener, (struct sockaddr *) &their_addr,
				&sin_size);
		if (connfd == -1) {
			perror("ERROR: accept() failed");
			continue;
		}

		//store the ip address and port into the hoststr and portstr
		getnameinfo((struct sockaddr *)&their_addr, sin_size, hoststr,
				sizeof(hoststr), portstr, sizeof(portstr),
				NI_NUMERICHOST | NI_NUMERICSERV);

		if ((nbytes = recv(connfd, buf, MAX_BUF, 0)) > 0) {
			//we received a request!
			if (parse_request(buf) != -1 && strcmp(req.method, "GET") == 0) {
				check_modes(req.path);
				handle_request();
			} else {
				//Return a 403 Forbidden error if they attempt to load
				//something needing SSL/HTTPS
				write(connfd, ERROR_MSG, strlen(ERROR_MSG));
				close(connfd);
			}
		}
	}
	close(listener);
	return 0;
}

