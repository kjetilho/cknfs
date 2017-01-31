/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 * cknfs - Check for dead NFS servers
 *
 * Don't you hate it when you login on an NFS client, only to find
 * yourself hung because one of your execution paths points to a dead
 * NFS server?
 *
 * Well, this program fixes that problem.  It takes a list of execution
 * paths as arguments.  Each path is examined for an NFS mount point.
 * If found, the corresponding NFS server is checked.  Paths that lead
 * to dead NFS servers are ignored.  The remaining paths are printed to
 * stdout.  No more hung logins!
 *
 * Usage: cknfs -e -s -t# -u -v -D -L paths
 *  
 *	 -e	silent, do not print paths
 *	 -f	accept any type of file, not just directories
 *	 -s	print paths in sh format (colons)
 *	 -t n	timeout interval before assuming an NFS
 *		server is dead (default 10 seconds)
 *	 -u	unique paths
 *	 -v	verbose
 *	 -D	debug
 *	 -L	expand symbolic links
 *	 -H	print hostname pinged.
 *
 * Typical examples:
 *
 *	set path = `cknfs /bin /usr/bin /usr/ucb . /usr6/bin /sdg/bin`
 *	alias cd 'cknfs -e \!*; if ($status == 0) chdir \!*'
 *
 * The latter example prevents you from hanging if you cd to a
 * directory that leads to a dead NFS server.
 *
 * Adminstrative note: You can still get hung if your administrator
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
 * Initial program
 * May 1989, Alan Klietz (aklietz@ncsa.uiuc.edu)
 *
 * Additional modifications made 1990-2006, University of Oslo
 */

#include <sys/param.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#define PORTMAP
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <assert.h>
#include <sys/select.h>

#if defined(sgi)
  /* sgi is missing nfs.h, so we must hardcode the RPC values */
# define NFS_PROGRAM 100003L
#else
# ifdef __osf__
#  include <sys/mount.h>
# endif
# include <nfs/nfs.h>
#endif

#define DEFAULT_TIMEOUT 5  /* Default timeout for checking NFS server */

#ifndef __STDC__
extern char *realloc(), *malloc();
extern char *strchr(), *strrchr(), *strtok();
#endif

#ifndef INADDR_NONE
/* old OS without INADDR_NONE probably don't have in_addr_t either */
# define INADDR_NONE ((unsigned int)-1)
#endif

struct m_mlist {
	int mlist_checked; /* -1 if bad, 0 if not checked, 1 if ok */
	struct m_mlist *mlist_next;
	char *mlist_dir;
	char *mlist_fsname;
	int mlist_isnfs;
	int mlist_pid;	/* if pid is set, only check automount process */
	int nfs_version;
	int proto;
	struct addrinfo *mountaddr;
};
static struct m_mlist *firstmnt;

static int errflg;
static int eflg, fflg, qflg, sflg, vflg, Dflg, Hflg, Lflg, uflg;
static int timeout = DEFAULT_TIMEOUT;
static int nfs_version = 3;
static char prefix[MAXPATHLEN];
void mkm_mlist();

void *
xalloc(size)
/*
 * Alloc memory with error checks
 */
int size;
{
	char *mem;
	
	if ((mem = (char *)malloc((unsigned)size)) == NULL) {
		(void) fprintf(stderr, "out of memory\n");
		exit(1);
	}
	return(mem);
}

void *
xrealloc(orig, size)
/*
 * Realloc memory with error checks
 */
void *orig;
int size;
{
	char *mem;

	if (orig == NULL)
		return(xalloc(size));

	if ((mem = (char *)realloc(orig, (unsigned)size)) == NULL) {
		(void) fprintf(stderr, "out of memory\n");
		exit(1);
	}
	return(mem);
}

int
unique(path)
char *path;
{
	static int n = -1;
	static int hist_size = 0;
	static char **hist = NULL;
	int i;

	if (!uflg)
		return 1;

	if (++n >= hist_size) {
		hist_size += 32;
		hist = xrealloc(hist, hist_size * sizeof (char *));
	}
	hist[n] = xalloc(strlen(path) + 1);
	strcpy(hist[n], path);
	for (i = 0; i < n; i++)
		if (hist[i] && strcmp(hist[i], hist[n]) == 0) {
			--n;
			return 0;
		}
	return 1;
}

