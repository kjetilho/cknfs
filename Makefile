#
# $Header$
#
SHELL	= /bin/sh

PREFIX = /usr/local
###  Where executable should be put
DESTDIR	= $(PREFIX)/bin
###  Where man page should be put
MANDIR	= $(PREFIX)/share/man/man1

CDEBUGFLAGS=-g

### HP-UX
#CFLAGS = $(CDEBUGFLAGS) -D_XPG2
#EXTRAOBJS= getwd.o
#LIBS=

### SGI
#CFLAGS = $(CDEBUGFLAGS) -I. -I/usr/include/sun -I/usr/include/bsd
#EXTRAOBJS=
#LIBS= -lsun -lbsd

# OSF/1
# CFLAGS = $(CDEBUGFLAGS) -std
# EXTRAOBJS=
# LIBS=

###  SunOS, Ultrix, NeXT and the rest of the lot
CFLAGS = $(CDEBUGFLAGS)
EXTRAOBJS=
LIBS=

# Solaris
CFLAGS = $(CDEBUGFLAGS)
EXTRAOBJS=
LIBS= -lnsl

###  Suffix for man page
#MANSUFFIX = 1l
MANSUFFIX = 1
#MANSUFFIX = l
#MANSUFFIX = 1local

###
###  End of configuration section
###

MANPAGE = cknfs.$(MANSUFFIX)
PROG = cknfs

all:	$(PROG)

cknfs:	cknfs.o $(EXTRAOBJS)
	$(CC) -o cknfs cknfs.o $(EXTRAOBJS) $(LIBS)

install:	$(PROG)
	rm -f $(DESTDIR)/$(PROG)
	cp $(PROG) $(DESTDIR)
	chmod 755 $(DESTDIR)/$(PROG)
	rm -f $(MANDIR)/$(MANPAGE)
	cp cknfs.man $(MANDIR)/$(MANPAGE)
	chmod 644 $(MANDIR)/$(MANPAGE)

clean:
	rm -f *.o core cknfs

clobber:
	rm -f *.o core $(PROG)

lint:	cknfs.c
	lint -ahb $(INCLUDES) cknfs.c
