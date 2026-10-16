CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

prefix ?= /usr/local
PREFIX ?= $(prefix)
DESTDIR ?=
bindir = $(PREFIX)/bin

INSTALL ?= install
RM ?= rm -f

WARNINGS ?= -Wall -Wextra -Wpedantic
SOURCES = src/main.c src/walk.c src/output.c
HEADERS = src/kitten.h

all: kitten

kitten: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c99 -o $@ $(SOURCES) $(LDFLAGS) $(LDLIBS)

install: kitten
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 755 kitten "$(DESTDIR)$(bindir)/kitten"

uninstall:
	$(RM) "$(DESTDIR)$(bindir)/kitten"

clean:
	$(RM) kitten

.PHONY: all install uninstall clean
