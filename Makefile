#
#
SHELL	= /bin/sh
VERSION = 1.9

PREFIX = /usr/local
###  Where executable should be put
DESTDIR	= $(PREFIX)/bin
###  Where man page should be put
MANDIR	= $(PREFIX)/share/man/man1

CDEBUGFLAGS=-g

### HP-UX
HPUX_CFLAGS = -D_XPG2
#LIBS=

### SGI
SGI_CFLAGS = -I/usr/include/sun -I/usr/include/bsd
#LIBS= -lsun -lbsd

# OSF/1
# CFLAGS = $(CDEBUGFLAGS) -std
# LIBS=


CFLAGS = $(CDEBUGFLAGS) # $(HPUX_CFLAGS) $(SGI_CFLAGS)

# Many but not all OS require -lnsl, so we test for existence of the
# shared library.  HP-UX names it .sl, not .so
LIBS = `[ -f /usr/lib/libnsl.so -o -f /usr/lib/libnsl.sl ] && echo -lnsl`

###  Suffix for man page
MANSUFFIX = 1

###
###  End of configuration section
###

MANPAGE = cknfs.$(MANSUFFIX)
PROG = cknfs

all:	$(PROG)

$(PROG):	cknfs.o
	$(CC) -o $(PROG) cknfs.o $(LIBS)

install: test
	rm -f $(DESTDIR)/$(PROG)
	cp $(PROG) $(DESTDIR)
	chmod 755 $(DESTDIR)/$(PROG)
	rm -f $(MANDIR)/$(MANPAGE)
	cp cknfs.man $(MANDIR)/$(MANPAGE)
	chmod 644 $(MANDIR)/$(MANPAGE)

test:	all
	here=`/bin/pwd`; \
	[ "`./cknfs -u $$here / /VERY-UNLIKELY-PATH / /etc 2>/dev/null`" = \
          "$$here / /etc" ]

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
