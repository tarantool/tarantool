/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <netinet/in.h> /* TCP_NODELAY */
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <arpa/inet.h> /* inet_ntoa */
#include <poll.h>
#include <unistd.h> /* lseek for sending file */
#include <sys/stat.h> /* fstat for sending file */
#ifdef TARGET_OS_LINUX
#include <sys/sendfile.h> /* sendfile system call */
#endif /* #ifdef TARGET_OS_LINUX */

#include "say.h"
#include "trivia/util.h"

const struct type type_SocketError = make_type("SocketError",
	&type_SystemError);
SocketError::SocketError(const char *file, unsigned line, int fd,
			 const char *format, ...)
	: SystemError(&type_SocketError, file, line)
{
	int save_errno = errno;

	char buf[DIAG_ERRMSG_MAX];

	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	const char *socketname = sio_socketname(fd);
	error_format_msg(this, "%s, called on %s", buf, socketname);
	errno = save_errno;
}

/** Pretty print socket name and peer (for exceptions) */
const char *
sio_socketname(int fd)
{
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
	/* AF_UNIX can't use tcp protocol */
	if (domain == AF_UNIX)
		protocol = 0;
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
sio_connect(int fd, struct sockaddr *addr, socklen_t addrlen)
{
	/* Establish the connection. */
	int rc = connect(fd, (struct sockaddr *) addr, addrlen);
	if (rc < 0 && errno != EINPROGRESS) {
		tnt_raise(SocketError, fd, "connect to %s",
			  sio_strfaddr((struct sockaddr *)addr, addrlen));
	}
	return rc;
}

/** Bind a socket to the given address. */
int
sio_bind(int fd, struct sockaddr *addr, socklen_t addrlen)
{
	int rc = bind(fd, addr, addrlen);
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
sio_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	/* Accept a connection. */
	int newfd = accept(fd, addr, addrlen);
	if (newfd < 0 &&
	    (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
		tnt_raise(SocketError, fd, "accept");
	return newfd;
}

/** Read up to 'count' bytes from a socket. */
ssize_t
sio_read(int fd, void *buf, size_t count)
{
	ssize_t n = read(fd, buf, count);
	if (n < 0) {
		if (errno == EWOULDBLOCK)
			errno = EINTR;
		switch (errno) {
		case EAGAIN:
		case EINTR:
			break;
		/*
		 * Happens typically when the client closes
		 * socket on timeout without reading the previous
		 * query's response completely. Treat the same as
		 * EOF.
		 */
		case ECONNRESET:
			errno = 0;
			n = 0;
			break;
		default:
			tnt_raise(SocketError, fd, "read(%zd)", count);
		}
	}
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

/** Blocking I/O writev */
ssize_t
sio_writev_all(int fd, struct iovec *iov, int iovcnt)
{
	ssize_t bytes_total = 0;
	struct iovec *iovend = iov + iovcnt;
	while (1) {
		int cnt = iovend - iov;
		if (cnt > IOV_MAX)
			cnt = IOV_MAX;
		ssize_t bytes_written = writev(fd, iov, cnt);
		if (bytes_written < 0) {
			if (errno == EINTR)
				continue;
			tnt_raise(SocketError, fd, "writev(%d)", cnt);
		}
		bytes_total += bytes_written;
		/*
		 * Check for iov < iovend, since otherwise
		 * if iovend->iov_len is 0, iov may go beyond
		 * iovend
		 */
		while (bytes_written >= iov->iov_len) {
			bytes_written -= (iov++)->iov_len;
			if (iov == iovend)
				break;
		}
		if (iov == iovend)
			break;
		iov->iov_base = (char *) iov->iov_base + bytes_written;
		iov->iov_len -= bytes_written;
	}
	return bytes_total;
}

ssize_t
sio_readn_ahead(int fd, void *buf, size_t count, size_t buf_size)
{
	size_t read_count = 0;
	while (read_count < count) {
		ssize_t read_res = read(fd, (char *) buf + read_count,
					buf_size - read_count);
		if (read_res < 0 && (errno == EWOULDBLOCK ||
				     errno == EINTR || errno == EAGAIN))
			continue;

		if (read_res <= 0)
			tnt_raise(SocketError, fd, "read (%zd)", count);

		read_count += read_res;
	}
	return read_count;
}

ssize_t
sio_writen(int fd, const void *buf, size_t count)
{
	size_t write_count = 0;
	while (write_count < count) {
		ssize_t write_res = write(fd, (char *) buf + write_count,
					  count - write_count);
		if (write_res < 0 && (errno == EWOULDBLOCK ||
				      errno == EINTR || errno == EAGAIN))
			continue;

		if (write_res <= 0)
			tnt_raise(SocketError, fd, "write (%zd)", count);

		write_count += write_res;
	}
	return write_count;
}

static inline off_t
sio_lseek(int fd, off_t offset, int whence)
{
	off_t res = lseek(fd, offset, whence);
	if (res == -1)
		tnt_raise(SocketError, fd, "lseek");
	return res;
}

#if defined(HAVE_SENDFILE_LINUX)
ssize_t
sio_sendfile(int sock_fd, int file_fd, off_t *offset, size_t size)
{
	ssize_t send_res = sendfile(sock_fd, file_fd, offset, size);
	if (send_res < size)
		tnt_raise(SocketError, sock_fd, "sendfile");
	return send_res;
}
#else
ssize_t
sio_sendfile(int sock_fd, int file_fd, off_t *offset, size_t size)
{
	if (offset)
		sio_lseek(file_fd, *offset, SEEK_SET);

	const size_t buffer_size = 8192;
	char buffer[buffer_size];
	size_t bytes_sent = 0;
	while (bytes_sent < size) {
		size_t to_send_now = MIN(size - bytes_sent, buffer_size);
		ssize_t n = sio_read(file_fd, buffer, to_send_now);
		sio_writen(sock_fd, buffer, n);
		bytes_sent += n;
	}

	if (offset)
		lseek(file_fd, *offset, SEEK_SET);

	return bytes_sent;
}
#endif

ssize_t
sio_recvfile(int sock_fd, int file_fd, off_t *offset, size_t size)
{
	if (offset)
		sio_lseek(file_fd, *offset, SEEK_SET);

	const size_t buffer_size = 8192;
	char buffer[buffer_size];
	size_t bytes_read = 0;
	while (bytes_read < size) {
		size_t to_read_now = MIN(size - bytes_read, buffer_size);
		ssize_t n = sio_read(sock_fd, buffer, to_read_now);
		sio_writen(file_fd, buffer, n);
		bytes_read += n;
	}

	if (offset)
		sio_lseek(file_fd, *offset, SEEK_SET);

	return bytes_read;
}

/** Send a message on a socket. */
ssize_t
sio_sendto(int fd, const void *buf, size_t len, int flags,
	   const struct sockaddr *dest_addr, socklen_t addrlen)
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
	     struct sockaddr *src_addr, socklen_t *addrlen)
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
sio_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	if (getpeername(fd, addr, addrlen) < 0) {
		say_syserror("getpeername");
		return -1;
	}
	/* XXX: I've no idea where this is copy-pasted from. */
	/*
	if (addr->sin_addr.s_addr == 0) {
		say_syserror("getpeername: empty peer");
		return -1;
	}
	*/
	return 0;
}

/** Pretty print a peer address. */
const char *
sio_strfaddr(struct sockaddr *addr, socklen_t addrlen)
{
	static __thread char name[NI_MAXHOST + _POSIX_PATH_MAX + 2];
	switch(addr->sa_family) {
		case AF_UNIX:
			if (addrlen >= sizeof(sockaddr_un)) {
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
