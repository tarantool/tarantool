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
#include "trivia/util.h"
#include "exception.h"
#include "uri/uri.h"

const char *
sio_socketname(int fd)
{
	/* Preserve errno */
	int save_errno = errno;
	static __thread char name[2 * SERVICE_NAME_MAXLEN];
	int n = snprintf(name, sizeof(name), "fd %d", fd);
	if (fd >= 0) {
		struct sockaddr_storage addr;
		socklen_t addrlen = sizeof(addr);
		int rc = getsockname(fd, (struct sockaddr *) &addr, &addrlen);
		if (rc == 0) {
			n += snprintf(name + n,
				sizeof(name) - n, ", aka %s",
				sio_strfaddr((struct sockaddr *)&addr,
								addrlen));
		}
		addrlen = sizeof(addr);
		rc = getpeername(fd, (struct sockaddr *) &addr, &addrlen);
		if (rc == 0) {
			n += snprintf(name + n, sizeof(name) - n,
				      ", peer of %s",
				      sio_strfaddr((struct sockaddr *)&addr,
								addrlen));
		}
	}
	/*
	 * Restore the original errno, it might have been reset by
	 * snprintf() or getsockname().
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
#ifdef TARGET_OS_LINUX
	FILE *proc = fopen("/proc/sys/net/core/somaxconn", "r");
	if (proc) {
		int backlog;
		int rc = fscanf(proc, "%d", &backlog);
		fclose(proc);
		if (rc == 1)
			return backlog;
	}
#endif /* TARGET_OS_LINUX */
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
sio_connect(int fd, struct sockaddr *addr, socklen_t addrlen)
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
sio_bind(int fd, struct sockaddr *addr, socklen_t addrlen)
{
	int rc = bind(fd, addr, addrlen);
	if (rc < 0 && errno != EADDRINUSE)
		diag_set(SocketError, sio_socketname(fd), "bind");
	return rc;
}

int
sio_listen(int fd)
{
	int rc = listen(fd, sio_listen_backlog());
	if (rc < 0 && errno != EADDRINUSE)
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

const char *
sio_strfaddr(const struct sockaddr *addr, socklen_t addrlen)
{
	static __thread char name[NI_MAXHOST + _POSIX_PATH_MAX + 2];
	switch (addr->sa_family) {
		case AF_UNIX:
			if (addrlen >= sizeof(struct sockaddr_un)) {
				snprintf(name, sizeof(name), "unix/:%s",
					((struct sockaddr_un *)addr)->sun_path);
			} else {
				snprintf(name, sizeof(name),
					 "unix/:(socket)");
			}
			break;
		default: {
			char host[NI_MAXHOST], serv[NI_MAXSERV];
			if (getnameinfo(addr, addrlen, host, sizeof(host),
					serv, sizeof(serv),
					NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
				snprintf(name, sizeof(name),
					 addr->sa_family == AF_INET
					 ? "%s:%s" : "[%s]:%s", host, serv);
			} else {
				snprintf(name, sizeof(name), "(host):(port)");
			}
			break;
		}
	}
	return name;
}

int
sio_uri_to_addr(const char *uri, struct sockaddr *addr)
{
	struct uri u;
	if (uri_parse(&u, uri) != 0 || u.service == NULL)
		goto invalid_uri;
	if (u.host_len == strlen(URI_HOST_UNIX) &&
	    memcmp(u.host, URI_HOST_UNIX, u.host_len) == 0) {
		struct sockaddr_un *un = (struct sockaddr_un *) addr;
		if (u.service_len + 1 > sizeof(un->sun_path))
			goto invalid_uri;
		memcpy(un->sun_path, u.service, u.service_len);
		un->sun_path[u.service_len] = 0;
		un->sun_family = AF_UNIX;
		return 0;
	}
	in_addr_t iaddr;
	if (u.host_len == 0) {
		iaddr = htonl(INADDR_ANY);
	} else if (u.host_len == 9 && memcmp("localhost", u.host, 9) == 0) {
		iaddr = htonl(INADDR_LOOPBACK);
	} else {
		iaddr = inet_addr(tt_cstr(u.host, u.host_len));
		if (iaddr == (in_addr_t) -1)
			goto invalid_uri;
	}
	struct sockaddr_in *in = (struct sockaddr_in *) addr;
	int port = htons(atoi(u.service));
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_addr.s_addr = iaddr;
	in->sin_port = port;
	return 0;

invalid_uri:
	diag_set(SocketError, sio_socketname(-1), "invalid uri \"%s\"", uri);
	return -1;
}
