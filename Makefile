CFLAGS ?= -O3

all: ircdemux

ircdemux: ircdemux.o
	${CC} ${CFLAGS} -o ircdemux ircdemux.o

clean:
	rm -f ircdemux.o ircdemux
