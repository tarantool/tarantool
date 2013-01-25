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

#include <netinet/tcp.h>
#include <stdio.h>

#include "fiber.h"
#include "iobuf.h"
#include "sio.h"


/** Note: this function does not throw */
void
coio_init(struct ev_io *coio, int fd)
{
	assert(fd >= 0);
	/* Prepare for ev events. */
	coio->data = fiber;
	ev_init(coio, (void *) fiber_schedule);
	coio->fd = fd;
}

/**
 * Create an endpoint for communication.
 * Set socket as non-block and apply protocol specific options.
 */
void
coio_socket(struct ev_io *coio, int domain, int type, int protocol)
{
	assert(coio->fd == -1);
	int fd = sio_socket(domain, type, protocol);
	coio_init(coio, fd);
	sio_setfl(fd, O_NONBLOCK, 1);
}

/**
 * Connect to a host.
 */
void
coio_connect(struct ev_io *coio, struct sockaddr_in *addr)
{
	coio_connect_timeout(coio, addr, sizeof(*addr),
			     TIMEOUT_INFINITY);
}

/**
 * Connect to a host with a specified timeout.
 */
void
coio_connect_timeout(struct ev_io *coio, struct sockaddr_in *addr,
		     socklen_t len, ev_tstamp timeout)
{
	if (sio_connect(coio->fd, addr, len) == 0)
		return;
	assert(errno == EINPROGRESS);
	/* Wait until socket is ready for writing or
	 * timed out. */
	ev_io_set(coio, coio->fd, EV_WRITE);
	ev_io_start(coio);
	bool is_timedout = fiber_yield_timeout(timeout);
	ev_io_stop(coio);
	fiber_testcancel();
	if (is_timedout) {
		errno = ETIMEDOUT;
		tnt_raise(SocketError, :coio->fd in:"connect");
	}
	int error = EINPROGRESS;
	socklen_t sz = sizeof(error);
	sio_getsockopt(coio->fd, SOL_SOCKET, SO_ERROR,
		       &error, &sz);
	if (error == 0)
		return;
	errno = error;
	tnt_raise(SocketError, :coio->fd in:"connect");
}

/**
 * Connect to a first address in addrinfo list and initialize coio
 * with connected socket.
 *
 * Coio should not be initialized.
 */
void
coio_connect_addrinfo(struct ev_io *coio, struct addrinfo *ai,
		      ev_tstamp timeout)
{
	struct addrinfo *a;
	int error = 0;

	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	for (a = ai; a; a = a->ai_next) {
		@try {
			coio_socket(coio, a->ai_family, a->ai_socktype, a->ai_protocol);

			evio_setsockopt_tcp(coio->fd);

			coio_connect_timeout(coio, (struct sockaddr_in*)a->ai_addr,
					     a->ai_addrlen, delay);
			return;
		} @catch (FiberCancelException *e) {
			evio_close(coio);
			@throw;
		} @catch (tnt_Exception *e) {
			error = errno;
			ev_now_update();
			evio_timeout_update(start, &delay);
			evio_close(coio);
			continue;
		}
		return;
	}
	errno = error;
	tnt_raise(SocketError, :coio->fd in:"connect_addrinfo");
}

/**
 * Bind to a first address in addrinfo list and initialize coio
 * with binded socket.
 */
void
coio_bind_addrinfo(struct ev_io *coio, struct addrinfo *ai) {
	struct addrinfo *a;
	int error = 0;
	for (a = ai; a; a = a->ai_next) {
		@try {
			coio_socket(coio, a->ai_family, a->ai_socktype, a->ai_protocol);

			evio_setsockopt_tcpserver(coio->fd);

			if (sio_bind(coio->fd, (struct sockaddr_in*)a->ai_addr,
				     a->ai_addrlen) == 0)
				return;
			socklen_t sz = sizeof(error);
			sio_getsockopt(coio->fd, SOL_SOCKET, SO_ERROR, &error, &sz);
			if (error) {
				evio_close(coio);
				continue;
			}
			return;
		} @catch (FiberCancelException *e) {
			evio_close(coio);
			@throw;
		} @catch (tnt_Exception *e) {
			evio_close(coio);
			continue;
		}
		return;
	}
	errno = error;
	tnt_raise(SocketError, :coio->fd in:"bind_addrinfo");
}

/**
 * Wait a client connection on a server socket until
 * timedout.
 */
