#include <errno.h>
#include <resolv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_EVENTS 128

bool color = true;
int epfd;

void info(char *message) {
	if (color)
		fprintf(stderr, "[\e[37mINFO\e[m] %s\n", message);
	else
		fprintf(stderr, "[INFO] %s\n", message);
}

void warn(char *message) {
	if (color)
		fprintf(stderr, "[\e[33mWARN\e[m] %s\n", message);
	else
		fprintf(stderr, "[WARN] %s\n", message);
}

void warnint(char *message, int param) {
	if (color)
		fprintf(stderr, "[\e[33mWARN\e[m] %d %s\n", param, message);
	else
		fprintf(stderr, "[WARN] %d %s\n", param, message);
}

void error(int err, char *message) {
	if (color)
		fprintf(stderr, "[\e[31mERROR\e[m] %s\n", message);
	else
		fprintf(stderr, "[ERROR] %s\n", message);
	exit(err);
}

int openConnect(int epfd, char *server, char *port) {
	int sockfd;
	static struct addrinfo hints;
	struct addrinfo *res, *r;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	/* SOCK_NONBLOCK will get set later, getaddrinfo
	 * seems to not like when hints contain it */

	if (getaddrinfo(server, port, &hints, &res) != 0) {
		warn("failed to resolve host");
		return 14;
	}

	hints.ai_socktype |= SOCK_NONBLOCK;

	for (r = res; r; r = r->ai_next) {
		if ((sockfd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1 )
			continue;
		if (connect(sockfd, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(sockfd);
	}
	freeaddrinfo(res);
	if (!r) {
		warn("failed to connect");
		return 111;
	}

	struct epoll_event event;
	event.events = EPOLLIN | EPOLLOUT;
	event.data.fd = sockfd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event);

	return 0;
}

int initEpoll() {
	epfd = epoll_create(512);
	
	/* add stdin to epoll instance */
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = 0; /* fd 0 is stdin */

	epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &event);
}

int epollLoop() {
	struct epoll_event events[MAX_EVENTS];
	char *stdinbuf[MAX_EVENTS * 512];
	int slice, bufslice, readslice;

	for (;;) {
		int ready = epoll_wait(epfd, events, MAX_EVENTS, 1000);
		for (slice = 0; slice < ready; slice++) {
			if (events[slice].events & EPOLLERR) {
				warnint("disconnected", events[slice].data.fd);
				close(events[slice].data.fd);
				continue;
			}
			if (events[slice].events & EPOLLOUT)
				printf("%d is ready to write!\n", events[slice].data.fd);
			if (events[slice].events & EPOLLIN)
				printf("%d got data or something!\n", events[slice].data.fd);
		}
	}
}

int main() {
	{
		char *no_color = getenv("NO_COLOR");
		if (no_color != NULL && *no_color != '\0') {
			color = false;
		}
	}

	initEpoll();
	openConnect(epfd, "localhost", "6969");
	epollLoop();

	error(117, "not implemented");
}

