/*
 * cknfs - Check for dead NFS servers
 *
 * Don't you hate it when you login on an NFS client, only to find
 * yourself hung because one of your execution paths points to a dead
 * NFS server?
 *
 * Well, this program fixes that problem.  It takes a list of execution
 * paths as arguments.   Each path is examined for an NFS mount point.
 * If found, the corresponding NFS server is checked.   Paths that lead
 * to dead NFS servers are ignored.  The remaining paths are printed to
 * stdout.  No more hung logins!
 *
 * Usage: cknfs -e -s -t# -v -D -L paths
 *  
 *       -e     silent, do not print paths
 *       -s     print paths in sh format (colons)
 *       -t n   timeout interval before assuming an NFS
 *              server is dead (default 10 seconds)
 *       -v     verbose
 *       -D     debug
 *       -L     expand symbolic links
 *
 * Typical examples:
 *
 *	set path = `cknfs /bin /usr/bin /usr/ucb . /usr6/bin /sdg/bin`
 *	alias cd 'cknfs -e \!*; if ($status == 0) chdir \!*'
 *
 * The latter example prevents you from hanging if you cd to a
 * directory that leads to a dead NFS server.
 *
 * Adminstrative note:  You can still get hung if your administator 
 * mixes NFS mount points from different machines in the same parent
 * directory or if your administrator mixes regular directories and
 * NFS mount points in the same parent directory.
 *
 * The best organization is an overall /nfs directory with subdirectories 
 * for each machine.   For example, if you have 3 NFS servers named
 * "newton", "bardeen", and "zaphod", a good organization would be
 *
 *	/nfs/bardeen/apps
 *	/nfs/bardeen/bin
 *	/nfs/newton/bin
 *	/nfs/newton/local
 *	/nfs/zaphod/bin
 *	/nfs/zaphod/sdg
 *
 * NEVER MIX MOUNT POINTS FROM DIFFERENT MACHINES IN THE SAME
 * PARENT DIRECTORY.
 *
 * Implementation note: A small amount of system-dependent code is required
 * to read the mount table.   This is located in mkm_mlist() at bottom of the
 * program.  It may have to be edited to handle local system dependencies.
 * #ifdef'ed versions for SunOs, Ultrix, and SGI are included.
 */

/*
 * Copyright (c) 1989, The Board of Trustees of the University of Illinois
 * National Center for Supercomputing Applications.
 *
 * No warranty is expressed or implied.
 * Unlimited redistribution permitted.
 *
 */

static char *RCSid = "$Header$";

/*
 * $Log$
 * Revision 1.4  1993/02/25 17:41:00  anders
 * OSF/1 port
 *
 * Revision 1.3  1992/10/29  14:56:45  obh
 * portet cknfs til NeXT.
 *
 * Revision 1.2  1992/10/24  03:01:02  obh
 * Fikset litt p} cknfs slik at den kompilerer greit p} SGI og HP.
 * Klarte ikke } logge meg inn p} NeXT maskinen.
 *
 * Revision 1.1.1.1  1990/09/09  20:01:19  rein
 * Version 1.6 of cknfs (check nfs server)
 *
 * Revision 1.1  90/09/09  20:01:16  rein
 * Initial revision
 * 
 * Revision 1.6  89/06/21  00:04:15  aklietz
 * Linted.  Baseline for release.
 * 
 * Revision 1.5  89/06/20  23:37:59  aklietz
 * Restart the parse loop on .. instead of just popping the stack,
 * because a/../b need not necessarily == b across a symbolic link.
 * Add support for SGI.
 * 
 * Revision 1.4  89/05/31  18:24:49  aklietz
 * Fix bug introduced in rev 1.3 that did hangable lstat before
 * checking for NFS mount point.
 * Add support for Ultrix.
 * 
 * Revision 1.3  89/05/29  03:30:55  aklietz
 * Terminate silently if no args in -e mode.
 * Fix omission of chdir("/") during parse of symlink to absolute path.
 * 
 * Revision 1.2  89/05/26  14:14:35  aklietz
 * Baseline for release
 * 
 * Revision 1.1  89/05/26  13:37:39  aklietz
 * Initial revision
 * 
 */

