#
# $Header$
#
# $Log$
# Revision 1.4  1993/02/25 17:40:58  anders
# OSF/1 port
#
# Revision 1.3  1992/10/29  14:56:42  obh
# portet cknfs til NeXT.
#
# Revision 1.2  1992/10/24  03:01:00  obh
# Fikset litt p} cknfs slik at den kompilerer greit p} SGI og HP.
# Klarte ikke } logge meg inn p} NeXT maskinen.
#
# Revision 1.1.1.1  1990/09/09  20:01:10  rein
# Version 1.6 of cknfs (check nfs server)
#
# Revision 1.1  90/09/09  20:01:09  rein
# Initial revision
# 
# Revision 1.1  89/06/20  23:29:37  aklietz
# Initial revision
# 
#
SHELL	= /bin/sh

### HP-UX
#CFLAGS = -D_XPG2
#EXTRAOBJS= getwd.o
#LIBS=

### SGI
#CFLAGS = -O -I. -I/usr/include/sun -I/usr/include/bsd
#EXTRAOBJS=
#LIBS= -lsun -lbsd

# OSF/1
CFLAGS = -std -g
EXTRAOBJS=
LIBS=

###  Sun, Ultrix, NeXT and the rest of the lot
#CFLAGS = -g
#EXTRAOBJS=
#LIBS=

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
