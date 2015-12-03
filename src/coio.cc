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
#include "coio.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "iobuf.h"
#include "sio.h"
#include "scoped_guard.h"
#include "coeio.h" /* coeio_resolve() */

struct CoioGuard {
	struct ev_io *ev_io;
	CoioGuard(struct ev_io *arg) :ev_io(arg) {}
	~CoioGuard() { ev_io_stop(loop(), ev_io); }
};

typedef void (*ev_stat_cb)(ev_loop *, ev_stat *, int);

/** Note: this function does not throw */
void
coio_init(struct ev_io *coio, int fd)
{
	/* Prepare for ev events. */
	coio->data = fiber();
	ev_init(coio, (ev_io_cb) fiber_schedule_cb);
	coio->fd = fd;
}

static inline bool
coio_fiber_yield_timeout(struct ev_io *coio, ev_tstamp delay)
{
	coio->data = fiber();
	bool is_timedout = fiber_yield_timeout(delay);
#ifdef DEBUG
	coio->data = NULL;
#endif
	return is_timedout;
}

/**
 * Connect to a host with a specified timeout.
 * @retval -1 timeout
 * @retval 0 connected
 */
static int
coio_connect_addr(struct ev_io *coio, struct sockaddr *addr,
		  socklen_t len, ev_tstamp timeout)
{
	ev_loop *loop = loop();
	evio_socket(coio, addr->sa_family, SOCK_STREAM, 0);
	auto coio_guard = make_scoped_guard([=]{ evio_close(loop, coio); });
	if (sio_connect(coio->fd, addr, len) == 0) {
		coio_guard.is_active = false;
		return 0;
	}
	assert(errno == EINPROGRESS);
	/*
	 * Wait until socket is ready for writing or
	 * timed out.
	 */
	ev_io_set(coio, coio->fd, EV_WRITE);
	ev_io_start(loop, coio);
	bool is_timedout = coio_fiber_yield_timeout(coio, timeout);
	ev_io_stop(loop, coio);
	fiber_testcancel();
	if (is_timedout)
		tnt_raise(TimedOut);
	int error = EINPROGRESS;
	socklen_t sz = sizeof(error);
	sio_getsockopt(coio->fd, SOL_SOCKET, SO_ERROR,
		       &error, &sz);
	if (error != 0) {
		errno = error;
		tnt_raise(SocketError, coio->fd, "connect");
	}
	coio_guard.is_active = false;
	return 0;
}

void
coio_fill_addrinfo(struct addrinfo *ai_local, const char *host,
		   const char *service, int host_hint)
{
	ai_local->ai_next = NULL;
	if (host_hint == 1) { // IPv4
		ai_local->ai_addrlen = sizeof(sockaddr_in);
		ai_local->ai_addr = (sockaddr*)malloc(ai_local->ai_addrlen);
		memset(ai_local->ai_addr, 0, ai_local->ai_addrlen);
		((sockaddr_in*)ai_local->ai_addr)->sin_family = AF_INET;
		((sockaddr_in*)ai_local->ai_addr)->sin_port =
			htons((uint16_t)atoi(service));
		inet_pton(AF_INET, host,
			&((sockaddr_in*)ai_local->ai_addr)->sin_addr);
	} else { // IPv6
		ai_local->ai_addrlen = sizeof(sockaddr_in6);
		ai_local->ai_addr = (sockaddr*)malloc(ai_local->ai_addrlen);
		memset(ai_local->ai_addr, 0, ai_local->ai_addrlen);
		((sockaddr_in6*)ai_local->ai_addr)->sin6_family = AF_INET6;
		((sockaddr_in6*)ai_local->ai_addr)->sin6_port =
			htons((uint16_t)atoi(service));
		inet_pton(AF_INET6, host,
			&((sockaddr_in6*)ai_local->ai_addr)->sin6_addr);
	}
}