#include "sys/param.h"
#include "errno.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "signal.h"
#include "ctype.h"
#include "stdio.h"
#include "sys/time.h"
#include "sys/socket.h"
#include "arpa/inet.h"
#include "netdb.h"
#include "rpc/rpc.h"
#include "rpc/pmap_prot.h"
#include "rpc/pmap_clnt.h"
#ifndef sgi /* sgi is missing nfs.h for some reason */
#ifdef __osf__
#include <sys/mount.h>
#endif
#include "nfs/nfs.h"
#else
	/* sgi is missing nfs.h, so we must hardcode the RPC values */
#	define NFS_PROGRAM 100003L
#	define NFS_VERSION 2L
#endif

/*
 * Make initial program
 * May 1989, Alan Klietz (aklietz@ncsa.uiuc.edu)
 */

#define DEFAULT_TIMEOUT 10  /* Default timeout for checking NFS server */

#if !defined(sgi) && !defined(NeXT)
extern char *realloc();
#endif
extern char *strchr(), *strrchr(), *strtok();

struct m_mlist {
	int mlist_checked; /* -1 if bad, 0 if not checked, 1 if ok */
	struct m_mlist *mlist_next;
	char *mlist_dir;
	char *mlist_fsname;
	int mlist_isnfs;
};
static struct m_mlist *firstmnt;

static int errflg;
static int eflg, sflg, vflg, Dflg, Lflg;
static int timeout = DEFAULT_TIMEOUT;
static char prefix[MAXPATHLEN];
struct m_mlist *isnfsmnt();
char *xalloc();
void mkm_mlist();

int
main(argc, argv)
int argc;
char **argv;
{
	register int n;
	register char *s;
	int good = 0;
	char outbuf[BUFSIZ];
	char errbuf[BUFSIZ];
	extern int optind;
	extern char *optarg;


	/*
	 * Avoid intermixing stdout and stderr
	 */
	setvbuf(stdout, outbuf, _IOFBF, sizeof(outbuf));
	setvbuf(stderr, errbuf, _IOLBF, sizeof(errbuf));

	while ((n = getopt(argc, argv, "est:vDL")) != EOF)
		switch(n) {
			case 'e':	++eflg;
					break;

			case 's':	++sflg;
					break;

			case 't':	timeout = atoi(optarg);
					break;

			case 'v':	++vflg;
					break;

			case 'D':	++Dflg; ++vflg;
					break;

			case 'L':	++Lflg;
					break;

			default:
					++errflg;
		}

	if (argc <= optind && !eflg) /* no paths */
		++errflg;

	if (errflg) {
		fprintf(stderr, "Usage: %s -e -s -t# -v -D -L paths\n", argv[0]);
		fprintf(stderr, "\tCheck paths for dead NFS servers\n");
		fprintf(stderr, "\tGood paths are printed to stdout\n\n");
		fprintf(stderr, "\t -e\tsilent, do not print paths\n");
		fprintf(stderr, "\t -s\tprint paths in sh format (semicolons)\n");
		fprintf(stderr, "\t -t n\ttimeout interval before assuming an NFS\n");
		fprintf(stderr, "\t\tserver is dead (default 10 seconds)\n");
		fprintf(stderr, "\t -v\tverbose\n");
		fprintf(stderr, "\t -D\tdebug\n");
		fprintf(stderr, "\t -L\texpand symbolic links\n\n");
		exit(1);
	}

	for (n = optind; n < argc; ++n) {
		s = argv[n];
		if (Dflg)
			fprintf(stderr, "chkpath(%s)\n", s);
		if (chkpath(s)) {
			if (good++ && !eflg)
				putchar(sflg ? ':' : ' ');
			if (!eflg)
				fputs(Lflg ? prefix : s, stdout);
		} else
			if (vflg)
				fprintf(stderr, "path skipped: %s\n",
					Lflg ? prefix : s);
	}

	if (good && !eflg)
		putchar('\n');

	(void) fflush(stderr);
	(void) fflush(stdout);

	exit(good == 0 && optind < argc );
}


