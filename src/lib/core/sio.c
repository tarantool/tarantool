/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sio.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <stdio.h>
#include <limits.h>
#include <netinet/in.h> /* TCP_NODELAY */
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <arpa/inet.h>
#include "say.h"
#include "tt_static.h"
#include "exception.h"
#include "uri/uri.h"
#include "errinj.h"

#if TARGET_OS_DARWIN
#include <sys/sysctl.h>
#endif

static_assert(SMALL_STATIC_SIZE > NI_MAXHOST + NI_MAXSERV,
	      "static buffer should fit host name");

/**
 * Safely print a socket description to the given buffer, with correct overflow
 * checks and all.
 */
static int
sio_socketname_to_buffer(int fd,
			 const struct sockaddr *base_addr, socklen_t addrlen,
			 const struct sockaddr *peer_addr, socklen_t peerlen,
			 char *buf, int size)
{
	int n = 0;
	(void)n;
	SNPRINT(n, snprintf, buf, size, "fd %d", fd);
	if (fd < 0)
		return 0;
	if (base_addr != NULL) {
		SNPRINT(n, snprintf, buf, size, ", aka ");
		SNPRINT(n, sio_addr_snprintf, buf, size, base_addr, addrlen);
	}
	if (peer_addr != NULL) {
		SNPRINT(n, snprintf, buf, size, ", peer of ");
		SNPRINT(n, sio_addr_snprintf, buf, size,
			peer_addr, peerlen);
	}
	return 0;
}

const char *
sio_socketname_addr(int fd, const struct sockaddr *base_addr,
		    socklen_t addrlen)
{
	/* Preserve errno */
	int save_errno = errno;
	int name_size = SERVICE_NAME_MAXLEN;
	char *name = static_alloc(name_size);

	struct sockaddr_storage peer_addr_storage;
	socklen_t peerlen = sizeof(peer_addr_storage);
	struct sockaddr *peer_addr = (struct sockaddr *)&peer_addr_storage;
	int rcp = getpeername(fd, peer_addr, &peerlen);
	if (rcp != 0) {
		peer_addr = NULL;
	}

	int rc = sio_socketname_to_buffer(fd,
					  base_addr, addrlen,
					  peer_addr, peerlen,
					  name, name_size);
	/*
	 * Could fail only because of a bad format in snprintf, but it is not
	 * bad, so should not fail.
	 */
	assert(rc == 0);
	(void)rc;
	/*
	 * Restore the original errno, it might have been reset by
	 * snprintf() or getsockname().
	 */
	errno = save_errno;
	return name;
}

const char *
sio_socketname(int fd)
{
	/* Preserve errno */
	int save_errno = errno;

	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	struct sockaddr *base_addr = (struct sockaddr *)&addr;
	int rcb = getsockname(fd, base_addr, &addrlen);
	if (rcb != 0) {
		base_addr = NULL;
	}

	const char *name = sio_socketname_addr(fd, base_addr, addrlen);
	/*
	 * Restore the original errno, it might have been reset by
	 * getsockname().
	 */
	errno = save_errno;
	return name;
}

/** Get a string representation of a socket option name,
 * for logging.
 */
static const char *
sio_option_name(int option)
{
#define CASE_OPTION(opt) case opt: return #opt
	switch (option) {
	CASE_OPTION(SO_KEEPALIVE);
	CASE_OPTION(SO_LINGER);
	CASE_OPTION(SO_ERROR);
	CASE_OPTION(SO_REUSEADDR);
	CASE_OPTION(TCP_NODELAY);
#ifdef __linux__
	CASE_OPTION(TCP_KEEPCNT);
	CASE_OPTION(TCP_KEEPINTVL);
#endif
	default:
		return "undefined";
	}
#undef CASE_OPTION
}

/** Try to automatically configure a listen backlog.
 * On Linux, use the system setting, which defaults
 * to 128. This way a system administrator can tune
 * the backlog as needed. On other systems, use SOMAXCONN.
 */