/**
 * Resolve hostname:service from \a uri and connect to the first available
 * address with a specified timeout.
 *
 * If \a addr is not NULL the function provides resolved address on success.
 * In this case, \a addr_len is a value-result argument. It should be
 * initialized to the size of the buffer associated with \a addr. Upon return,
 * \a addr_len is updated to contain the actual size of the source address.
 * The returned address is truncated if the buffer provided is too small;
 * in this case, addrlen will return a value greater than was supplied to the
 * call.
 *
 * This function also supports UNIX domain sockets if uri->path is not NULL and
 * uri->service is NULL.
 *
 * @retval -1 timeout
 * @retval 0 connected
 */
int
coio_connect_timeout(struct ev_io *coio, struct uri *uri, struct sockaddr *addr,
		     socklen_t *addr_len, ev_tstamp timeout)
{
	char host[URI_MAXHOST] = { '\0' };
	if (uri->host) {
		snprintf(host, sizeof(host), "%.*s", (int) uri->host_len,
			 uri->host);
	}
	char service[URI_MAXSERVICE];
	snprintf(service, sizeof(service), "%.*s", (int) uri->service_len,
		 uri->service);
	/* try to resolve a hostname */
	struct ev_loop *loop = loop();
	ev_tstamp start, delay;
	evio_timeout_init(loop, &start, &delay, timeout);

	assert(service != NULL);
	if (strcmp(host, URI_HOST_UNIX) == 0) {
		/* UNIX socket */
		struct sockaddr_un un;
		snprintf(un.sun_path, sizeof(un.sun_path), "%s", service);
		un.sun_family = AF_UNIX;
		if (coio_connect_addr(coio, (struct sockaddr *) &un, sizeof(un),
				      delay) != 0)
			return -1;
		if (addr != NULL) {
			assert(addr_len != NULL);
			*addr_len = MIN(sizeof(un), *addr_len);
			memcpy(addr, &un, *addr_len);
		}
		return 0;
	}

	struct addrinfo *ai = NULL;
	struct addrinfo ai_local;
	if (uri->host_hint) {
		coio_fill_addrinfo(&ai_local, host, service, uri->host_hint);
		ai = &ai_local;
	} else {
	    struct addrinfo hints;
	    memset(&hints, 0, sizeof(struct addrinfo));
	    hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
	    hints.ai_socktype = SOCK_STREAM;
	    hints.ai_flags = AI_ADDRCONFIG|AI_NUMERICSERV|AI_PASSIVE;
	    hints.ai_protocol = 0;
	    int rc = coio_getaddrinfo(host, service, &hints, &ai, delay);
	    if (rc != 0) {
		    diag_raise();
		    tnt_raise(SocketError, -1, "getaddrinfo");
	    }
	}
	auto addrinfo_guard = make_scoped_guard([=] {
		if (!uri->host_hint) freeaddrinfo(ai);
		else free(ai_local.ai_addr);
	});
	evio_timeout_update(loop(), start, &delay);

	coio_timeout_init(&start, &delay, timeout);
	assert(! evio_has_fd(coio));
	while (ai) {
		try {
			if (coio_connect_addr(coio, ai->ai_addr,
					      ai->ai_addrlen, delay))
				return -1;
			if (addr != NULL) {
				assert(addr_len != NULL);
				*addr_len = MIN(ai->ai_addrlen, *addr_len);
				memcpy(addr, ai->ai_addr, *addr_len);
			}
			return 0; /* connected */
		} catch (SocketError *e) {
			if (ai->ai_next == NULL)
				throw;
			/* ignore exception and try the next address */
		}
		ai = ai->ai_next;
		ev_now_update(loop);
		coio_timeout_update(start, &delay);
	}

	tnt_raise(SocketError, coio->fd, "connection failed");
}

/**
 * Wait a client connection on a server socket until
 * timedout.
 */