int chkpath(path)
/*
 * Check path for accessibility.  Return 1 if ok, 0 if error
 */
char *path;
{
	extern char *getwd();

	if (*path != '/') { /* If not absolute path, get initial prefix */
		if (getwd(prefix) == NULL) {
			fprintf(stderr, "%s\n", prefix);
			return 0;
		}
	}
	return(_chkpath(path));
}


#define NTERMS 256

int
_chkpath(path)
char *path;
{
	register char *s, *s2;
	register int i, front=0, back=0;
	struct stat stb;
	struct m_mlist *mlist;
	char p[MAXPATHLEN];
	char symlink[MAXPATHLEN];
	char *queue[NTERMS];


	/*
	 * Copy path to working storage
	 */
	strncpy(p, path, sizeof(p)-1);

	if (*p == '/') { /* If absolute path, start at root */
		*prefix = '\0';
		(void) chdir("/");
	}

	if (Dflg)
		fprintf(stderr, "_chkpath(%s) prefix=%s\n", path, prefix);

	/*
	 * Put directory terms on FIFO queue
	 */
	for (s = strtok(p, "/"); s != NULL; s = strtok((char *)NULL, "/")) {
		if (back >= NTERMS) {
			fprintf(stderr, "Too many subdirs: %s\n", path);
			goto fail;
		}
		queue[back++] = s;
	}
	/*  queue[front] = a, queue[front+1] = b, ... queue[back] = null */

	/*
	 * Scan queue of directory terms, expanding 
	 * symbolic links recursively.
	 */
	while (front != back) {
		s = queue[front++];
		/* Dot */
		if (s[0] == '.' && s[1] == '\0')
			continue;
		/* Dot Dot */
		if (s[0] == '.' && s[1] == '.' && s[2] == '\0') {
			if (chdir("..") < 0) {
				perror("chdir(..)");
				goto fail;
			}
			/* Remove trailing component of prefix */
			if ((s2 = strrchr(prefix, '/')) != NULL)
				*s2 = '\0';
			continue;
		} else {
			(void) strcat(prefix, "/");
			(void) strcat(prefix, s);
		}

		if ((mlist = isnfsmnt(prefix)) != NULL) /* NFS mount? */
			if (chknfsmnt(mlist) <= 0)
				return 0;

		/* Check if symlink */
		if (lstat(s, &stb) < 0) {
			perror(s);
			goto fail;
		}
		if ((stb.st_mode & S_IFMT) != S_IFLNK) {
			/* not symlink */
			if (chdir(s) < 0) {
				perror(prefix);
				goto fail;
			}
			continue;
		}

		/* Remove symlink from tail of prefix */
		if ((s2 = strrchr(prefix, '/')) != NULL)
			*s2 = '\0';
		/* 
		 * Read symlink
		 */
		if ((i = readlink(s, symlink, MAXPATHLEN-1)) < 0) {
			perror(s);
			goto fail;
		}
		symlink[i] = '\0'; /* null terminate */

		/*
		 * Recursively check symlink
		 */
		if (_chkpath(symlink) == 0)
			return 0;
	}

	return 1;

fail:
	return 0;
}
	

struct m_mlist *
isnfsmnt(path)
/*
 * Return 1 if path is NFS mount point
 */
char *path;
{
	register struct m_mlist *mlist;
	static int init;

	if (init == 0) {
		++init;
		mkm_mlist();
	}

	for (mlist = firstmnt; mlist != NULL; mlist = mlist->mlist_next) {
		if (mlist->mlist_isnfs == 0)
			continue;
		if (strcmp(mlist->mlist_dir, path) == 0)
			return(mlist);
	}
	return NULL;
}


static int
get_inaddr(saddr, host)
/*
 * Translate host name to Internet address.
 * Return 1 if ok, 0 if error
 */
