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
#include "coio.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <stdio.h>

#include "iobuf.h"
#include "sio.h"
#include "scoped_guard.h"
#include "coeio.h" /* coeio_resolve() */

struct CoioGuard {
	struct ev_io *ev_io;
	CoioGuard(struct ev_io *arg) :ev_io(arg) {}
	~CoioGuard() { ev_io_stop(loop(), ev_io); }
};

static inline void
fiber_schedule_coio(ev_loop * /* loop */, ev_io *watcher, int event)
{
	return fiber_schedule((ev_watcher *) watcher, event);
}

/** Note: this function does not throw */
void
coio_init(struct ev_io *coio)
{
	/* Prepare for ev events. */
	coio->data = fiber();
	ev_init(coio, fiber_schedule_coio);
	coio->fd = -1;
}

static inline void
coio_fiber_yield(struct ev_io *coio)
{
	coio->data = fiber();
	fiber_yield();
#ifdef DEBUG
	coio->data = NULL;
#endif
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
	if (is_timedout) {
		errno = ETIMEDOUT;
		return -1;
	}
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
coio_connect_timeout(struct ev_io *coio, const char *host, const char *service,
		     struct sockaddr *addr, socklen_t *addr_len,
		     ev_tstamp timeout)
{
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

	struct addrinfo *ai = coeio_resolve(SOCK_STREAM, host, service, delay);
	if (ai == NULL)
		return -1; /* timeout */

	auto addrinfo_guard = make_scoped_guard([=]{ freeaddrinfo(ai); });
	evio_timeout_update(loop(), start, &delay);

	coio_timeout_init(&start, &delay, timeout);
	assert(! evio_is_active(coio));
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
			/* ignore */
			say_error("failed to connect to %s: %s",
				  sio_strfaddr(ai->ai_addr, ai->ai_addrlen),
				  e->errmsg());
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
			evio_setsockopt_tcp(fd, addr->sa_family);
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
		if (is_timedout) {
			errno = ETIMEDOUT;
			tnt_raise(SocketError, coio->fd, "accept");
		}
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
 * @retval the number of bytes read, sets the errno to ETIMEDOUT or 0.
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
		if (is_timedout) {
			errno = ETIMEDOUT;
			return sz - to_read;
		}
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

		if (is_timedout) {
			errno = ETIMEDOUT;
			return sz - towrite;
		}
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
	} catch (Exception *e) {
		sio_add_to_iov(iov, offset);
		throw;
	}
	return nwr;
}

ssize_t
coio_writev(struct ev_io *coio, struct iovec *iov, int iovcnt,
	    size_t size_hint)
{
	ssize_t total = 0;
	size_t iov_len = 0;
	struct iovec *end = iov + iovcnt;

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
		coio_fiber_yield(coio);
		fiber_testcancel();
	}
	return total;
}

/**
 * Send up to sz bytes to a UDP socket.
 * Return the number of bytes sent.
 *
 * @retval  0, errno = ETIMEDOUT timeout
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
		if (is_timedout) {
			errno = ETIMEDOUT;
			return 0;
		}
		coio_timeout_update(start, &delay);
	}
}

/**
 * Read a datagram up to sz bytes from a socket, with a timeout.
 *
 * @retval   0, errno = 0   eof
 * @retval   0, errno = ETIMEDOUT timeout
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
		if (is_timedout) {
			errno = ETIMEDOUT;
			return 0;
		}
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

	coio_init(&coio);
	coio.fd = fd;

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
		iobuf = iobuf_new(iobuf_name);
		f = fiber_new(fiber_name, service->handler);
	} catch (Exception *e) {
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
	fiber_call(f, coio, addr, iobuf, service->handler_param);
}

void
coio_service_init(struct coio_service *service, const char *name,
		  const char *uri,
		  void (*handler)(va_list ap), void *handler_param)
{
	evio_service_init(loop(), &service->evio_service, name, uri,
			  coio_service_on_accept, service);
	service->handler = handler;
	service->handler_param = handler_param;
}
