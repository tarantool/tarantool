/*
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

#include <sys/uio.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <arpa/inet.h> /* inet_ntoa */

#include "say.h"

SocketError::SocketError(const char *file, unsigned line, int fd,
			 const char *format, ...)
	: SystemError(file, line)
{
	int save_errno = errno;

	char buf[TNT_ERRMSG_MAX];

	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	const char *socketname = sio_socketname(fd);
	init("%s, called on %s", buf, socketname);
	va_end(ap);
	errno = save_errno;
}

/** Pretty print socket name and peer (for exceptions) */
const char *
sio_socketname(int fd)
{
	static __thread char name[2 * SERVICE_NAME_MAXLEN];
	int n = snprintf(name, sizeof(name), "fd %d", fd);
	if (fd >= 0) {
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int rc = getsockname(fd, (struct sockaddr *) &addr, &addrlen);
		if (rc == 0) {
			n += snprintf(name + n, sizeof(name) - n,
				      ", aka %s", sio_strfaddr(&addr));
		}
		addrlen = sizeof(addr);
		rc = getpeername(fd, (struct sockaddr *) &addr, &addrlen);
		if (rc == 0) {
			n += snprintf(name + n, sizeof(name) - n,
				      ", peer of %s", sio_strfaddr(&addr));
		}
	}
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
	default:
		return "undefined";
	}
#undef CASE_OPTION
}

/** shut down part of a full-duplex connection */
int
sio_shutdown(int fd, int how)
{
	int rc = shutdown(fd, how);
	if (rc < 0)
		tnt_raise(SocketError, fd, "shutdown");
	return rc;
}

/** Try to automatically configure a listen backlog.
 * On Linux, use the system setting, which defaults
 * to 128. This way a system administrator can tune
 * the backlog as needed. On other systems, use SOMAXCONN.
 */
int
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

/** Create a TCP socket. */
int
sio_socket(int domain, int type, int protocol)
{
	int fd = socket(domain, type, protocol);
	if (fd < 0)
		tnt_raise(SocketError, fd, "socket");
	return fd;
}

/** Get socket flags, raise an exception if error. */
int
sio_getfl(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		tnt_raise(SocketError, fd, "fcntl(..., F_GETFL, ...)");
	return flags;
}

/** Set socket flags, raise an exception if error. */
int
sio_setfl(int fd, int flag, int on)
{
	int flags = sio_getfl(fd);
	flags = fcntl(fd, F_SETFL, on ? flags | flag : flags & ~flag);
	if (flags < 0)
		tnt_raise(SocketError, fd, "fcntl(..., F_SETFL, ...)");
	return flags;
}

/** Set an option on a socket. */
void
sio_setsockopt(int fd, int level, int optname,
	       const void *optval, socklen_t optlen)
{
	int rc = setsockopt(fd, level, optname, optval, optlen);
	if (rc) {
		tnt_raise(SocketError, fd, "setsockopt(%s)",
			  sio_option_name(optname));
	}
}

/** Get a socket option value. */
void
sio_getsockopt(int fd, int level, int optname,
	       void *optval, socklen_t *optlen)
{
	int rc = getsockopt(fd, level, optname, optval, optlen);
	if (rc) {
		tnt_raise(SocketError, fd, "getsockopt(%s)",
			  sio_option_name(optname));
	}
}

/** Connect a client socket to a server. */
int
sio_connect(int fd, struct sockaddr_in *addr, socklen_t addrlen)
{
	/* Establish the connection. */
	int rc = connect(fd, (struct sockaddr *) addr, addrlen);
	if (rc < 0 && errno != EINPROGRESS) {
		tnt_raise(SocketError, fd, "connect");
	}
	return rc;
}

/** Bind a socket to the given address. */
int
sio_bind(int fd, struct sockaddr_in *addr, socklen_t addrlen)
{
	int rc = bind(fd, (struct sockaddr *) addr, addrlen);
	if (rc < 0 && errno != EADDRINUSE)
		tnt_raise(SocketError, fd, "bind");
	return rc;
}

/** Mark a socket as accepting connections.  */
int
sio_listen(int fd)
{
	int rc = listen(fd, sio_listen_backlog());
	if (rc < 0 && errno != EADDRINUSE)
		tnt_raise(SocketError, fd, "listen");
	return rc;
}

/** Accept a client connection on a server socket. */
int
sio_accept(int fd, struct sockaddr_in *addr, socklen_t *addrlen)
{
	/* Accept a connection. */
	int newfd = accept(fd, (struct sockaddr *) addr, addrlen);
	if (newfd < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		tnt_raise(SocketError, fd, "accept");
	return newfd;
}

/** Read up to 'count' bytes from a socket. */
ssize_t
sio_read(int fd, void *buf, size_t count)
{
	ssize_t n = read(fd, buf, count);
	if (n < 0 && errno != EAGAIN &&
	    errno != EWOULDBLOCK && errno != EINTR)
			tnt_raise(SocketError, fd, "read(%zd)", count);
	return n;
}

/** Write up to 'count' bytes to a socket. */
ssize_t
sio_write(int fd, const void *buf, size_t count)
{
	ssize_t n = write(fd, buf, count);
	if (n < 0 && errno != EAGAIN &&
	    errno != EWOULDBLOCK && errno != EINTR)
			tnt_raise(SocketError, fd, "write(%zd)", count);
	return n;
}

/** Write to a socket with iovec. */
ssize_t
sio_writev(int fd, const struct iovec *iov, int iovcnt)
{
	int cnt = iovcnt < IOV_MAX ? iovcnt : IOV_MAX;
	ssize_t n = writev(fd, iov, cnt);
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
	    errno != EINTR) {
		tnt_raise(SocketError, fd, "writev(%d)", iovcnt);
	}
	return n;
}

/** Send a message on a socket. */
ssize_t
sio_sendto(int fd, const void *buf, size_t len, int flags,
	   const struct sockaddr_in *dest_addr, socklen_t addrlen)
{
	ssize_t n = sendto(fd, buf, len, flags, (struct sockaddr*)dest_addr,
	                   addrlen);
	if (n < 0 && errno != EAGAIN &&
	    errno != EWOULDBLOCK && errno != EINTR)
			tnt_raise(SocketError, fd, "sendto(%zd)", len);
	return n;
}

/** Receive a message on a socket. */
ssize_t
sio_recvfrom(int fd, void *buf, size_t len, int flags,
	     struct sockaddr_in *src_addr, socklen_t *addrlen)
{
	ssize_t n = recvfrom(fd, buf, len, flags, (struct sockaddr*)src_addr,
	                     addrlen);
	if (n < 0 && errno != EAGAIN &&
	    errno != EWOULDBLOCK && errno != EINTR)
			tnt_raise(SocketError, fd, "recvfrom(%zd)", len);
	return n;
}

/** Get socket peer name. */
int
sio_getpeername(int fd, struct sockaddr_in *addr)
{
	socklen_t addrlen = sizeof(struct sockaddr_in);
	if (getpeername(fd, (struct sockaddr *) addr, &addrlen) < 0) {
		say_syserror("getpeername");
		return -1;
	}
	/* XXX: I've no idea where this is copy-pasted from. */
	if (addr->sin_addr.s_addr == 0) {
		say_syserror("getpeername: empty peer");
		return -1;
	}
	return 0;
}

/** Pretty print a peer address. */
const char *
sio_strfaddr(struct sockaddr_in *addr)
{
	static __thread char name[SERVICE_NAME_MAXLEN];
	snprintf(name, sizeof(name), "%s:%d",
		 inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
	return name;
}