struct sockaddr_in *saddr;
char *host;
{
	register struct hostent *hp;

	(void) memset((char *)saddr, 0, sizeof(struct sockaddr_in));
	saddr->sin_family = AF_INET;
	if ((saddr->sin_addr.s_addr = inet_addr(host)) == -1) {
		if ((hp = gethostbyname(host)) == NULL) {
			fprintf(stderr, "%s: unknown host\n", host);
			return 0;
		}
		(void) memcpy((char *)&saddr->sin_addr, hp->h_addr,
			hp->h_length);
	}
	return 1;
}


int
chknfsmnt(mlist)
/*
 * Ping the NFS server indicated by the given mnt entry
 */
register struct m_mlist *mlist;
{
	register char *s;
	register struct m_mlist *mlist2;
	CLIENT *client;
	struct sockaddr_in saddr;
	int sock, len;
	struct timeval tottimeout;
	struct timeval interval;
	unsigned short port = 0;
	struct pmap pmap;
	enum clnt_stat rpc_stat;
	static char p[MAXPATHLEN];

	if (Dflg)
		fprintf(stderr, "chknfsmnt(%s)\n", mlist->mlist_fsname);

	if (mlist->mlist_checked) /* if already checked this mount point */
		return (mlist->mlist_checked);

	/*
	 * Save path to working storage and strip colon
	 */
	(void) strncpy(p, mlist->mlist_fsname, sizeof(p)-1);
	if ((s = strchr(p, ':')) != NULL)
		*s = '\0';
	len = strlen(p);

	/*
	 * See if remote host already checked via another mount point
	 */
	for (mlist2 = firstmnt; mlist2 != NULL; mlist2 = mlist2->mlist_next)
		if (strncmp(mlist2->mlist_fsname, p, len) == 0 
				&& mlist2->mlist_checked)
			return(mlist2->mlist_checked);

	mlist->mlist_checked = -1; /* set failed */
	if (vflg)
		fprintf(stderr, "Checking %s..\n", p);
	interval.tv_sec = 2;  /* retry interval */
	interval.tv_usec = 0;

	/*
	 * Parse internet address
	 */
	if (get_inaddr(&saddr, p) == 0)
		return 0;
	/*
	 * Get socket to remote portmapper
	 */
	saddr.sin_port = htons(PMAPPORT);
	sock = RPC_ANYSOCK;
	if ((client = clntudp_create(&saddr, PMAPPROG, PMAPVERS, interval, 
			&sock)) == NULL) {
		clnt_pcreateerror(p);
		return 0;
	}
	/*
	 * Query portmapper for port # of NFS server
	 */
	pmap.pm_prog = NFS_PROGRAM;
	pmap.pm_vers = NFS_VERSION;
	pmap.pm_prot = IPPROTO_UDP;
	pmap.pm_port = 0;
	tottimeout.tv_sec = timeout;  /* total timeout */
	tottimeout.tv_usec = 0;
	if ((rpc_stat = clnt_call(client, PMAPPROC_GETPORT, xdr_pmap, &pmap,
			xdr_u_short, &port, tottimeout)) != RPC_SUCCESS) {
		clnt_perror(client, p);
		clnt_destroy(client);
		return 0;
	}
	clnt_destroy(client);

	if (port == 0) {
		fprintf(stderr, "%s: NFS server not registered\n", p);
		return 0;
	}
	/*
	 * Get socket to NFS server
	 */
	saddr.sin_port = htons(port);
	sock = RPC_ANYSOCK;
	if ((client = clntudp_create(&saddr, NFS_PROGRAM, NFS_VERSION,
			interval, &sock)) == NULL) {
		clnt_pcreateerror(p);
		return 0;
	}
	/*
	 * Ping NFS server
	 */
	tottimeout.tv_sec = timeout;
	tottimeout.tv_usec = 0;
	if ((rpc_stat = clnt_call(client, NULLPROC, xdr_void, (char *)NULL,
			xdr_void, (char *)NULL, tottimeout)) != RPC_SUCCESS) {
		clnt_perror(client, p);
		clnt_destroy(client);
		return 0;
	}
	clnt_destroy(client);
	mlist->mlist_checked = 1; /* set success */
	if (vflg)
		fprintf(stderr, "%s ok\n", p);
	return 1;
}