static int
check_automount(mlist)
/*
 * Probe the automounter process and see if it's alive
 */
struct m_mlist *mlist;
{
#ifdef linux
	char procfile[32];
	char statline[512];
	char *line = statline;
	int statfd;
	int field = 2;

	if (Dflg)
		fprintf(stderr, "check_automount %d\n", mlist->mlist_pid);
	sprintf(procfile, "/proc/%d/stat", mlist->mlist_pid);
	statfd = open(procfile, O_RDONLY);
	if (statfd < 0) {
		if (vflg)
			fprintf(stderr, "Process %d is dead\n",
				mlist->mlist_pid);
		mlist->mlist_checked = -1;
		return -1;
	}
	read(statfd, statline, sizeof(statline)-1);
	statline[sizeof(statline)-1] = 0;
	while (*line && field > 0) {
		if (*line++ == ' ')
			--field;
	}
	if (Dflg)
		fprintf(stderr, "process state %c\n", *line);
	/* D is disk wait.  T is stopped.  others? */
	if (*line == 'D' || *line == 'T')
		mlist->mlist_checked = -1;
	else
		mlist->mlist_checked = 1;
	close(statfd);
#else
	/* mlist_pid can't be populated other than on Linux, but we
	   add code for future portability anyway. */
	if (kill(0, mlist->mlist_pid) < 0 && errno == ESRCH)
		mlist->mlist_checked = -1;
	else
		mlist->mlist_checked = 1;
#endif
	return mlist->mlist_checked;
}

static int
translate_hostname(host, proto, result)
/*
 * Translate host name to Internet address.
 * Return 1 if ok, 0 if error
 */
const char *host;
int proto;
struct addrinfo **result;
{
        struct addrinfo hints;
        int ret;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        switch (proto) {
        case IPPROTO_UDP:
                hints.ai_socktype = SOCK_DGRAM; break;
        case IPPROTO_TCP:
                hints.ai_socktype = SOCK_STREAM; break;
        }
        hints.ai_flags = AI_ADDRCONFIG;
	if (Dflg)
		fprintf(stderr, "looking up %s\n", host);
        ret = getaddrinfo(host, NULL, &hints, result);
        if (ret != 0) {
                fprintf(stderr, "%s: getaddrinfo returned %s\n",
                        host, gai_strerror(ret));
        }
        return ret == 0;
}

static int
translate_address(addr, proto, result)
/*
 * Translate textual IP address to addrinfo struct
 * Return 1 if ok, 0 if error
 */
const char *addr;
int proto;
struct addrinfo **result;
{
        struct addrinfo hints;
        char *copy = NULL;
        const char *address = addr;
        int ret;

	if (Dflg)
		fprintf(stderr, "translating %s\n", addr);

        if (addr[0] == '[') {
                int len;
                len = strlen(addr);
                if (addr[len] != ']') {
                        fprintf(stderr, "%s: malformed, expected to end with ]\n",
                                addr);
                        return 0;
                }
                copy = xalloc(len);
                strcpy(copy, addr);
                copy[len-1] = 0;
                address = copy;
        }
                        
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        switch (proto) {
        case IPPROTO_UDP:
                hints.ai_socktype = SOCK_DGRAM; break;
        case IPPROTO_TCP:
                hints.ai_socktype = SOCK_STREAM; break;
        }
        hints.ai_flags = AI_ADDRCONFIG;
        ret = getaddrinfo(address, NULL, &hints, result);
        if (ret != 0) {
                fprintf(stderr, "%s: getaddrinfo returned %s\n",
                        address, gai_strerror(ret));
        }
        if (copy != address)
                free(copy);
        return ret == 0;
}

/* portability note: Ultrix doesn't have clnt_create, so we wrap
   clntudp_create and clnttcp_create ourselves. */

