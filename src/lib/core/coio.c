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
#include "coio.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>

#include "iostream.h"
#include "sio.h"
#include "coio_task.h" /* coio_resolve() */
#include "uri/uri.h"

typedef void (*ev_stat_cb)(ev_loop *, ev_stat *, int);

/**
 * Connect to a host with a specified timeout.
 * @retval socket fd
 * @retval -1 error
 */
static int
coio_connect_addr(const struct sockaddr *addr, socklen_t len, ev_tstamp timeout)
{
	int fd = sio_socket(addr->sa_family, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (evio_setsockopt_client(fd, addr->sa_family, SOCK_STREAM) != 0)
		goto err;
	if (sio_connect(fd, addr, len) == 0)
		return fd;
	if (errno != EINPROGRESS)
		goto err;
	/*
	 * Wait until socket is ready for writing or
	 * timed out.
	 */
	int revents = coio_wait(fd, EV_WRITE, timeout);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		goto err;
	}
	if (revents == 0) {
		diag_set(TimedOut);
		goto err;
	}
	int error = EINPROGRESS;
	socklen_t sz = sizeof(error);
	if (sio_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &sz))
		goto err;
	if (error != 0) {
		errno = error;
		diag_set(SocketError, sio_socketname(fd), "connect");
		goto err;
	}
	return fd;
err:
	close(fd);
	return -1;
}

static int
coio_fill_addrinfo(struct addrinfo *ai_local, const char *host,
		   const char *service, int host_hint)
{
	int rc;
	ai_local->ai_next = NULL;
	if (host_hint == 1) { // IPv4
		ai_local->ai_addrlen = sizeof(struct sockaddr_in);
		ai_local->ai_addr = xmalloc(ai_local->ai_addrlen);
		memset(ai_local->ai_addr, 0, ai_local->ai_addrlen);
		struct sockaddr_in *addr = (void *)ai_local->ai_addr;
		addr->sin_family = AF_INET;
		addr->sin_port = htons((uint16_t)atoi(service));
		rc = inet_pton(AF_INET, host, &addr->sin_addr);
	} else { // IPv6
		ai_local->ai_addrlen = sizeof(struct sockaddr_in6);
		ai_local->ai_addr = xmalloc(ai_local->ai_addrlen);
		memset(ai_local->ai_addr, 0, ai_local->ai_addrlen);
		struct sockaddr_in6 *addr = (void *)ai_local->ai_addr;
		addr->sin6_family = AF_INET6;
		addr->sin6_port = htons((uint16_t)atoi(service));
		rc = inet_pton(AF_INET6, host, &addr->sin6_addr);
	}
	if (rc != 1) {
		diag_set(IllegalParams, "Invalid host name: %s", host);
		return -1;
	}
	return 0;
}

/**
 * Resolve \a host and \a service with optional \a host_hint and connect to
 * the first available address with a specified timeout.
 *
 * If \a addr is not NULL the function provides resolved address on success.
 * In this case, \a addr_len is a value-result argument. It should be
 * initialized to the size of the buffer associated with \a addr. Upon return,
 * \a addr_len is updated to contain the actual size of the source address.
 * The returned address is truncated if the buffer provided is too small;
 * in this case, addrlen will return a value greater than was supplied to the
 * call.
 *
 * This function also supports UNIX domain sockets: if \a host is 'unix/',
 * it will treat \a service as a path to a socket file.
 *
 * @retval socket fd
 * @retval -1 error
 */
int
coio_connect_timeout(const char *host, const char *service, int host_hint,
		     struct sockaddr *addr, socklen_t *addr_len,
		     ev_tstamp timeout)
{
	int fd = -1;
	/* try to resolve a hostname */
	struct ev_loop *loop = loop();
	ev_tstamp start, delay;
	evio_timeout_init(loop, &start, &delay, timeout);

	if (host != NULL && service != NULL &&
	    strcmp(host, URI_HOST_UNIX) == 0) {
		/* UNIX socket */
		struct sockaddr_un un;
		snprintf(un.sun_path, sizeof(un.sun_path), "%s", service);
		un.sun_family = AF_UNIX;
		fd = coio_connect_addr((struct sockaddr *)&un, sizeof(un),
				       delay);
		if (fd < 0)
			return -1;
		if (addr != NULL) {
			assert(addr_len != NULL);
			*addr_len = MIN(sizeof(un), *addr_len);
			memcpy(addr, &un, *addr_len);
		}
		return fd;
	}

	struct addrinfo *ai = NULL;
	struct addrinfo ai_local;
	if (host != NULL && service != NULL && host_hint != 0) {
		if (coio_fill_addrinfo(&ai_local, host, service,
				       host_hint) != 0)
			goto out;
		ai = &ai_local;
	} else {
		struct addrinfo hints;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG;
		int rc = coio_getaddrinfo(host, service, &hints, &ai, delay);
		if (rc != 0)
			return -1;
	}
	evio_timeout_update(loop(), &start, &delay);
	coio_timeout_init(&start, &delay, timeout);
	while (ai) {
		fd = coio_connect_addr(ai->ai_addr, ai->ai_addrlen, delay);
		if (fd >= 0) {
			if (addr != NULL) {
				assert(addr_len != NULL);
				*addr_len = MIN(ai->ai_addrlen, *addr_len);
				memcpy(addr, ai->ai_addr, *addr_len);
			}
			goto out; /* connected */
		}
		if (ai->ai_next == NULL)
			goto out;
		/* Ignore the error and try the next address. */
		ai = ai->ai_next;
		ev_now_update(loop);
		coio_timeout_update(&start, &delay);
	}
	diag_set(SocketError, sio_socketname(fd), "connection failed");
	return fd;
out:
	if (host_hint == 0)
		freeaddrinfo(ai);
	else
		free(ai_local.ai_addr);
	return fd;
}

