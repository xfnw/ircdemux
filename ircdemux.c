#include <errno.h>
#include <fcntl.h>
#include <resolv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>

#define MAX_EVENTS 128

bool color = true;
int epfd, ewfd;
unsigned int srngstate;

/* chars that can be usually used anywhere in a nickname */
static const char *nickchars = "abcdefghijklmnopqrstuvwxyz\\_|[]";

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

/* fast-ish bad random number generator
 * magic: 584963 and 3942989 are primes, and 2147483648 is 2**31 to
 * make mod faster */
int srng(int input, int mod) {
	srngstate = (srngstate * 584963 + 3942989 + input) % 2147483648;
	return srngstate % mod;
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

	int flag = 1;
	if (!setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
				(char *) &flag,
				sizeof(flag)))
		warn("failed to disable nagle's algorithm");

	{
		struct epoll_event event;
		event.events = EPOLLIN | EPOLLRDHUP;
		event.data.fd = sockfd;
		epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event);
	}
	{
		struct epoll_event event;
		event.events = EPOLLOUT;
		event.data.fd = sockfd;
		epoll_ctl(ewfd, EPOLL_CTL_ADD, sockfd, &event);
	}

	return sockfd;
}

int initEpoll() {
	epfd = epoll_create(512);
	ewfd = epoll_create(512);
	
	/* make stdin nonblocking */
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	/* add stdin to epoll instance */
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLRDHUP;
	event.data.fd = 0; /* fd 0 is stdin */

	epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &event);
}

int readLine(char *buf, int maxlen, int fd) {
	int bufslice, readresult;

	maxlen--;

	for (bufslice=0; bufslice < maxlen; bufslice++) {
		if (!(readresult = read(fd, buf + bufslice, 1)) ||
				buf[bufslice] == '\n')
			break;
	}

	buf[bufslice+1] = '\0';

	/* return -1 if no more data to read */
	if (readresult == -1)
		return -1;

	/* return length otherwise */
	return bufslice+1;
}

void handleSLine(char *buf, int buflen, int outfd) {
	if (*buf == '/') {
		warn("control commands not yet implemented");
		return;
	}
	printf("hmm %d %d %ul\n", outfd, buflen);
	write(outfd, buf, buflen);
	return;
}

void aggressiveRead(char *buf, int buflen, int fd) {
	struct epoll_event events[MAX_EVENTS];
	int slice;

	for (;;) {
		int ready = epoll_wait(ewfd, events, MAX_EVENTS, 1000);

		for (slice = 0; slice < ready; slice++) {
			if (events[slice].events & (EPOLLERR|EPOLLRDHUP)) {
				warnint("disconnected", events[slice].data.fd);
				close(events[slice].data.fd);
				continue;
			}
			if (events[slice].events & EPOLLOUT) {
				handleSLine(buf, buflen, events[slice].data.fd);
				if ((buflen = readLine(buf, 512, fd)) == -1)
					return;
			}
		}
	}
}

void handleLine(char *buf, int buflen, int fd) {
	if (fd == 0) {
		aggressiveRead(buf, buflen, 0);
		return;
	}

	char *source = NULL;
	char *tok, *cmd;

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
		 * format because tok /should/ end with that
		 * still */
		dprintf(fd, "PONG %s", tok);
		return;
	}

	/* ERR_NAMEINUSE */
	if (!strcmp("433", cmd)) {
		char *attemptednick;
		strtok_r(NULL, " \r\n", &tok);
		attemptednick = strtok_r(NULL, " \r\n", &tok);

		attemptednick[srng(fd, strlen(attemptednick))] =
			nickchars[srng(fd, sizeof(nickchars))];

		dprintf(fd, "NICK %s\r\n", attemptednick);
		return;
	}
	printf("got %s from %s\n",cmd,source);
}

int epollLoop() {
	struct epoll_event events[MAX_EVENTS];
	char *inbuf[513];
	int slice;

	for (;;) {
		int ready = epoll_wait(epfd, events, MAX_EVENTS, 1000);
		for (slice = 0; slice < ready; slice++) {
			if (events[slice].events & (EPOLLERR|EPOLLRDHUP)) {
				warnint("disconnected", events[slice].data.fd);
				close(events[slice].data.fd);
				continue;
			}
			if (events[slice].events & EPOLLIN) {
				//printf("%d got %d data or something!\n", events[slice].data.fd,
				int buflen = readLine((char *)inbuf, 512, events[slice].data.fd);
				info((char *)inbuf);
				handleLine((char *)inbuf, buflen, events[slice].data.fd);
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

	srngstate = time(NULL);

	initEpoll();
	int desc = openConnect(epfd, "wppnx", "6667");
	write(desc, "USER x x x x\r\n", 14);
	write(desc, "NICK xftesty\r\n", 14);
	epollLoop();

	error(117, "not implemented");
}

