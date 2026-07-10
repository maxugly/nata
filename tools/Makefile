PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CC=gcc
CFLAGS=-Wall -Wextra -O2

all: natactl

natactl: natactl.c
	$(CC) $(CFLAGS) -o natactl natactl.c

install: natactl
	mkdir -p $(DESTDIR)$(BINDIR)
	cp natactl $(DESTDIR)$(BINDIR)/

clean:
	rm -f natactl