static int
connected_socket(saddr, port, proto)
     struct sockaddr_in *saddr;
     int port, proto;
{
	int sock;
        int len;
        int flags;
        int socktype;
        char *protoname;

        if (proto == IPPROTO_UDP) {
                socktype = SOCK_DGRAM;
                protoname = "UDP";
        } else {
                socktype =SOCK_STREAM;
                protoname = "TCP";
        }

        if (Dflg)
                fprintf(stderr, "Creating IPv%d %s client, port is %d\n",
                        saddr->sin_family == AF_INET ? 4 : 6,
                        protoname,
                        port);

	saddr->sin_port = htons(port);
        if (saddr->sin_family == AF_INET6) {
                sock = socket(PF_INET6, socktype, proto);
                len = sizeof(struct sockaddr_in6);
        } else {
                sock = socket(PF_INET, socktype, proto);
                len = sizeof(struct sockaddr_in);
        }
        flags = fcntl(sock, F_GETFL);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        {
                int ret;
                ret = connect(sock, (struct sockaddr *) saddr, len);
                if (ret != 0 && errno == EINPROGRESS) {
                        struct timeval tv;
                        fd_set fds;

                        FD_ZERO(&fds);
                        tv.tv_sec = timeout;
                        tv.tv_usec = 0;

                        while (1) {
                                FD_SET(sock, &fds);
                                ret = select(sock+1, NULL, &fds, NULL, &tv);
                                if (ret == 0) {
                                        /* timeout */
                                        errno = ETIME;
                                        return -1;
                                } else if (ret > 0)
                                        break;
                        }
                }
        }
        fcntl(sock, F_SETFL, flags);
        return sock;
}

static CLIENT *
create_udp_client(saddr, port, prog, vers)
     struct sockaddr_in *saddr;
     int port, prog, vers;
{
	struct timeval interval;
	int sock = connected_socket(saddr, port, IPPROTO_UDP);
        if (sock == -1)
                return NULL;

	return clntudp_create(saddr, prog, vers, interval, &sock);
}

static CLIENT *
create_tcp_client(saddr, port, prog, vers)
     struct sockaddr_in *saddr;
     int port, prog, vers;
{
	int sock = connected_socket(saddr, port, IPPROTO_TCP);
        if (sock == -1)
                return NULL;

	return clnttcp_create(saddr, prog, vers, &sock, 0, 0);
}

static int
get_port_from_pmap(hostname, clntcreat, saddr, vers, proto)
	const char *hostname;
	CLIENT *(*clntcreat)();
	const struct sockaddr_in *saddr;
	int vers;
        int proto;
{
	CLIENT *client;
	struct pmap pmap;
	struct timeval tottimeout;
	unsigned short port = 0;

	/*
	 * Get socket to remote portmapper
	 */
	client = clntcreat(saddr, PMAPPORT, PMAPPROG, PMAPVERS);
	if (client == NULL) {
                if (rpc_createerr.cf_stat == RPC_SUCCESS)
                        fprintf(stderr, "%s portmapper: %s\n",
                                hostname, strerror(errno));
                else
                        clnt_pcreateerror(hostname);
		return 0;
	}

	/*
	 * Query portmapper for port # of NFS server
	 */
	pmap.pm_prog = NFS_PROGRAM;
	pmap.pm_vers = vers;
	pmap.pm_prot = proto == 0 ? IPPROTO_UDP : proto;
	pmap.pm_port = 0;

	if (Dflg)
		fprintf(stderr, "get port for NFS v%d (proto %ld) from portmapper\n",
			vers, pmap.pm_prot);

	tottimeout.tv_sec = timeout;  /* total timeout */
	tottimeout.tv_usec = 0;
	/* on Linux xdr_pmap and xdr_u_short have mismatched type
	   due to a header bug, so we add explicit casts */
	if (clnt_call(client, PMAPPROC_GETPORT, (xdrproc_t)xdr_pmap,
		      (caddr_t)&pmap, (xdrproc_t)xdr_u_short,
		      (caddr_t)&port, tottimeout) != RPC_SUCCESS) {
		clnt_perror(client, hostname);
		clnt_destroy(client);
		return 0;
	}
	clnt_destroy(client);

	if (port == 0) {
		fprintf(stderr, "%s: NFS server not registered\n", hostname);
		return 0;
	}
	return port;
}