/**
 * Wait a client connection on a server socket until
 * timedout.
 * @retval socket fd
 * @retval -1 error
 */
int
coio_accept(int sfd, struct sockaddr *addr, socklen_t addrlen,
	    ev_tstamp timeout)
{
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	while (true) {
		/* Assume that there are waiting clients
		 * available */
		int fd = sio_accept(sfd, addr, &addrlen);
		if (fd >= 0) {
			if (evio_setsockopt_client(fd, addr->sa_family,
						   SOCK_STREAM) != 0) {
				close(fd);
				return -1;
			}
			return fd;
		}
		if (!sio_wouldblock(errno))
			return -1;
		if (delay <= 0) {
			diag_set(TimedOut);
			return -1;
		}
		/*
		 * Yield control to other fibers until the
		 * timeout is reached.
		 */
		coio_wait(sfd, EV_READ, delay);
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
		coio_timeout_update(&start, &delay);
	}
}

/**
 * Read at least sz bytes from socket with readahead.
 *
 * In case of EOF returns the amount read until eof (possibly 0).
 * Can read up to bufsiz bytes.
 *
 * @retval the number of bytes read.
 * @retval -1 error.
 */
ssize_t
coio_read_ahead_timeout(struct iostream *io, void *buf, size_t sz,
			size_t bufsiz, ev_tstamp timeout)
{
	assert(sz <= bufsiz);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	ssize_t to_read = (ssize_t) sz;
	while (true) {
		/*
		 * Sic: assume the socket is ready: since
		 * the user called read(), some data must
		 * be expected.
		 */
		ssize_t nrd = iostream_read(io, buf, bufsiz);
		if (nrd > 0) {
			to_read -= nrd;
			if (to_read <= 0)
				return sz - to_read;
			buf = (char *) buf + nrd;
			bufsiz -= nrd;
			continue;
		} else if (nrd == 0) {
			return sz - to_read;
		} else if (nrd == IOSTREAM_ERROR) {
			return -1;
		}
		if (delay <= 0) {
			diag_set(TimedOut);
			return -1;
		}
		/*
		 * Yield control to other fibers until the
		 * timeout is being reached.
		 */
		coio_wait(io->fd, iostream_status_to_events(nrd), delay);
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
		coio_timeout_update(&start, &delay);
	}
}

/**
 * Read at least sz bytes, with readahead.
 *
 * Treats EOF as an error, and throws an exception.
 *
 * @retval the number of bytes read, > 0.
 * @retval -1 error.
 */
ssize_t
coio_readn_ahead(struct iostream *io, void *buf, size_t sz, size_t bufsiz)
{
	ssize_t nrd = coio_read_ahead(io, buf, sz, bufsiz);
	if (nrd >= 0 && nrd < (ssize_t)sz) {
		errno = EPIPE;
		diag_set(SocketError, sio_socketname(io->fd),
			 "unexpected EOF when reading from socket");
		return -1;
	}
	return nrd;
}

/**
 * Read at least sz bytes, with readahead and timeout.
 *
 * Treats EOF as an error, and throws an exception.
 *
 * @retval the number of bytes read, > 0.
 * @retval -1 error.
 */
ssize_t
coio_readn_ahead_timeout(struct iostream *io, void *buf, size_t sz,
			 size_t bufsiz, ev_tstamp timeout)
{
	ssize_t nrd = coio_read_ahead_timeout(io, buf, sz, bufsiz, timeout);
	if (nrd >= 0 && nrd < (ssize_t)sz) { /* EOF. */
		errno = EPIPE;
		diag_set(SocketError, sio_socketname(io->fd),
			 "unexpected EOF when reading from socket");
		return -1;
	}
	return nrd;
}

/**
 * Write sz bytes to socket.
 *
 * Throws SocketError in case of write error. If
 * the socket is not ready, yields the current
 * fiber until the socket becomes ready, until
 * all data is written.
 *
 * @retval the number of bytes written. Always
 * equal to @a sz.
 * @retval -1 error.
 */