int
coio_accept(struct ev_io *coio, struct sockaddr *addr,
	    socklen_t addrlen, ev_tstamp timeout)
{
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	CoioGuard coio_guard(coio);

	while (true) {
		/* Assume that there are waiting clients
		 * available */
		int fd = sio_accept(coio->fd, addr, &addrlen);
		if (fd >= 0) {
			evio_setsockopt_client(fd, addr->sa_family,
					       SOCK_STREAM);
			return fd;
		}
		/* The socket is not ready, yield */
		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_READ);
			ev_io_start(loop(), coio);
		}
		/*
		 * Yield control to other fibers until the
		 * timeout is reached.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio, delay);
		fiber_testcancel();
		if (is_timedout)
			tnt_raise(TimedOut);
		coio_timeout_update(start, &delay);
	}
}

/**
 * Read at least sz bytes from socket with readahead.
 *
 * In case of EOF returns the amount read until eof (possibly 0),
 * and sets errno to 0.
 * Can read up to bufsiz bytes.
 *
 * @retval the number of bytes read.
 */
ssize_t
coio_read_ahead_timeout(struct ev_io *coio, void *buf, size_t sz,
			size_t bufsiz, ev_tstamp timeout)
{
	assert(sz <= bufsiz);

	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	ssize_t to_read = (ssize_t) sz;

	CoioGuard coio_guard(coio);

	while (true) {
		/*
		 * Sic: assume the socket is ready: since
		 * the user called read(), some data must
		 * be expected.
		 */
		ssize_t nrd = sio_read(coio->fd, buf, bufsiz);
		if (nrd > 0) {
			to_read -= nrd;
			if (to_read <= 0)
				return sz - to_read;
			buf = (char *) buf + nrd;
			bufsiz -= nrd;
		} else if (nrd == 0) {
			errno = 0;
			return sz - to_read;
		}

		/* The socket is not ready, yield */
		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_READ);
			ev_io_start(loop(), coio);
		}
		/*
		 * Yield control to other fibers until the
		 * timeout is being reached.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio,
							    delay);
		fiber_testcancel();
		if (is_timedout)
			tnt_raise(TimedOut);
		coio_timeout_update(start, &delay);
	}
}

/**
 * Read at least sz bytes, with readahead.
 *
 * Treats EOF as an error, and throws an exception.
 *
 * @retval the number of bytes read, > 0.
 */
ssize_t
coio_readn_ahead(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz)
{
	ssize_t nrd = coio_read_ahead(coio, buf, sz, bufsiz);
	if (nrd < sz) {
		errno = EPIPE;
		tnt_raise(SocketError, coio->fd, "unexpected EOF when reading "
			  "from socket");
	}
	return nrd;
}

/**
 * Read at least sz bytes, with readahead and timeout.
 *
 * Treats EOF as an error, and throws an exception.
 *
 * @retval the number of bytes read, > 0.
 */
ssize_t
coio_readn_ahead_timeout(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz,
		         ev_tstamp timeout)
{
	ssize_t nrd = coio_read_ahead_timeout(coio, buf, sz, bufsiz, timeout);
	if (nrd < sz && errno == 0) { /* EOF. */
		errno = EPIPE;
		tnt_raise(SocketError, coio->fd, "unexpected EOF when reading "
			  "from socket");
	}
	return nrd;
}

/** Write sz bytes to socket.
 *
 * Throws SocketError in case of write error. If
 * the socket is not ready, yields the current
 * fiber until the socket becomes ready, until
 * all data is written.
 *
 * @retval the number of bytes written. Can be less than
 * requested only in case of timeout.
 */
