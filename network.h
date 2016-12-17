#ifndef NETWORK_H
#define NETWORK_H

#define BACKLOG 10 //how many pending connections the queue will hold

void
*get_in_addr(struct sockaddr *sa);

void
setup_server(int *listener, char *port);

int
connect_host(char *hostname);

#endif