ssize_t
coio_write_timeout(struct iostream *io, const void *buf, size_t sz,
		   ev_tstamp timeout)
{
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	ssize_t towrite = sz;
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	while (true) {
		/*
		 * Sic: write as much data as possible,
		 * assuming the socket is ready.
		 */
		ssize_t nwr = iostream_write(io, buf, towrite);
		if (nwr >= 0) {
			/* Go past the data just written. */
			if (nwr >= towrite)
				return sz;
			towrite -= nwr;
			buf = (char *) buf + nwr;
			continue;
		} else if (nwr == IOSTREAM_ERROR) {
			return -1;
		}
		if (delay <= 0) {
			diag_set(TimedOut);
			return -1;
		}
		/*
		 * Yield control to other fibers until the
		 * timeout is reached or the socket is
		 * ready.
		 */
		coio_wait(io->fd, iostream_status_to_events(nwr), delay);
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
		coio_timeout_update(&start, &delay);
	}
}

/*
 * Write iov using iostream API.
 * Put in an own function to workaround gcc bug with @finally
 */
static inline ssize_t
coio_flush(struct iostream *io, struct iovec *iov, ssize_t offset, int iovcnt)
{
	sio_add_to_iov(iov, -offset);
	ssize_t nwr = iostream_writev(io, iov, iovcnt);
	sio_add_to_iov(iov, offset);
	return nwr;
}

ssize_t
coio_writev_timeout(struct iostream *io, struct iovec *iov, int iovcnt,
		    size_t size_hint, ev_tstamp timeout)
{
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	size_t total = 0;
	size_t iov_len = 0;
	struct iovec *end = iov + iovcnt;
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	/* Avoid a syscall in case of 0 iovcnt. */
	while (iov < end) {
		/* Write as much data as possible. */
		ssize_t nwr = coio_flush(io, iov, iov_len, end - iov);
		if (nwr >= 0) {
			total += nwr;
			/*
			 * If there was a hint for the total size
			 * of the vector, use it.
			 */
			if (size_hint > 0 && size_hint == total)
				break;

			iov += sio_move_iov(iov, nwr, &iov_len);
			if (iov == end) {
				assert(iov_len == 0);
				break;
			}
			continue;
		} else if (nwr == IOSTREAM_ERROR) {
			return -1;
		}
		if (delay <= 0) {
			diag_set(TimedOut);
			return -1;
		}
		/*
		 * Yield control to other fibers until the
		 * timeout is reached or the socket is
		 * ready.
		 */
		coio_wait(io->fd, iostream_status_to_events(nwr), delay);
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
		coio_timeout_update(&start, &delay);
	}
	return total;
}

void
coio_stat_init(ev_stat *stat, const char *path)
{
	ev_stat_init(stat, (ev_stat_cb) fiber_schedule_cb, path, 0.0);
}

int
coio_stat_stat_timeout(ev_stat *stat, ev_tstamp timeout)
{
	stat->data = fiber();
	ev_stat_start(loop(), stat);
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	fiber_yield_timeout(delay);
	ev_stat_stop(loop(), stat);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	return 0;
}

/**
 * The process status change callback.
 * Similar to fiber_schedule_cb, but also set watcher->data to NULL to indicate
 * that the fiber has been woken up on the child process termination.
 */
static void
coio_status_change_cb(ev_loop *loop, ev_child *watcher, int revents)
{
	(void)loop;
	(void)revents;
	struct fiber *fiber = watcher->data;
	assert(fiber() == &cord()->sched);
	watcher->data = NULL;
	fiber_wakeup(fiber);
}

int
coio_waitpid(pid_t pid, int *status)
{
	assert(cord_is_main());
	ev_child cw;
	ev_init(&cw, coio_status_change_cb);
	ev_child_set(&cw, pid, 0);
	cw.data = fiber();
	ev_child_start(loop(), &cw);

	do {
		fiber_yield();
	} while (cw.data != NULL);

	ev_child_stop(loop(), &cw);
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	*status = cw.rstatus;
	return 0;
}

/* Values of COIO_READ(WRITE) must equal to EV_READ(WRITE) */
static_assert(COIO_READ == (int) EV_READ, "TNT_IO_READ");
static_assert(COIO_WRITE == (int) EV_WRITE, "TNT_IO_WRITE");

struct coio_wdata {
	struct fiber *fiber;
	int revents;
};

static void
coio_wait_cb(struct ev_loop *loop, ev_io *watcher, int revents)
{
	(void) loop;
	struct coio_wdata *wdata = (struct coio_wdata *) watcher->data;
	wdata->revents = revents;
	fiber_wakeup(wdata->fiber);
}

API_EXPORT int
coio_wait(int fd, int events, double timeout)
{
	if (fiber_is_cancelled())
		return 0;
	struct ev_io io;
	ev_io_init(&io, coio_wait_cb, fd, events);
	struct coio_wdata wdata = {
		/* .fiber =   */ fiber(),
		/* .revents = */ 0
	};
	io.data = &wdata;

	/* A special hack to work with zero timeout */
	ev_set_priority(&io, EV_MAXPRI);
	ev_io_start(loop(), &io);

	fiber_yield_timeout(timeout);

	ev_io_stop(loop(), &io);
	return wdata.revents & (EV_READ | EV_WRITE);
}

API_EXPORT int
coio_close(int fd)
{
	ev_io_closing(loop(), fd);
	return close(fd);
}