char *
xalloc(size)
/*
 * Alloc memory with error checks
 */
int size;
{
	register char *mem;
#ifndef __STDC__
	char *malloc();
#endif
	
	if ((mem = (char *)malloc((unsigned)size)) == NULL) {
		(void) fprintf(stderr, "out of memory\n");
		exit(1);
	}
	return(mem);
}

/*
 * Begin machine dependent code for mount table 
 */

#if defined(sun) || defined(sgi) || defined(__hpux) || defined(NeXT)
#include <mntent.h>
void
mkm_mlist()
/*
 * Build list of mnt entries - Sun version
 */
{
	FILE *mounted;
	struct m_mlist *mlist;
	struct mntent *mnt;

	if ((mounted = setmntent(MOUNTED, "r"))== NULL) {
		perror(MOUNTED);
		exit(1);
	}
	while ((mnt = getmntent(mounted)) != NULL) {
		mlist = (struct m_mlist *)xalloc(sizeof(*mlist));
		mlist->mlist_next = firstmnt;
		mlist->mlist_checked = 0;
		mlist->mlist_dir = xalloc(strlen(mnt->mnt_dir)+1);
		(void) strcpy(mlist->mlist_dir, mnt->mnt_dir);
		mlist->mlist_fsname = xalloc(strlen(mnt->mnt_fsname)+1);
		(void) strcpy(mlist->mlist_fsname, mnt->mnt_fsname);
		mlist->mlist_isnfs = !strcmp(mnt->mnt_type, MNTTYPE_NFS);
		firstmnt = mlist;
	}
	(void) endmntent(mounted);
}
#elif defined(ultrix)
#include <sys/fs_types.h>
#include <sys/mount.h>
void
mkm_mlist()
/*
 * Build list of mnt entries - Ultrix version
 */
{
	struct m_mlist *mlist;
	struct fs_data fs_data;
	int start=0, len;

	while ((len = getmnt(&start, &fs_data, sizeof(fs_data), 
			NOSTAT_MANY, NULL)) > 0) {
		mlist = (struct m_mlist *)xalloc(sizeof(*mlist));
		mlist->mlist_next = firstmnt;
		mlist->mlist_checked = 0;
		mlist->mlist_dir = xalloc(strlen(fs_data.fd_path)+1);
		(void) strcpy(mlist->mlist_dir, fs_data.fd_path);
		mlist->mlist_fsname = 
			xalloc(strlen(fs_data.fd_devname)+1);
		(void) strcpy(mlist->mlist_fsname, fs_data.fd_devname);
		mlist->mlist_isnfs = (fs_data.fd_fstype == GT_NFS);
		firstmnt = mlist;
	}
	if (len < 0) {
		perror("getmnt");
		exit(1);
	}
}
#elif defined (__osf__)
#include <sys/mount.h>
void
mkm_mlist()
{
	struct statfs *fs;
	int i;
	int max;
	struct m_mlist *mlist;
	max = getfsstat ((struct statfs *)0, 0 , MNT_NOWAIT);
	fs = (struct statfs *) xalloc (sizeof(struct statfs)*max);
	max = getfsstat(fs, sizeof(struct statfs)*max, MNT_NOWAIT);
	for (i = 0; i < max; i++) {
		mlist = (struct m_mlist *)xalloc(sizeof(struct m_mlist));
		mlist->mlist_next = firstmnt;
		mlist->mlist_checked = 0;
		mlist->mlist_dir = xalloc (strlen (fs[i].f_mntonname) + 1);
		(void) strcpy (mlist->mlist_dir, fs[i].f_mntonname);
		mlist->mlist_fsname = xalloc (strlen (fs[i].f_mntfromname) + 1);
		(void) strcpy (mlist->mlist_fsname, fs[i].f_mntfromname);
		mlist->mlist_isnfs = (fs[i].f_type == MOUNT_NFS);
                firstmnt = mlist;
	}
	free((char *)fs);
}
#else
	UNDEFINED_mkm_mlist
#endif
