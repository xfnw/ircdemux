CC = c99
CFLAGS ?= -D_GNU_SOURCE

all: ircdemux

ircdemux: ircdemux.o
	${CC} -o ircdemux ircdemux.o

clean:
	rm -f ircdemux.o ircdemux
