#
# $Header$
#
SHELL	= /bin/sh
VERSION = 1.7

PREFIX = /usr/local
###  Where executable should be put
DESTDIR	= $(PREFIX)/bin
###  Where man page should be put
MANDIR	= $(PREFIX)/share/man/man1

CDEBUGFLAGS=-g

### HP-UX
#CFLAGS = $(CDEBUGFLAGS) -D_XPG2
#LIBS=

### SGI
#CFLAGS = $(CDEBUGFLAGS) -I. -I/usr/include/sun -I/usr/include/bsd
#LIBS= -lsun -lbsd

# OSF/1
# CFLAGS = $(CDEBUGFLAGS) -std
# LIBS=

###  SunOS, Ultrix, NeXT and the rest of the lot
CFLAGS = $(CDEBUGFLAGS)
LIBS=

# Solaris
CFLAGS = $(CDEBUGFLAGS)
LIBS= -lnsl

###  Suffix for man page
MANSUFFIX = 1

###
###  End of configuration section
###

MANPAGE = cknfs.$(MANSUFFIX)
PROG = cknfs

all:	$(PROG)

cknfs:	cknfs.o
	$(CC) -o cknfs cknfs.o $(LIBS)

install:	$(PROG)
	rm -f $(DESTDIR)/$(PROG)
	cp $(PROG) $(DESTDIR)
	chmod 755 $(DESTDIR)/$(PROG)
	rm -f $(MANDIR)/$(MANPAGE)
	cp cknfs.man $(MANDIR)/$(MANPAGE)
	chmod 644 $(MANDIR)/$(MANPAGE)

dist:
	mkdir cknfs-$(VERSION)
	cp README Makefile cknfs.c cknfs.man cknfs-$(VERSION)
	tar zcf cknfs-$(VERSION).tar.gz cknfs-$(VERSION)
	rm -rf cknfs-$(VERSION)

clean:
	rm -f *.o core cknfs

clobber:
	rm -f *.o core $(PROG)

lint:	cknfs.c
	lint -ahb $(INCLUDES) cknfs.c
