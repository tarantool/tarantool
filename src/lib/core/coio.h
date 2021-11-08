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

struct sockaddr;
struct uri;

/**
 * Co-operative I/O
 * Yield the current fiber until IO is ready.
 */
struct coio_service
{
	struct evio_service evio_service;
	/* Fiber function. */
	fiber_func handler;
	/** Passed to the created fiber. */
	void *handler_param;
};

int
coio_connect_timeout(struct uri *uri, struct sockaddr *addr,
		     socklen_t *addr_len, ev_tstamp timeout);

static inline int
coio_connect(struct uri *uri, struct sockaddr *addr, socklen_t *addr_len)
{
	return coio_connect_timeout(uri, addr, addr_len, TIMEOUT_INFINITY);
}

void
coio_bind(struct ev_io *coio, struct sockaddr *addr,
	  socklen_t addrlen);

int
coio_accept(struct ev_io *coio, struct sockaddr *addr, socklen_t addrlen,
	    ev_tstamp timeout);

void
coio_create(struct ev_io *coio, int fd);

/*
 * Due to name conflict with coio_close in API_EXPORT
 * we have to use coio_close_io() instead of plain coio_close().
 */
static inline void
coio_close_io(ev_loop *loop, struct ev_io *coio)
{
	return evio_close(loop, coio);
}

ssize_t
coio_read_ahead_timeout(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz,
		        ev_tstamp timeout);

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
 * Returns 0 in case of EOF.
 */
static inline ssize_t
coio_read_ahead(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz)
{
	return coio_read_ahead_timeout(coio, buf, sz, bufsiz, TIMEOUT_INFINITY);
}

ssize_t
coio_readn_ahead(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz);

static inline ssize_t
coio_read(struct ev_io *coio, void *buf, size_t sz)
{
	return coio_read_ahead(coio, buf, sz, sz);
}

static inline ssize_t
coio_read_timeout(struct ev_io *coio, void *buf, size_t sz, ev_tstamp timeout)
{
	return coio_read_ahead_timeout(coio, buf, sz, sz, timeout);
}

/**
 * Read data with timeout.
 *
 * Yield until some data will be available for read.
 *
 * Returns amount of read bytes at success, otherwise returns -1
 * and set a diag.
 *
 * Zero return value means EOF.
 *
 * Note: Less then @a count bytes may be available for read at a
 * moment, so a return value less then @a count does not mean EOF.
 *
 * Possible errors:
 *
 * - SocketError: an IO error occurs at read().
 * - TimedOut: @a timeout quota is exceeded.
 * - FiberIsCancelled: cancelled by an outside code.
 */
ssize_t
coio_read_ahead_timeout_noxc(struct ev_io *coio, void *buf, size_t sz,
			     size_t bufsiz, ev_tstamp timeout);

static inline ssize_t
coio_readn(struct ev_io *coio, void *buf, size_t sz)
{
	return coio_readn_ahead(coio, buf, sz, sz);
}

ssize_t
coio_readn_ahead_timeout(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz,
		         ev_tstamp timeout);

ssize_t
coio_write_timeout(struct ev_io *coio, const void *buf, size_t sz,
		   ev_tstamp timeout);

/**
 * Write @a count bytes with timeout.
 *
 * Yield until all @a count bytes will be written.
 *
 * Returns @a count at success, otherwise returns -1 and set a
 * diag.
 *
 * Possible errors:
 *
 * - SocketError: an IO error occurs at write().
 * - TimedOut: @a timeout quota is exceeded.
 * - FiberIsCancelled: cancelled by an outside code.
 */
ssize_t
coio_write_timeout_noxc(struct ev_io *coio, const void *buf, size_t sz,
			ev_tstamp timeout);

static inline void
coio_write(struct ev_io *coio, const void *buf, size_t sz)
{
	coio_write_timeout(coio, buf, sz, TIMEOUT_INFINITY);
}

ssize_t
coio_writev_timeout(struct ev_io *coio, struct iovec *iov, int iovcnt,
		    size_t size, ev_tstamp timeout);

static inline ssize_t
coio_writev(struct ev_io *coio, struct iovec *iov, int iovcnt, size_t size)
{
	return coio_writev_timeout(coio, iov, iovcnt, size, TIMEOUT_INFINITY);
}

void
coio_service_init(struct coio_service *service, const char *name,
		  fiber_func handler, void *handler_param);

/** Wait until the service binds to the port. */
void
coio_service_start(struct evio_service *service, const char *uri);

void
coio_stat_init(ev_stat *stat, const char *path);

void
coio_stat_stat_timeout(ev_stat *stat, ev_tstamp delay);

/**
 * Wait for a child to end.
 * @note this is a cancellation point (can throw
 * FiberIsCancelled).
 *
 * @retval exit status of the child.
 *
 * This call only works in the main thread.
 */
int
coio_waitpid(pid_t pid);

/** \cond public */

enum {
	/** READ event */
	COIO_READ  = 0x1,
	/** WRITE event */
	COIO_WRITE = 0x2,
};

/**
 * Wait until READ or WRITE event on socket (\a fd). Yields.
 * \param fd - non-blocking socket file description
 * \param events - requested events to wait.
 * Combination of TNT_IO_READ | TNT_IO_WRITE bit flags.
 * \param timeoout - timeout in seconds.
 * \retval 0 - timeout
 * \retval >0 - returned events. Combination of TNT_IO_READ | TNT_IO_WRITE
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

/**
 * Write to a socket in at most @a timeout seconds.
 * @param fd Socket descriptor.
 * @param data Data to write.
 * @param size Size of @a data.
 * @param timeout Timeout on the operation.
 *
 * @retval 0 Success.
 * @retval -1 Timeout or socket error.
 */
int
coio_write_fd_timeout(int fd, const void *data, size_t size, ev_tstamp timeout);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_COIO_H_INCLUDED */
