.POSIX:

VERSION = 1.0
TARGET = ssm 
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

CFLAGS += -std=c99 -pedantic -Wall -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE

SRC = ssm.c
OBJS = $(SRC:.c=.o)

.c.o:
	$(CC) -o $@ $(CFLAGS) -c $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)
dist:
	mkdir -p $(TARGET)-$(VERSION)
	cp -R README.md $(TARGET) $(TARGET)-$(VERSION)
	tar -czf $(TARGET)-$(VERSION).tar.gz $(TARGET)-$(VERSION)
	rm -rf $(TARGET)-$(VERSION)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -p $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) *.o

all: $(TARGET)

.PHONY: all dist install uninstall clean