static int
sio_listen_backlog()
{
#if TARGET_OS_LINUX
	FILE *proc = fopen("/proc/sys/net/core/somaxconn", "r");
	if (proc) {
		int backlog;
		int rc = fscanf(proc, "%d", &backlog);
		fclose(proc);
		if (rc == 1)
			return backlog;
	}
#elif TARGET_OS_DARWIN
	int somaxconn = 0;
	size_t size = sizeof(somaxconn);
	const char *name = "kern.ipc.somaxconn";
	int rc = sysctlbyname(name, &somaxconn, &size, NULL, 0);
	if (rc == 0) {
		/*
		 * From tests it appears that values > INT16_MAX
		 * work strangely. For example, 32768 behaves
		 * worse than 32767. Like if nothing was changed.
		 * The suspicion is that listen() on Mac
		 * internally uses int16_t or 'short' for storing
		 * the queue size and it simply gets reset to
		 * default on bigger values.
		 */
		if (somaxconn > INT16_MAX) {
			say_warn("%s is too high (%d), "
				 "truncated to %d", name,
				 somaxconn, (int)INT16_MAX);
			somaxconn = INT16_MAX;
		}
		return somaxconn;
	}
	say_syserror("couldn't get system's %s setting", name);
#endif
	return SOMAXCONN;
}

int
sio_socket(int domain, int type, int protocol)
{
	/* AF_UNIX can't use tcp protocol */
	if (domain == AF_UNIX)
		protocol = 0;
	int fd = socket(domain, type, protocol);
	if (fd < 0)
		diag_set(SocketError, sio_socketname(fd), "socket");
	return fd;
}

int
sio_getfl(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		diag_set(SocketError, sio_socketname(fd),
			 "fcntl(..., F_GETFL, ...)");
	return flags;
}

int
sio_setfl(int fd, int flag, int on)
{
	int flags = sio_getfl(fd);
	if (flags < 0)
		return flags;
	flags = fcntl(fd, F_SETFL, on ? flags | flag : flags & ~flag);
	if (flags < 0)
		diag_set(SocketError, sio_socketname(fd),
			 "fcntl(..., F_SETFL, ...)");
	return flags;
}

int
sio_setsockopt(int fd, int level, int optname,
	       const void *optval, socklen_t optlen)
{
	int rc = setsockopt(fd, level, optname, optval, optlen);
	if (rc) {
		diag_set(SocketError, sio_socketname(fd),
			  "setsockopt(%s)", sio_option_name(optname));
	}
	return rc;
}

int
sio_getsockopt(int fd, int level, int optname,
	       void *optval, socklen_t *optlen)
{
	int rc = getsockopt(fd, level, optname, optval, optlen);
	if (rc) {
		diag_set(SocketError, sio_socketname(fd), "getsockopt(%s)",
			 sio_option_name(optname));
	}
	return rc;
}

int
sio_connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	/* Establish the connection. */
	int rc = connect(fd, (struct sockaddr *) addr, addrlen);
	if (rc < 0 && errno != EINPROGRESS) {
		diag_set(SocketError, sio_socketname(fd), "connect to %s",
			 sio_strfaddr((struct sockaddr *)addr, addrlen));
	}
	return rc;
}

int
sio_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	int rc = bind(fd, addr, addrlen);
	if (rc < 0)
		diag_set(SocketError,
			 sio_socketname_addr(fd, addr, addrlen),
			 "bind");
	return rc;
}

int
sio_listen(int fd)
{
	int rc = listen(fd, sio_listen_backlog());
	if (rc < 0)
		diag_set(SocketError, sio_socketname(fd), "listen");
	return rc;
}

int
sio_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	/* Accept a connection. */
	int newfd = accept(fd, addr, addrlen);
	if (newfd < 0 && !sio_wouldblock(errno))
		diag_set(SocketError, sio_socketname(fd), "accept");
	return newfd;
}

ssize_t
sio_read(int fd, void *buf, size_t count)
{
	struct errinj *inj = errinj(ERRINJ_SIO_READ_MAX, ERRINJ_INT);
	if (inj != NULL && inj->iparam > 0)
		count = MIN(count, (size_t)inj->iparam);

	ssize_t n = read(fd, buf, count);
	if (n < 0 && !sio_wouldblock(errno)) {
		/*
		 * Happens typically when the client closes
		 * socket on timeout without reading the previous
		 * query's response completely. Treat the same as
		 * EOF.
		 */
		if (errno == ECONNRESET) {
			errno = 0;
			n = 0;
		} else {
			diag_set(SocketError, sio_socketname(fd),
				  "read(%zd)", count);
		}
	}
	return n;
}

ssize_t
sio_write(int fd, const void *buf, size_t count)
{
	assert(count); /* count == 0 is most likely a software bug. */
	ssize_t n = write(fd, buf, count);
	if (n < 0 && !sio_wouldblock(errno))
		diag_set(SocketError, sio_socketname(fd), "write(%zd)", count);
	return n;
}