static int
chknfsmntproto(hostname, clntcreat, mount)
     const char *hostname;
     CLIENT *(*clntcreat)();
     const struct m_mlist *mount;
{
	CLIENT *client;
	struct timeval tottimeout;
	unsigned short port = 0;
        struct addrinfo *rp;

	if (mount->nfs_version < 4) {
                rp = mount->mountaddr;
                while (rp) {
                        /* always use TCP for portmap queries */
                        port = get_port_from_pmap(hostname,
                                                  create_tcp_client,
                                                  (struct sockaddr_in *)rp->ai_addr,
                                                  mount->nfs_version,
                                                  mount->proto);
                        if (port)
                                break;
                        rp = rp->ai_next;
                }
                if (port == 0)
                        return 0;
                if (Dflg)
                        fprintf(stderr, "portmapper returned port %d\n", port);
	} else {
		port = 2049;
	}
	/*
	 * Get socket to NFS server
	 */
        rp = mount->mountaddr;
        while (rp) {
                client = clntcreat((struct sockaddr_in *)rp->ai_addr,
                                   port, NFS_PROGRAM, nfs_version);
                if (client)
                        break;
                rp = rp->ai_next;
	}
        if (client == NULL) {
                if (rpc_createerr.cf_stat == RPC_SUCCESS)
                        perror(hostname);
                else
                        clnt_pcreateerror(hostname);
		return 0;
        }
	/*
	 * Ping NFS server
	 */
	tottimeout.tv_sec = timeout;
	tottimeout.tv_usec = 0;
	/* on Linux xdr_void has mismatched type due to a header bug,
	   so we add explicit casts */
	if (clnt_call(client, NULLPROC, (xdrproc_t)xdr_void, NULL, (xdrproc_t)xdr_void, NULL,
		      tottimeout) != RPC_SUCCESS) {
		clnt_perror(client, hostname);
		clnt_destroy(client);
		return 0;
	}
	clnt_destroy(client);
	return 1;
}

int
chknfsmnt(mlist)
/*
 * Ping the NFS server indicated by the given mnt entry
 */
struct m_mlist *mlist;
{
	char *s;
	struct m_mlist *mlist2;
	int len;
	static char p[MAXPATHLEN];

	if (Dflg)
		fprintf(stderr, "chknfsmnt(%s)\n", mlist->mlist_fsname);

	if (mlist->mlist_checked) /* if already checked this mount point */
		return (mlist->mlist_checked);

	if (mlist->mlist_pid)
		return check_automount(mlist);

	/*
	 * Save path to working storage and strip colon
	 */
	(void) strncpy(p, mlist->mlist_fsname, sizeof(p)-1);
        if (p[0] == '[') {
                s = strchr(p, ']');
                assert(s);
                assert(s[1] == ':');
                s[1] = 0;
        } else if  ((s = strchr(p, ':')) != NULL)
		*s = '\0';
	len = strlen(p);

	if (Hflg)
		printf("%s ", p);

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

	/*
	 * Parse internet address
	 */
	if (!mlist->mountaddr) {
		if (translate_hostname(p, mlist->proto, &mlist->mountaddr) == 0)
			return 0;
	}

	if (mlist->proto) {
		if (!chknfsmntproto(p, mlist->proto == IPPROTO_UDP ?
				    create_udp_client : create_tcp_client,
				    mlist))
			return 0;
	} else {
		if (!chknfsmntproto(p, create_tcp_client, mlist) &&
		    !chknfsmntproto(p, create_udp_client, mlist))
			return 0;
	}

	mlist->mlist_checked = 1; /* set success */
	if (vflg)
		fprintf(stderr, "%s ok\n", p);
	return 1;
}

