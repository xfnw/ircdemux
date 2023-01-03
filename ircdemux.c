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
		return -1;
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
		return -1;
	}

	struct epoll_event event;
	event.events = EPOLLIN | EPOLLRDHUP; // | EPOLLOUT;
	event.data.fd = sockfd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event);

	return sockfd;
}

int initEpoll() {
	epfd = epoll_create(512);
	
	/* add stdin to epoll instance */
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLRDHUP;
	event.data.fd = 0; /* fd 0 is stdin */

	epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &event);
}

int readLine(char *buf, int maxlen, int fd) {
	int bufslice;

	maxlen--;

	for (bufslice=0; bufslice < maxlen; bufslice++) {
		if (!read(fd, buf + bufslice, 1) ||
				buf[bufslice] == '\n')
			break;
	}

	buf[bufslice+1] = '\0';

	return bufslice;
}

void handleLine(char *buf, int fd) {
	if (fd == 0) {
		if (*buf == '/') {
			warn("control commands not yet implemented");
			return;
		}
		warn("wah");
		return;
	}

	char *source = NULL;
	char *tok, *cmd, reply[513];

	source = strtok_r(buf, " \r\n", &tok);
	if (*source != ':') {
		cmd = source;
		source = NULL;
	} else {
		cmd = strtok_r(NULL, " \r\n", &tok);
	}

	/* tok is now the remaining, unprocessed line */

	if (!strcmp("PING", cmd)) {
		/* we do not need a \r\n at the end of the
		 * format because buf /should/ end with that
		 * still */
		dprintf(fd, "PONG %s", tok);
	}
	printf("got %s from %s\n",cmd,source);
}

int epollLoop() {
	struct epoll_event events[MAX_EVENTS];
	char *stdinbuf[513];
	int slice, readslice;

	for (;;) {
		int ready = epoll_wait(epfd, events, MAX_EVENTS, 1000);
		for (slice = 0; slice < ready; slice++) {
			if (events[slice].events & (EPOLLERR|EPOLLRDHUP)) {
				warnint("disconnected", events[slice].data.fd);
				close(events[slice].data.fd);
				continue;
			}
			//if (events[slice].events & EPOLLOUT)
			//	printf("%d is ready to write!\n", events[slice].data.fd);
			if (events[slice].events & EPOLLIN) {
				//printf("%d got %d data or something!\n", events[slice].data.fd,
				readLine((char *)stdinbuf, 512, events[slice].data.fd);
				info((char *)stdinbuf);
				handleLine((char *)stdinbuf, events[slice].data.fd);
			}
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
	int desc = openConnect(epfd, "wppnx", "6667");
	write(desc, "USER x x x x\r\n", 14);
	write(desc, "NICK xftesty\r\n", 14);
	epollLoop();

	error(117, "not implemented");
}