int
coio_accept(struct ev_io *coio, struct sockaddr_in *addr,
	    socklen_t addrlen, ev_tstamp timeout)
{
	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	while (true) {
		/* Assume that there are waiting clients
		 * available */
		int fd = sio_accept(coio->fd, addr, &addrlen);
		if (fd >= 0) {
			evio_setsockopt_tcp(fd);
			return fd;
		}
		/* The socket is not ready, yield */
		if (! ev_is_active(coio)) {
			ev_io_set(coio, coio->fd, EV_READ);
			ev_io_start(coio);
		}
		/* Yield control to other fibers until the timeout
		 * is being reached. */
		bool is_timedout = fiber_yield_timeout(delay);
		fiber_testcancel();
		if (is_timedout) {
			errno = ETIMEDOUT;
			tnt_raise(SocketError, :coio->fd in:"accept");
		}
		evio_timeout_update(start, &delay);
	}
}

/**
 * Read at least sz bytes from socket with readahead.
 *
 * In case of EOF returns 0.
 * Can read up to bufsiz bytes.
 *
 * @retval the number of bytes read, > 0.
 */
ssize_t
coio_read_ahead(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz)
{
	assert(sz <= bufsiz);

	ssize_t to_read = (ssize_t) sz;
	@try {
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
				buf += nrd;
				bufsiz -= nrd;
			} else if (nrd == 0) {
				return 0;
			}
			/* The socket is not ready, yield */
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_READ);
				ev_io_start(coio);
			}
			fiber_yield();
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(coio);
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
		tnt_raise(SocketError, :coio->fd in:"unexpected EOF when reading "
			  "from socket");
	}
	return nrd;
}

/**
 * Read at least sz bytes from socket with readahead and
 * timeout.
 *
 * In case of EOF returns 0.
 * Can read up to bufsiz bytes.
 *
 * @retval the number of bytes read, > 0.
 */
ssize_t
coio_read_ahead_timeout(struct ev_io *coio, void *buf, size_t sz, size_t bufsiz,
		        ev_tstamp timeout)
{
	assert(sz <= bufsiz);

	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	ssize_t to_read = (ssize_t) sz;
	@try {
		while (true) {
			/*
			 * Sic: assume the socket is ready: since
			 * the user called read(), some data must
			 * be expected.
		         */
			ssize_t nrd = sio_read_total(coio->fd, buf, bufsiz, sz - to_read);
			if (nrd > 0) {
				to_read -= nrd;
				if (to_read <= 0)
					return sz - to_read;
				buf += nrd;
				bufsiz -= nrd;
			} else if (nrd == 0) {
				return 0;
			}
			/* The socket is not ready, yield */
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_READ);
				ev_io_start(coio);
			}
			/* Yield control to other fibers until the timeout
			 * is being reached. */
			bool is_timedout = fiber_yield_timeout(delay);
			fiber_testcancel();
			if (is_timedout) {
				errno = ETIMEDOUT;
				tnt_raise(SocketRWError, :coio->fd in: sz - to_read
					  :"read(timeout)");
			}
			evio_timeout_update(start, &delay);
		}
	} @finally {
		ev_io_stop(coio);
	}
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
	if (nrd < sz) {
		errno = EPIPE;
		tnt_raise(SocketError, :coio->fd in:"unexpected EOF when reading "
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
 */
void
coio_write(struct ev_io *coio, const void *buf, size_t sz)
{
	@try {
		while (true) {
			/*
			 * Sic: write as much data as possible,
			 * assuming the socket is ready.
		         */
			ssize_t nwr = sio_write(coio->fd, buf, sz);
			if (nwr > 0) {
				/* Go past the data just written. */
				if (nwr >= sz)
					return;
				sz -= nwr;
				buf += nwr;
			}
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_WRITE);
				ev_io_start(coio);
			}
			/* Yield control to other fibers. */
			fiber_yield();
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(coio);
	}
}

/** Write sz bytes to socket.
 *
 * Throws SocketWriteError in case of write error.
 * If the socket is not ready, yields the current
 * fiber until the socket becomes ready, until
 * all data is written or the timeout is
 * being reached.
 */
void
coio_write_timeout(struct ev_io *coio, const void *buf, size_t sz,
		   ev_tstamp timeout)
{
	size_t to_write = sz;

	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	@try {
		while (true) {
			/*
			 * Sic: write as much data as possible,
			 * assuming the socket is ready.
		         */
			ssize_t nwr = sio_write_total(coio->fd, buf, sz, to_write - sz);
			if (nwr > 0) {
				/* Go past the data just written. */
				if (nwr >= sz)
					return;
				sz -= nwr;
				buf += nwr;
			}
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_WRITE);
				ev_io_start(coio);
			}
			/* Yield control to other fibers until the timeout
			 * is being reached. */
			bool is_timedout = fiber_yield_timeout(delay);
			fiber_testcancel();

			if (is_timedout) {
				errno = ETIMEDOUT;
				tnt_raise(SocketRWError, :coio->fd in: to_write - sz
					  :"write(timeout)");
			}
			evio_timeout_update(start, &delay);
		}
	} @finally {
		ev_io_stop(coio);
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
	@try {
		sio_add_to_iov(iov, -offset);
		nwr = sio_writev(fd, iov, iovcnt);
	} @finally {
		sio_add_to_iov(iov, offset);
	}
	return nwr;
}

