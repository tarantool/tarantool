#ifndef TARANTOOL_LIB_CORE_SIO_H_INCLUDED
#define TARANTOOL_LIB_CORE_SIO_H_INCLUDED
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
/**
 * A thin wrapper around BSD sockets. Sets the diagnostics
 * area with a nicely formatted message for most errors (some
 * intermittent errors such as EWOULDBLOCK, EINTR, EINPROGRESS,
 * EAGAIN are an exception to this). The API is following
 * suite of BSD socket API: most functinos -1 on error, 0 or a
 * valid file descriptor on success. Exceptions to this rule, once
 * again, are marked explicitly.
 */
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <tarantool_ev.h>
#include <errno.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/**
	 * - Unix socket path is 108 bytes max;
	 * - IP(v4, v6) max string len is 45;
	 *
	 * Max result is rounded up just in case the numbers are a bit different
	 * on various platforms.
	 */
	SERVICE_NAME_MAXLEN = 200,
};

/**
 * Check if an errno, returned from a sio function, means a
 * non-critical error: EAGAIN, EWOULDBLOCK, EINTR.
 */
static inline bool
sio_wouldblock(int err)
{
	return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}

/** Format the address into the given buffer. Behaves like snprintf(). */
int
sio_addr_snprintf(char *buf, size_t size, const struct sockaddr *addr,
		  socklen_t addrlen);

/**
 * Format the address provided in struct sockaddr *addr.
 * Returns result in a static thread-local buffer.
 * May garble errno. Used for error reporting.
 */
const char *
sio_strfaddr(const struct sockaddr *addr, socklen_t addrlen);

/**
 * Return a filled in struct sockaddr provided the file
 * descriptor. May garble the errno.
 *
 * @param[in] fd   a file descriptor; safe to hold any value,
 *                 but don't expect meaningful results for -1
 *
 * @param[out] addr    output buffer
 * @param[out] addrlen buffer length
 */
int
sio_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Fill @a addr with a real address currently bound to @a fd
 * socket.
 *
 * @param fd Socket.
 * @param[out] addr An address structure to fill.
 * @param[in][out] addlen On input it is a size of @a addr as a
 *                 buffer. On output it becomes a size of a new
 *                 content of @a addr.
 * @retval 0 Success.
 * @retval -1 Error.
 */
int
sio_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Advance write position in the iovec array
 * based on its current value and the number of
 * bytes written.
 *
 * @param[in]  iov        the vector being written with writev().
 * @param[in]  nwr        number of bytes written, @pre >= 0
 * @param[in,out] iov_len offset in iov[0];
 *
 * @return                offset of iov[0] for the next write
 */
static inline int
sio_move_iov(struct iovec *iov, size_t nwr, size_t *iov_len)
{
	nwr += *iov_len;
	struct iovec *begin = iov;
	while (nwr > 0 && nwr >= iov->iov_len) {
		nwr -= iov->iov_len;
		iov++;
	}
	*iov_len = nwr;
	return iov - begin;
}

/**
 * Change values of iov->iov_len and iov->iov_base
 * to adjust to a partial write.
 */
static inline void
sio_add_to_iov(struct iovec *iov, size_t size)
{
	iov->iov_len += size;
	iov->iov_base = (char *) iov->iov_base - size;
}

/**
 * Pretty print socket name and peer (for exceptions).
 * Preserves the errno. Returns a thread-local buffer.
 */
const char *sio_socketname(int fd);

/** Create a TCP or AF_UNIX socket. */
int sio_socket(int domain, int type, int protocol);

/** Get socket flags. */
int sio_getfl(int fd);

/** Set socket flags. */
int sio_setfl(int fd, int flag, int on);

/** Set an option on a socket. */
int
sio_setsockopt(int fd, int level, int optname,
	       const void *optval, socklen_t optlen);

/** Get a socket option value. */
int
sio_getsockopt(int fd, int level, int optname,
	       void *optval, socklen_t *optlen);

/**
 * Connect a client socket to a server.
 * The diagnostics is not set in case of EINPROGRESS.
 */
int sio_connect(int fd, struct sockaddr *addr, socklen_t addrlen);

/**
 * Bind a socket to the given address. The diagnostics is not set
 * in case of EADDRINUSE.
 */
int sio_bind(int fd, struct sockaddr *addr, socklen_t addrlen);

/**
 * Mark a socket as accepting connections. The diagnostics is not
 * set in case of EADDRINUSE.
 */
int sio_listen(int fd);

/**
 * Accept a client connection on a server socket. The
 * diagnostics is not set for inprogress errors (@sa
 * sio_wouldblock())
 */
int sio_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Read *up to* 'count' bytes from a socket.
 * The diagnostics is not set for sio_wouldblock() errors.
 */
ssize_t sio_read(int fd, void *buf, size_t count);

/**
 * Write up to 'count' bytes to a socket.
 * The diagnostics is not set in case of sio_wouldblock() errors.
 */
ssize_t sio_write(int fd, const void *buf, size_t count);

/**
 * Write to a socket with iovec.
 * The diagnostics is not set in case of sio_wouldblock() errors.
 */
ssize_t sio_writev(int fd, const struct iovec *iov, int iovcnt);

/**
 * Send a message on a socket.
 * The diagnostics is not set for sio_wouldblock() errors.
 */
ssize_t sio_sendto(int fd, const void *buf, size_t len, int flags,
		   const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * Receive a message on a socket.
 * The diagnostics is not set for sio_wouldblock() errors.
 */
ssize_t sio_recvfrom(int fd, void *buf, size_t len, int flags,
		     struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * Convert a string URI like "ip:port" or "unix/:path" to
 * sockaddr_in/un structure.
 */
int
sio_uri_to_addr(const char *uri, struct sockaddr *addr, bool *is_host_empty);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_SIO_H_INCLUDED */