ssize_t
sio_writev(int fd, const struct iovec *iov, int iovcnt)
{
	int cnt = iovcnt < IOV_MAX ? iovcnt : IOV_MAX;
	ssize_t n = writev(fd, iov, cnt);
	if (n < 0 && !sio_wouldblock(errno))
		diag_set(SocketError, sio_socketname(fd), "writev(%d)", iovcnt);
	return n;
}

ssize_t
sio_sendto(int fd, const void *buf, size_t len, int flags,
	   const struct sockaddr *dest_addr, socklen_t addrlen)
{
	ssize_t n = sendto(fd, buf, len, flags, (struct sockaddr*)dest_addr,
	                   addrlen);
	if (n < 0 && !sio_wouldblock(errno))
		diag_set(SocketError, sio_socketname(fd), "sendto(%zd)", len);
	return n;
}

ssize_t
sio_recvfrom(int fd, void *buf, size_t len, int flags,
	     struct sockaddr *src_addr, socklen_t *addrlen)
{
	ssize_t n = recvfrom(fd, buf, len, flags, (struct sockaddr*)src_addr,
	                     addrlen);
	if (n < 0 && !sio_wouldblock(errno))
		diag_set(SocketError, sio_socketname(fd), "recvfrom(%zd)", len);
	return n;
}

int
sio_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	if (getpeername(fd, addr, addrlen) < 0) {
		say_syserror("getpeername");
		return -1;
	}
	return 0;
}

int
sio_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	if (getsockname(fd, addr, addrlen) < 0) {
		diag_set(SocketError, sio_socketname(fd), "getsockname");
		return -1;
	}
	return 0;
}

int
sio_addr_snprintf(char *buf, size_t size, const struct sockaddr *addr,
		  socklen_t addrlen)
{
	int res;
	if (addr->sa_family == AF_UNIX) {
		struct sockaddr_un *u = (struct sockaddr_un *)addr;
		if (addrlen >= sizeof(*u))
			res = snprintf(buf, size, "unix/:%s", u->sun_path);
		else
			res = snprintf(buf, size, "unix/:(socket)");
	} else {
		char host[NI_MAXHOST], serv[NI_MAXSERV];
		int flags = NI_NUMERICHOST | NI_NUMERICSERV;
		if (getnameinfo(addr, addrlen, host, sizeof(host), serv,
				sizeof(serv), flags) != 0)
			res = snprintf(buf, size, "(host):(port)");
		else if (addr->sa_family == AF_INET)
			res = snprintf(buf, size, "%s:%s", host, serv);
		else
			res = snprintf(buf, size, "[%s]:%s", host, serv);
	}
	assert(res + 1 < SERVICE_NAME_MAXLEN);
	assert(res >= 0);
	return res;
}

const char *
sio_strfaddr(const struct sockaddr *addr, socklen_t addrlen)
{
	int size = SERVICE_NAME_MAXLEN;
	char *buf = (char *) static_reserve(size);
	/* +1 for terminating 0. */
	static_alloc(sio_addr_snprintf(buf, size, addr, addrlen) + 1);
	return buf;
}

int
sio_uri_to_addr(const char *uri, struct sockaddr *addr, bool *is_host_empty)
{
	struct uri u;
	if (uri_create(&u, uri) != 0 || u.service == NULL)
		goto invalid_uri;
	*is_host_empty = u.host == NULL;
	if (u.host != NULL && strcmp(u.host, URI_HOST_UNIX) == 0) {
		struct sockaddr_un *un = (struct sockaddr_un *) addr;
		if (strlen(u.service) + 1 > sizeof(un->sun_path))
			goto invalid_uri;
		strlcpy(un->sun_path, u.service, sizeof(un->sun_path));
		un->sun_family = AF_UNIX;
		uri_destroy(&u);
		return 0;
	}
	in_addr_t iaddr;
	if (u.host == NULL) {
		iaddr = htonl(INADDR_ANY);
	} else if (strcmp("localhost", u.host) == 0) {
		iaddr = htonl(INADDR_LOOPBACK);
	} else {
		iaddr = inet_addr(u.host);
		if (iaddr == (in_addr_t) -1)
			goto invalid_uri;
	}
	struct sockaddr_in *in = (struct sockaddr_in *) addr;
	int port = htons(atoi(u.service));
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_addr.s_addr = iaddr;
	in->sin_port = port;
	uri_destroy(&u);
	return 0;

invalid_uri:
	uri_destroy(&u);
	diag_set(SocketError, sio_socketname(-1), "invalid uri \"%s\"", uri);
	return -1;
}