struct m_mlist *
isnfsmnt(path)
/*
 * Return 1 if path is NFS mount point
 */
char *path;
{
	struct m_mlist *mlist;
	static int init;

	if (init == 0) {
		++init;
		mkm_mlist();
	}

	if (Dflg)
		fprintf(stderr, "isnfsmnt(%s)\n", path);
	for (mlist = firstmnt; mlist != NULL; mlist = mlist->mlist_next) {
		if (mlist->mlist_isnfs == 0)
			continue;
		if (strcmp(mlist->mlist_dir, path) == 0) {
			if (Dflg)
				fprintf(stderr, "%s: contained in %s mounted from %s\n",
					path, mlist->mlist_dir, mlist->mlist_fsname);
			return(mlist);
		}
	}
	return NULL;
}

typedef void (*sighandler_t)(int);

#define NTERMS 256

jmp_buf alarmclock;

void
sigalrm(signum)
	int signum;
{
	if (Dflg)
		fprintf(stderr, "caught signal %d\n", signum);
	longjmp(alarmclock, 1);
}

int
_chkpath(path, maxdepth)
char *path;
int maxdepth;
{
	char *s, *s2;
	int i, front=0, back=0;
	struct stat stb;
	struct m_mlist *mlist;
	char p[MAXPATHLEN];
	char symlink[MAXPATHLEN];
	char *queue[NTERMS];

	if (maxdepth == 0) {
		fprintf(stderr,
			"%s: Too many levels of symbolic links\n", path);
		return 0;
	}
	/*
	 * Copy path to working storage
	 */
	strncpy(p, path, sizeof(p)-1);

	if (*p == '/') { /* If absolute path, start at root */
		*prefix = '\0';
		(void) chdir("/");
	}

	if (Dflg)
		fprintf(stderr, "_chkpath(%s, %d) prefix=%s\n",
			path, maxdepth, prefix);
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

	/* The code below may make some unsafe calls to stat, so set
	 * the alarm to catch problems.
	 */
	signal(SIGALRM, sigalrm);
	alarm(timeout + 1);
	if (setjmp(alarmclock))
		goto fail;

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
				goto fail;
		/* Check if symlink */
		if (lstat(s, &stb) < 0) {
			if (errno != ENOENT || !qflg)
				perror(prefix);
			goto fail;
		}
		if ((stb.st_mode & S_IFMT) != S_IFLNK) {
			/* not symlink */
			if (chdir(s) < 0) {
				if (fflg) {
					alarm(0);
					return 1;
				}
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

		if (_chkpath(symlink, maxdepth-1) == 0)
			goto fail;
	}
	alarm(0);
	return 1;

fail:
	alarm(0);
	return 0;
}
	
int
chkpath(path)
/*
 * Check path for accessibility.  Return 1 if ok, 0 if error
 */
char *path;
{
	char pwd[MAXPATHLEN];
	int ret;

	if (Dflg)
	    fprintf(stderr, "chkpath(%s)\n", path);

	if (getcwd(pwd, sizeof(pwd)-1) == NULL) {
	    perror("getcwd()");
	    return 0;
	}
	if (*path != '/')   /* If not absolute path, get initial prefix */
		strcpy(prefix, pwd);

	/* Allow maximum 64 levels of symbolic links */
	ret = _chkpath(path, 64);
	
	/* "/" becomes "", crude fix */
	if (prefix[0] == 0)
		strcpy(prefix, "/");

	/* restore cwd so relative paths work next time around */
	chdir(pwd);
	
	return ret;
}

/* Return point after '=' for opt in comma separated list */
const char *
find_opt_val(list, opt)
	const char *list, *opt;
{
	int optlen;
	char *search;
	const char *found;

	optlen = strlen(opt);
	search = xalloc(optlen + 2);
	strcpy(search + 1, opt);
	search[0] = ',';
	search[optlen+1] = '=';
	search[optlen+2] = 0;
	/* First check for first argument */
	if (strncmp(list, search+1, optlen+1) == 0)
		return list + optlen + 1;
	else if ((found = strstr(list, search)))
		return found + optlen + 2;
	else
		return NULL;
}

char *
copy_opt_val(list, opt)
	const char *list, *opt;
{
	const char *start;
	const char *comma;
	int length;
	char *copy;

	if ((start = find_opt_val(list, opt)) == NULL)
		return NULL;

	if ((comma = strchr(start, ',')))
		length = comma - start;
	else
		length = strlen(start);
	copy = xalloc(length);
	strncpy(copy, start, length);
	return copy;
}

/*
 * Begin machine dependent code for mount table 
 */

#if defined(sun) && defined(__SVR4)
#include <sys/mnttab.h>
void
mkm_mlist()
/*
 * Build list of mnt entries - Solaris version
 */
{
    FILE *mounted;
    struct m_mlist *mlist;
    struct mnttab mnt;
    FILE *mounts = fopen(MNTTAB, "r");
    int i;

    if (mounts == NULL) {
	perror(MNTTAB);
	exit (1);
    }

    do {
	i = getmntent(mounts, &mnt);
	if (i > 0) {
	    fprintf (stderr, "%s: getmntent returns %d\n", MNTTAB, i);
	    exit (1);
	} 
	if (i == -1) break;
	mlist = (struct m_mlist *)xalloc(sizeof(*mlist));
	mlist->mlist_next = firstmnt;
	mlist->mlist_checked = 0;
	mlist->mlist_dir = xalloc(strlen(mnt.mnt_mountp)+1);
	(void) strcpy(mlist->mlist_dir, mnt.mnt_mountp);
	mlist->mlist_fsname = xalloc(strlen(mnt.mnt_special)+1);
	(void) strcpy(mlist->mlist_fsname, mnt.mnt_special);
	if (strcmp(mnt.mnt_fstype, "nfs") == 0) {
	    mlist->mlist_isnfs = 1;
	} else {
	    mlist->mlist_isnfs = 0;
	}
	firstmnt = mlist;
    } while (1);
    (void) fclose(mounts);
}
#elif defined(sun) || defined(sgi) || defined(__hpux) || defined(NeXT) || defined(linux)
#include <mntent.h>
void
mkm_mlist()
/*
 * Build list of mnt entries - SunOS/IRIX/HP-UX/NeXTSTEP/Linux version
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
		mlist->mlist_pid = 0;
		mlist->nfs_version = 0;
		mlist->mountaddr = NULL;
		/* remember the local automounter with funny entry.  Linux only? */
		{
			char dummy1[MAXPATHLEN];
			int  pid;

			if (sscanf(mnt->mnt_fsname, "%[^(](pid%d)",
				   dummy1, &pid) == 2) {
				mlist->mlist_pid = pid;
			}
		}
		{
			const char *vers;
			if ((vers = find_opt_val(mnt->mnt_opts, "vers")))
				mlist->nfs_version = atoi(vers);
			else if ((vers = find_opt_val(mnt->mnt_opts, "nfsvers")))
				mlist->nfs_version = atoi(vers);
			if (vers && Dflg)
				fprintf(stderr, "%s: NFS version is %d\n",
					mnt->mnt_fsname, mlist->nfs_version);
		}
		{
			const char *proto;
			if ((proto = find_opt_val(mnt->mnt_opts, "proto")))
				mlist->proto = strncmp(proto, "tcp", 3) == 0 ?
					IPPROTO_TCP : IPPROTO_UDP;
		}
		{
			char *addr;
			if ((addr = copy_opt_val(mnt->mnt_opts, "mountaddr")) ||
			    (addr = copy_opt_val(mnt->mnt_opts, "addr"))) {
				if (Dflg)
					fprintf(stderr, "%s: mountaddr is %s\n",
						mnt->mnt_fsname, addr);
                                translate_address(addr, mlist->proto, &mlist->mountaddr);
			}
		}
		mlist->mlist_next = firstmnt;
		mlist->mlist_checked = 0;
		mlist->mlist_dir = xalloc(strlen(mnt->mnt_dir)+1);
		(void) strcpy(mlist->mlist_dir, mnt->mnt_dir);
		mlist->mlist_fsname = xalloc(strlen(mnt->mnt_fsname)+1);
		(void) strcpy(mlist->mlist_fsname, mnt->mnt_fsname);
		mlist->mlist_isnfs = !strcmp(mnt->mnt_type, MNTTYPE_NFS) ||
			!strcmp(mnt->mnt_type, "nfs4") ||
			(mlist->mlist_pid && !strcmp(mnt->mnt_type, "autofs"));
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


int
main(argc, argv)
int argc;
char **argv;
{
	int n;
	char *s;
	int good = 0;
	char outbuf[BUFSIZ];
	char errbuf[BUFSIZ];
	extern int optind;
	extern char *optarg;
	char **newargv;

	/*
	 * Avoid intermixing stdout and stderr
	 */
	setvbuf(stdout, outbuf, _IOFBF, sizeof(outbuf));
	setvbuf(stderr, errbuf, _IOLBF, sizeof(errbuf));

	while ((n = getopt(argc, argv, "efqst:uvDHL")) != EOF)
		switch(n) {
			case 'e':	++eflg;
					break;
			case 'f':	++fflg;
					break;
			case 'q':	++qflg;
					break;
			case 's':	++sflg;
					break;
			case 't':	timeout = atoi(optarg);
					break;
			case 'u':	++uflg;
					break;
			case 'v':	++vflg;
					break;
			case 'D':	++Dflg; ++vflg;
					break;
			case 'H':	++Hflg;
					break;
			case 'L':	++Lflg;
					break;
			default:	++errflg;
		}

	if (argc <= optind && !eflg) /* no paths */
		++errflg;

	if (errflg) {
		fprintf(stderr, "Usage: %s -e -f -q -s -t# -u -v -D -L paths\n",
			argv[0]);
		fprintf(stderr, "\tCheck paths for dead NFS servers\n");
		fprintf(stderr, "\tGood paths are printed to stdout\n\n");
		fprintf(stderr, "\t -e\tsilent, do not print paths\n");
		fprintf(stderr, "\t -f\taccept ordinary files\n");
		fprintf(stderr, "\t -q\tquiet, omit diagnostics about missing files\n");
		fprintf(stderr, "\t -s\tprint paths in sh format (semicolons)\n");
		fprintf(stderr, "\t -t n\ttimeout interval before assuming an NFS\n");
		fprintf(stderr, "\t\tserver is dead (default 5 seconds)\n");
		fprintf(stderr, "\t -u\tunique paths\n");
		fprintf(stderr, "\t -v\tverbose\n");
		fprintf(stderr, "\t -D\tdebug\n");
		fprintf(stderr, "\t -H\tprint host pinged\n");
		fprintf(stderr, "\t -L\texpand symbolic links\n\n");
		exit(1);
	}

	if (uflg)
		newargv = (char **) xalloc((argc - optind) * sizeof(char *));

	for (n = optind; n < argc; ++n) {
		char *colon = NULL;

		s = argv[n];
		do {
			if (sflg) {
				colon = strchr(s, ':');
				if (colon) *colon = '\0';
			}

			if (*s == '.') {
				if (!eflg) {
					if (good++)
						putchar(sflg ? ':' : ' ');
					fputs(s, stdout);
				}
			} else if (chkpath(s)) {
				if (unique(prefix)) {
					if (good++ && !eflg)
						putchar(sflg ? ':' : ' ');
					if (!eflg)
						fputs(Lflg ? prefix : s, stdout);
				}
			} else {
				if (uflg)
					newargv[n - optind] = NULL;
				if (vflg)
					fprintf(stderr, "path skipped: %s\n",
						Lflg && *s != '.' ? prefix : s);
			}
			if (! colon)
				break;	/* always taken if !sflg */
			s = colon + 1;
		} while (1);
	}

	if (good && !eflg)
		putchar('\n');

	(void) fflush(stderr);
	(void) fflush(stdout);

	exit(good == 0 && optind < argc );
}
