#
# $Header$
#
# $Log$
# Revision 1.1  1990/09/09 20:01:09  rein
# Initial revision
#
# Revision 1.1  89/06/20  23:29:37  aklietz
# Initial revision
# 
#
SHELL	= /bin/sh

###  Change to -g for debugging
CFLAGS	= -O

### Necessary include dirs, if any
INCLUDES =
#INCLUDES = -I/usr/include/sun -I/usr/include/bsd     # for SGI systems

### Necessary libraries, if any
LFLAGS=
#LFLAGS= -lsun -lbsd   # for SGI systems

###  Where executable should be put
DESTDIR	= /local/bin

###  Where man page should be put
MANDIR	= /local/man/man1

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

cknfs:	cknfs.c
	$(CC) $(CFLAGS) $(INCLUDES) cknfs.c -o cknfs $(LFLAGS)

install:	$(PROG)
	rm -f $(DESTDIR)/$(PROG)
	cp $(PROG) $(DESTDIR)
	chmod 755 $(DESTDIR)/$(PROG)
	rm -f $(MANDIR)/$(MANPAGE)
	cp cknfs.man $(MANDIR)/$(MANPAGE)
	chmod 644 $(MANDIR)/$(MANPAGE)

clean:
	rm -f *.o core

clobber:
	rm -f *.o core $(PROG)

lint:	cknfs.c
	lint -ahb $(INCLUDES) cknfs.c