ssize_t
coio_writev(struct ev_io *coio, struct iovec *iov, int iovcnt, size_t size_hint)
{
	ssize_t total = 0;
	size_t iov_len = 0;
	struct iovec *end = iov + iovcnt;
	@try {
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
				ev_io_start(coio);
			}
			/* Yield control to other fibers. */
			fiber_yield();
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(coio);
	}
	return total;
}

void
coio_sendto_timeout(struct ev_io *coio, const void *buf, size_t sz, int flags,
		    const struct sockaddr_in *dest_addr, socklen_t addrlen,
		    ev_tstamp timeout)
{
	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	@try {
		while (true) {
			/*
			 * Sic: write as much data as possible,
			 * assuming the socket is ready.
		         */
			ssize_t nwr = sio_sendto(coio->fd, buf, sz, flags,
						 dest_addr, addrlen);
			if (nwr > 0) {
				/* Go past the data just written. */
				if (nwr >= sz)
					return;
				sz -= nwr;
				buf += nwr;
			}
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_WRITE);
				ev_io_start(coio);
			}
			/*
			 * Yield control to other fibers until
			 * timeout is reached or the socket is
			 * ready.
			 */
			bool is_timedout = fiber_yield_timeout(delay);
			fiber_testcancel();
			if (is_timedout) {
				errno = ETIMEDOUT;
				tnt_raise(SocketError, :coio->fd in:"sendto");
			}
			evio_timeout_update(start, &delay);
		}
	} @finally {
		ev_io_stop(coio);
	}
}

size_t
coio_recvfrom_timeout(struct ev_io *coio, void *buf, size_t sz, int flags,
		      struct sockaddr_in *src_addr, socklen_t addrlen,
		      ev_tstamp timeout)
{
	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	@try {
		while (true) {
			/*
			 * Read as much data as possible,
			 * assuming the socket is ready.
		         */
			ssize_t nwr = sio_recvfrom(coio->fd, buf, sz, flags,
						   src_addr, &addrlen);
			if (nwr > 0)
				return nwr;

			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_WRITE);
				ev_io_start(coio);
			}
			/*
			 * Yield control to other fibers until
			 * timeout is reached or the socket is
			 * ready.
			 */
			bool is_timedout = fiber_yield_timeout(delay);
			fiber_testcancel();
			if (is_timedout) {
				errno = ETIMEDOUT;
				tnt_raise(SocketError, :coio->fd in:"recvfrom");
			}
			evio_timeout_update(start, &delay);
		}
	} @finally {
		ev_io_stop(coio);
	}
}

void
coio_service_on_accept(struct evio_service *evio_service,
		       int fd, struct sockaddr_in *addr)
{
	struct coio_service *service = evio_service->on_accept_param;
	struct ev_io coio;

	coio_init(&coio, fd);

	/* Set connection name. */
	char fiber_name[SERVICE_NAME_MAXLEN];
	char iobuf_name[SERVICE_NAME_MAXLEN];
	snprintf(fiber_name, sizeof(fiber_name),
		 "%s/%s", evio_service->name, sio_strfaddr(addr));
	snprintf(iobuf_name, sizeof(iobuf_name), "%s/%s", "iobuf", sio_strfaddr(addr));

	/* Create the worker fiber. */
	struct iobuf *iobuf = NULL;
	struct fiber *f;

	@try {
		iobuf = iobuf_create(iobuf_name);
		f = fiber_create(fiber_name, service->handler);
	} @catch (tnt_Exception *e) {
		say_error("can't create a handler fiber, dropping client connection");
		evio_close(&coio);
		if (iobuf)
			iobuf_destroy(iobuf);
		@throw;
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
	fiber_call(f, coio, iobuf, service->handler_param);
}

void
coio_service_init(struct coio_service *service, const char *name,
		  const char *host, int port,
		  void (*handler)(va_list ap), void *handler_param)
{
	evio_service_init(&service->evio_service, name, host, port,
			  coio_service_on_accept, service);
	service->handler = handler;
	service->handler_param = handler_param;
}
