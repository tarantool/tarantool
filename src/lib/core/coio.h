#ifndef TARANTOOL_LIB_CORE_COIO_H_INCLUDED
#define TARANTOOL_LIB_CORE_COIO_H_INCLUDED
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
#include "fiber.h"
#include "trivia/util.h"

#include "evio.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct iostream;
struct sockaddr;

int
coio_connect_timeout(const char *host, const char *service, int host_hint,
		     struct sockaddr *addr, socklen_t *addr_len,
		     ev_tstamp timeout);

static inline int
coio_connect(const char *host, const char *service, int host_hint,
	     struct sockaddr *addr, socklen_t *addr_len)
{
	return coio_connect_timeout(host, service, host_hint, addr, addr_len,
				    TIMEOUT_INFINITY);
}

int
coio_accept(int sfd, struct sockaddr *addr, socklen_t addrlen,
	    ev_tstamp timeout);

ssize_t
coio_read_ahead_timeout(struct iostream *io, void *buf, size_t sz,
			size_t bufsiz, ev_tstamp timeout);

static inline void
coio_timeout_init(ev_tstamp *start, ev_tstamp *delay,
		  ev_tstamp timeout)
{
	return evio_timeout_init(loop(), start, delay, timeout);
}

static inline void
coio_timeout_update(ev_tstamp *start, ev_tstamp *delay)
{
	return evio_timeout_update(loop(), start, delay);
}

/**
 * Reat at least sz bytes, with readahead.
 *
 * Returns 0 in case of EOF, -1 in case of error.
 */
static inline ssize_t
coio_read_ahead(struct iostream *io, void *buf, size_t sz, size_t bufsiz)
{
	return coio_read_ahead_timeout(io, buf, sz, bufsiz, TIMEOUT_INFINITY);
}

ssize_t
coio_readn_ahead(struct iostream *io, void *buf, size_t sz, size_t bufsiz);

static inline ssize_t
coio_read(struct iostream *io, void *buf, size_t sz)
{
	return coio_read_ahead(io, buf, sz, sz);
}

static inline ssize_t
coio_read_timeout(struct iostream *io, void *buf, size_t sz, ev_tstamp timeout)
{
	return coio_read_ahead_timeout(io, buf, sz, sz, timeout);
}

static inline ssize_t
coio_readn(struct iostream *io, void *buf, size_t sz)
{
	return coio_readn_ahead(io, buf, sz, sz);
}

ssize_t
coio_readn_ahead_timeout(struct iostream *io, void *buf, size_t sz,
			 size_t bufsiz, ev_tstamp timeout);

static inline ssize_t
coio_readn_timeout(struct iostream *io, void *buf, size_t sz, ev_tstamp timeout)
{
	return coio_readn_ahead_timeout(io, buf, sz, sz, timeout);
}

ssize_t
coio_write_timeout(struct iostream *io, const void *buf, size_t sz,
		   ev_tstamp timeout);

static inline void
coio_write(struct iostream *io, const void *buf, size_t sz)
{
	coio_write_timeout(io, buf, sz, TIMEOUT_INFINITY);
}

ssize_t
coio_writev_timeout(struct iostream *io, struct iovec *iov, int iovcnt,
		    size_t size, ev_tstamp timeout);

static inline ssize_t
coio_writev(struct iostream *io, struct iovec *iov, int iovcnt, size_t size)
{
	return coio_writev_timeout(io, iov, iovcnt, size, TIMEOUT_INFINITY);
}

void
coio_stat_init(ev_stat *stat, const char *path);

/**
 * Wait for the stat data changes.
 * Returns 0 on event or timeout, -1 if the fiber was cancelled.
 */
int
coio_stat_stat_timeout(ev_stat *stat, ev_tstamp delay);

/**
 * Wait for a child to end.
 * The exit status is written to @a status.
 * Returns 0 on success, -1 if the fiber was cancelled.
 * This call only works in the main thread.
 */
int
coio_waitpid(pid_t pid, int *status);

/** \cond public */

enum {
	/** READ event */
	COIO_READ  = 0x1,
	/** WRITE event */
	COIO_WRITE = 0x2,
};

/**
 * Wait until READ or WRITE event on socket (\a fd). Yields.
 * \param fd non-blocking socket file description
 * \param event requested events to wait.
 * Combination of TNT_IO_READ | TNT_IO_WRITE bit flags.
 * \param timeout timeout in seconds.
 * \retval 0 timeout
 * \retval >0 returned events. Combination of TNT_IO_READ | TNT_IO_WRITE
 * bit flags.
 */
API_EXPORT int
coio_wait(int fd, int event, double timeout);

/**
 * Close the fd and wake any fiber blocked in
 * coio_wait() call on this fd.
 */
API_EXPORT int
coio_close(int fd);

/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_COIO_H_INCLUDED */