ssize_t
coio_write_timeout(struct ev_io *coio, const void *buf, size_t sz,
	   ev_tstamp timeout)
{
	size_t towrite = sz;
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	CoioGuard coio_guard(coio);

	while (true) {
		/*
		 * Sic: write as much data as possible,
		 * assuming the socket is ready.
		 */
		ssize_t nwr = sio_write(coio->fd, buf, towrite);
		if (nwr > 0) {
			/* Go past the data just written. */
			if (nwr >= towrite)
				return sz;
			towrite -= nwr;
			buf = (char *) buf + nwr;
		}
		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_WRITE);
			ev_io_start(loop(), coio);
		}
		/* Yield control to other fibers. */
		fiber_testcancel();
		/*
		 * Yield control to other fibers until the
		 * timeout is reached or the socket is
		 * ready.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio,
							    delay);
		fiber_testcancel();

		if (is_timedout)
			tnt_raise(TimedOut);
		coio_timeout_update(start, &delay);
	}
}

/*
 * Write iov using sio API.
 * Put in an own function to workaround gcc bug with @finally
 */
static inline ssize_t
coio_flush(int fd, struct iovec *iov, ssize_t offset, int iovcnt)
{
	ssize_t nwr;
	try {
		sio_add_to_iov(iov, -offset);
		nwr = sio_writev(fd, iov, iovcnt);
		sio_add_to_iov(iov, offset);
	} catch (SocketError *e) {
		sio_add_to_iov(iov, offset);
		throw;
	}
	return nwr;
}

ssize_t
coio_writev_timeout(struct ev_io *coio, struct iovec *iov, int iovcnt,
		    size_t size_hint, ev_tstamp timeout)
{
	ssize_t total = 0;
	size_t iov_len = 0;
	struct iovec *end = iov + iovcnt;
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	CoioGuard coio_guard(coio);

	/* Avoid a syscall in case of 0 iovcnt. */
	while (iov < end) {
		/* Write as much data as possible. */
		ssize_t nwr = coio_flush(coio->fd, iov, iov_len,
					 end - iov);
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
		}
		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_WRITE);
			ev_io_start(loop(), coio);
		}
		/* Yield control to other fibers. */
		fiber_testcancel();
		/*
		 * Yield control to other fibers until the
		 * timeout is reached or the socket is
		 * ready.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio, delay);
		fiber_testcancel();

		if (is_timedout)
			tnt_raise(TimedOut);
		coio_timeout_update(start, &delay);
	}
	return total;
}

/**
 * Send up to sz bytes to a UDP socket.
 * Return the number of bytes sent.
 *
 * @retval  n  the number of bytes written
 */
ssize_t
coio_sendto_timeout(struct ev_io *coio, const void *buf, size_t sz, int flags,
		    const struct sockaddr *dest_addr, socklen_t addrlen,
		    ev_tstamp timeout)
{
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	CoioGuard coio_guard(coio);

	while (true) {
		/*
		 * Sic: write as much data as possible,
		 * assuming the socket is ready.
		 */
		ssize_t nwr = sio_sendto(coio->fd, buf, sz,
					 flags, dest_addr, addrlen);
		if (nwr > 0)
			return nwr;
		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_WRITE);
			ev_io_start(loop(), coio);
		}
		/*
		 * Yield control to other fibers until
		 * timeout is reached or the socket is
		 * ready.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio,
							    delay);
		fiber_testcancel();
		if (is_timedout)
			tnt_raise(TimedOut);
		coio_timeout_update(start, &delay);
	}
}

/**
 * Read a datagram up to sz bytes from a socket, with a timeout.
 *
 * @retval   0, errno = 0   eof
 * @retvl    n              number of bytes read
 */
