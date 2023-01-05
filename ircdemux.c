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
char template[513] = "";
char chan[513] = "";

void info(char *message) {
	if (color)
		fprintf(stderr, "[\e[37mINFO\e[m] %s\n", message);
	else
		fprintf(stderr, "[INFO] %s\n", message);
}

void infochar(char *m1, char *m2, char *m3) {
	if (color)
		fprintf(stderr, "[\e[37mINFO\e[m] %s %s %s\n", m1, m2, m3);
	else
		fprintf(stderr, "[INFO] %s %s %s\n", m1, m2, m3);
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
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
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
	int bufslice;
	int readresult = -1;

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
	dprintf(outfd, "%s%s", template, buf);
	return;
}

int registerConnect(char *host, char *port, char *nick, char *user, char *real) {
	int desc;

	if ((desc = openConnect(epfd, host, port)) != -1) {
		if (user == NULL || *user == '\0')
			user = nick;
		if (real == NULL || *real == '\0')
			real = user;

		dprintf(desc, "USER %s 0 * :%s\r\n", user, real);
		dprintf(desc, "NICK %s\r\n", nick);

		return desc;
	}
	return -1;
}

void handleControlCommand(char *buf, int buflen) {
	if (buflen < 3)
		return;

	buf += 2;

	char *tok;

	switch (buf[-1]) {
		break; case 'c':
			char *host, *port, *nick, *user, *real;
			host = strtok_r(buf, " \r\n", &tok);
			port = strtok_r(NULL, " \r\n", &tok);
			nick = strtok_r(NULL, " \r\n", &tok);
			user = strtok_r(NULL, " \r\n", &tok);
			real = tok;

			if (host == NULL || port == NULL || nick == NULL) {
				warn("invalid syntax");
				return;
			}

			registerConnect(host, port, nick, user, real);
		break; case 't':
			memcpy(template, buf, buflen-3);
			template[buflen-3] = '\0';
		break; case 'j':
			memcpy(chan, buf, buflen-3);
			chan[buflen-3] = '\0';
		break; case 'a':
			struct epoll_event events[MAX_EVENTS];
			int slice;
			int ready = epoll_wait(ewfd, events, MAX_EVENTS, 1000);
			for (slice = 0; slice < ready; slice++) {
				if (events[slice].events & (EPOLLERR|EPOLLRDHUP)) {
					warnint("disconnected", events[slice].data.fd);
					close(events[slice].data.fd);
					continue;
				}
				if (events[slice].events & EPOLLOUT) {
					write(events[slice].data.fd, buf, buflen-2);
				}
			}
		break; default: warn("unknown control command");
	}
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
				/* skip out on priority
				 * reading, control commands
				 * may take a while */
				if (*buf == '/')
					goto CONTROLCOMMAND;

				handleSLine(buf, buflen, events[slice].data.fd);
				if ((buflen = readLine(buf, 512, fd)) == -1)
					return;
			}
		}
		/* allow some stuff to still function if
		 * no file descriptors are writable after
		 * timeout */
		if (ready == 0) {
			warn("no writable descriptors");
			if (*buf == '/')
				goto CONTROLCOMMAND;
			/* start dropping lines, so we can
			 * read future control commands */
			if ((buflen = readLine(buf, 512, fd)) == -1)
				return;
		}
	}

	return;

CONTROLCOMMAND:
	handleControlCommand(buf, buflen);
}

void handleLine(char *buf, int buflen, int fd) {
	if (fd == 0) {
		aggressiveRead(buf, buflen, 0);
		return;
	}

	char *source, *tok, *cmd;

	source = strtok_r(buf, " \r\n", &tok);

	/* why the heck is the ircd sending empty lines */
	if (source == NULL)
		return;

	if (*source != ':') {
		cmd = source;
		/* hack to get pointer to \0, lets us
		 * blindly printf the source without a
		 * segfault */
		source = tok-1;
	} else {
		cmd = strtok_r(NULL, " \r\n", &tok);
		if (cmd == NULL)
			return;
	}

	/* tok is now the remaining, unprocessed line */

	/* shortcut for ignoring lines faster */
	if (!strcmp("PRIVMSG", cmd) ||
			!strcmp("JOIN", cmd))
		return;

	/* differentiating between snotes and normal notices
	 * is too expensive, output them all */
	if (*cmd == '4' ||
			!strcmp("NOTICE", cmd) ||
			!strcmp("366", cmd) ||
			!strcmp("001", cmd)) {

		/* ERR_NAMEINUSE */
		if (!strcmp("433", cmd)) {
			char *attemptednick;
			strtok_r(NULL, " \r\n", &tok);
			attemptednick = strtok_r(NULL, " \r\n", &tok);

			if (attemptednick == NULL)
				return;

			attemptednick[srng(fd, strlen(attemptednick))] =
				nickchars[srng(fd, sizeof(nickchars))];

			dprintf(fd, "NICK %s\r\n", attemptednick);
		}

		/* RPL_WELCOME */
		if (*chan != '\0' && !strcmp("001", cmd))
			dprintf(fd, "JOIN %s\r\n", chan);

		infochar(source, cmd, tok);
		return;
	}

	if (!strcmp("PING", cmd)) {
		/* we do not need a \r\n at the end of the
		 * format because tok /should/ end with that
		 * still */
		dprintf(fd, "PONG %s", tok);
		return;
	}
	//printf("got %s from %s\n",cmd,source);
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
				int buflen = readLine((char *)inbuf, 512, events[slice].data.fd);
				//info((char *)inbuf);
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
	epollLoop();

	error(117, "not implemented");
}

