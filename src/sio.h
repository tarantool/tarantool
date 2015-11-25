#ifndef TARANTOOL_SIO_H_INCLUDED
#define TARANTOOL_SIO_H_INCLUDED
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
/**
 * Exception-aware wrappers around BSD sockets.
 * Provide better error logging and I/O statistics.
 */
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <tarantool_ev.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

const char *
sio_strfaddr(struct sockaddr *addr, socklen_t addrlen);

int
sio_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen);

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
sio_move_iov(struct iovec *iov, ssize_t nwr, size_t *iov_len)
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
sio_add_to_iov(struct iovec *iov, ssize_t size)
{
	iov->iov_len += size;
	iov->iov_base = (char *) iov->iov_base - size;
}

#if defined(__cplusplus)
} /* extern "C" */

#include "exception.h"
enum { SERVICE_NAME_MAXLEN = 32 };

extern const struct type type_SocketError;
class SocketError: public SystemError {
public:
	SocketError(const char *file, unsigned line, int fd,
		    const char *format, ...);
};

/** Close a file descriptor on exception or end of scope. */
struct FDGuard {
	int fd;
	explicit FDGuard(int fd_arg):fd(fd_arg) {}
	~FDGuard() { if (fd >= 0) close(fd); }
private:
	explicit FDGuard(const FDGuard&) = delete;
	FDGuard& operator=(const FDGuard&) = delete;
};

const char *sio_socketname(int fd);
int sio_socket(int domain, int type, int protocol);

int sio_shutdown(int fd, int how);

int sio_getfl(int fd);
int sio_setfl(int fd, int flag, int on);

void
sio_setsockopt(int fd, int level, int optname,
	       const void *optval, socklen_t optlen);
void
sio_getsockopt(int fd, int level, int optname,
	       void *optval, socklen_t *optlen);

int sio_connect(int fd, struct sockaddr *addr, socklen_t addrlen);
int sio_bind(int fd, struct sockaddr *addr, socklen_t addrlen);
int sio_listen(int fd);
int sio_listen_backlog();
int sio_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t sio_read(int fd, void *buf, size_t count);

ssize_t sio_write(int fd, const void *buf, size_t count);
ssize_t sio_writev(int fd, const struct iovec *iov, int iovcnt);

ssize_t sio_write_total(int fd, const void *buf, size_t count, size_t total);

/**
 * Read at least count and up to buf_size bytes from fd.
 * Throw exception on error or disconnect.
 *
 * @return the number of of bytes actually read.
 */
ssize_t
sio_readn_ahead(int fd, void *buf, size_t count, size_t buf_size);

/**
 * Read count bytes from fd.
 * Throw an exception on error or disconnect.
 *
 * @return count of bytes actually read.
 */
static inline ssize_t
sio_readn(int fd, void *buf, size_t count)
{
	return sio_readn_ahead(fd, buf, count, count);
}

/**
 * Write count bytes to fd.
 * Throw an exception on error or disconnect.
 *
 * @return count of bytes actually written.
 */
ssize_t
sio_writen(int fd, const void *buf, size_t count);

/* Only for blocked I/O */
ssize_t
sio_writev_all(int fd, struct iovec *iov, int iovcnt);

/**
 * A wrapper over sendfile.
 * Throw if send file failed.
 */
ssize_t
sio_sendfile(int sock_fd, int file_fd, off_t *offset, size_t size);

/**
 * Receive a file sent by sendfile
 * Throw if receiving failed
 */
ssize_t
sio_recvfile(int sock_fd, int file_fd, off_t *offset, size_t size);


ssize_t sio_sendto(int fd, const void *buf, size_t len, int flags,
		   const struct sockaddr *dest_addr, socklen_t addrlen);

ssize_t sio_recvfrom(int fd, void *buf, size_t len, int flags,
		     struct sockaddr *src_addr, socklen_t *addrlen);

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SIO_H_INCLUDED */