ssize_t
coio_recvfrom_timeout(struct ev_io *coio, void *buf, size_t sz, int flags,
		      struct sockaddr *src_addr, socklen_t addrlen,
		      ev_tstamp timeout)
{
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	CoioGuard coio_guard(coio);

	while (true) {
		/*
		 * Read as much data as possible,
		 * assuming the socket is ready.
		 */
		ssize_t nrd = sio_recvfrom(coio->fd, buf, sz, flags,
					   src_addr, &addrlen);
		if (nrd >= 0)
			return nrd;

		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_READ);
			ev_io_start(loop(), coio);
		}
		/*
		 * Yield control to other fibers until
		 * timeout is reached or the socket is
		 * ready.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio,
							    delay);
		fiber_testcancel();
		if (is_timedout)
			tnt_raise(TimedOut);
		coio_timeout_update(start, &delay);
	}
}

void
coio_service_on_accept(struct evio_service *evio_service,
		       int fd, struct sockaddr *addr, socklen_t addrlen)
{
	struct coio_service *service = (struct coio_service *)
			evio_service->on_accept_param;
	struct ev_io coio;

	coio_init(&coio, fd);

	/* Set connection name. */
	char fiber_name[SERVICE_NAME_MAXLEN];
	char iobuf_name[SERVICE_NAME_MAXLEN];
	snprintf(fiber_name, sizeof(fiber_name),
		 "%s/%s", evio_service->name, sio_strfaddr(addr, addrlen));
	snprintf(iobuf_name, sizeof(iobuf_name), "%s/%s", "iobuf",
		sio_strfaddr(addr, addrlen));

	/* Create the worker fiber. */
	struct iobuf *iobuf = NULL;
	struct fiber *f;

	try {
		iobuf = iobuf_new();
		f = fiber_new_xc(fiber_name, service->handler);
	} catch (struct error *e) {
		error_log(e);
		say_error("can't create a handler fiber, dropping client connection");
		evio_close(loop(), &coio);
		if (iobuf)
			iobuf_delete(iobuf);
		throw;
	}
	/*
	 * The coio is passed into the created fiber, reset the
	 * libev callback param to point at it.
	 */
	coio.data = f;
	/*
	 * Start the created fiber. It becomes the coio object owner
	 * and will have to close it and free before termination.
	 */
	fiber_start(f, coio, addr, addrlen, iobuf, service->handler_param);
}

void
coio_service_init(struct coio_service *service, const char *name,
		  fiber_func handler, void *handler_param)
{
	evio_service_init(loop(), &service->evio_service, name,
			  coio_service_on_accept, service);
	service->handler = handler;
	service->handler_param = handler_param;
}

static void
on_bind(void *arg)
{
	fiber_wakeup((struct fiber *) arg);
}

void
coio_service_start(struct evio_service *service, const char *uri)
{
	assert(service->on_bind == NULL);
	assert(service->on_bind_param == NULL);
	service->on_bind = on_bind;
	service->on_bind_param = fiber();
	evio_service_start(service, uri);
	fiber_yield();
	service->on_bind_param = NULL;
	service->on_bind = NULL;
}

void
coio_stat_init(ev_stat *stat, const char *path)
{
	ev_stat_init(stat, (ev_stat_cb) fiber_schedule_cb, path, 0.0);
}

void
coio_stat_stat_timeout(ev_stat *stat, ev_tstamp timeout)
{
	stat->data = fiber();
	ev_stat_start(loop(), stat);
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	fiber_yield_timeout(delay);
	ev_stat_stop(loop(), stat);
	fiber_testcancel();
}

typedef void (*ev_child_cb)(ev_loop *, ev_child *, int);

/**
 * Wait for a forked child to complete.
 * @return process return status
 */
int
coio_waitpid(pid_t pid)
{
	assert(cord_is_main());
	ev_child cw;
	ev_init(&cw, (ev_child_cb) fiber_schedule_cb);
	ev_child_set(&cw, pid, 0);
	cw.data = fiber();
	ev_child_start(loop(), &cw);
	/*
	 * It's not safe to spuriously wakeup this fiber since
	 * in this case the server will leave a zombie process
	 * behind.
	 */
	bool allow_cancel = fiber_set_cancellable(false);
	fiber_yield();
	fiber_set_cancellable(allow_cancel);
	ev_child_stop(loop(), &cw);
	int status = cw.rstatus;
	fiber_testcancel();
	return status;
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
	fiber_call(wdata->fiber);
}

int
coio_wait(int fd, int events, double timeout)
{
	if (fiber_is_cancelled())
		return 0;
	struct ev_io io;
	coio_init(&io, fd);
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
	return wdata.revents;
}
