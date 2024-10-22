.POSIX:
.SUFFIXES:

VERSION = 1.0
TARGET = ssm 
MANPAGE = $(TARGET).1
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

LDFLAGS != pkg-config --libs libnotify
INCFLAGS != pkg-config --cflags libnotify
CFLAGS = -O3 -march=native -mtune=native -pipe -s -std=c99 -flto -pedantic -Wall $(INCFLAGS)

SRC = ssm.c

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $@ $(CFLAGS) $(LDFLAGS)

dist:
	mkdir -p $(TARGET)-$(VERSION)
	cp -R README.md $(MANPAGE) $(TARGET) $(TARGET)-$(VERSION)
	tar -cf $(TARGET)-$(VERSION).tar $(TARGET)-$(VERSION)
	gzip $(TARGET)-$(VERSION).tar
	rm -rf $(TARGET)-$(VERSION)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)
	cp -p $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)
	cp -p $(MANPAGE) $(DESTDIR)$(MANDIR)/$(MANPAGE)
	chmod 644 $(DESTDIR)$(MANDIR)/$(MANPAGE)

uninstall:
	rm $(DESTDIR)$(BINDIR)/$(TARGET)
	rm $(DESTDIR)$(MANDIR)/$(MANPAGE)

clean:
	rm $(TARGET)

all: $(TARGET)

.PHONY: all dist install uninstall clean
