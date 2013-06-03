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
coio_init(struct ev_io *coio)
{
	/* Prepare for ev events. */
	coio->data = fiber;
	ev_init(coio, (void *) fiber_schedule);
	coio->fd = -1;
}

static inline void
coio_fiber_yield(struct ev_io *coio)
{
	coio->data = fiber;
	fiber_yield();
#ifdef DEBUG
	coio->data = NULL;
#endif
}

static inline bool
coio_fiber_yield_timeout(struct ev_io *coio, ev_tstamp delay)
{
	coio->data = fiber;
	bool is_timedout = fiber_yield_timeout(delay);
#ifdef DEBUG
	coio->data = NULL;
#endif
	return is_timedout;
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
 * @retval true timeout
 * @retval false connected
 */
bool
coio_connect_timeout(struct ev_io *coio, struct sockaddr_in *addr,
		     socklen_t len, ev_tstamp timeout)
{
	if (sio_connect(coio->fd, addr, len) == 0)
		return false;
	assert(errno == EINPROGRESS);
	/*
	 * Wait until socket is ready for writing or
	 * timed out.
	 */
	ev_io_set(coio, coio->fd, EV_WRITE);
	ev_io_start(coio);
	bool is_timedout = coio_fiber_yield_timeout(coio, timeout);
	ev_io_stop(coio);
	fiber_testcancel();
	if (is_timedout) {
		errno = ETIMEDOUT;
		return true;
	}
	int error = EINPROGRESS;
	socklen_t sz = sizeof(error);
	sio_getsockopt(coio->fd, SOL_SOCKET, SO_ERROR,
		       &error, &sz);
	if (error != 0) {
		errno = error;
		tnt_raise(SocketError, :coio->fd in:"connect");
	}
	return false;
}

/**
 * Connect to a first address in addrinfo list and initialize coio
 * with connected socket.
 *
 * If coio is already initialized, socket family,
 * type and protocol must match the remote address.
 *
 * @retval true  timeout
 * @retval false sucess
 */
bool
coio_connect_addrinfo(struct ev_io *coio, struct addrinfo *ai,
		      ev_tstamp timeout)
{
	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);
	assert(! evio_is_active(coio));
	bool res = true;
	while (ai) {
		struct sockaddr_in *addr = (struct sockaddr_in *)ai->ai_addr;
		@try {
			evio_socket(coio, ai->ai_family,
				    ai->ai_socktype,
				    ai->ai_protocol);
			res = coio_connect_timeout(coio, addr, ai->ai_addrlen,
						   delay);
			return res;
		} @catch (SocketError *e) {
			if (ai->ai_next == NULL)
				@throw;
			ev_now_update();
			evio_timeout_update(start, &delay);
		} @finally {
			if (res)
				evio_close(coio);
		}
		ai = ai->ai_next;
	}
	/* unreachable. */
	tnt_raise(SocketError, :coio->fd in: "connect_addrinfo()");
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
		/*
		 * Yield control to other fibers until the
		 * timeout is reached.
		 */
		bool is_timedout = coio_fiber_yield_timeout(coio, delay);
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
	evio_timeout_init(&start, &delay, timeout);

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
				errno = 0;
				return sz - to_read;
			}

			/* The socket is not ready, yield */
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_READ);
				ev_io_start(coio);
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
			evio_timeout_update(start, &delay);
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
	evio_timeout_init(&start, &delay, timeout);
	@try {
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
				buf += nwr;
			}
			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_WRITE);
				ev_io_start(coio);
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
coio_writev(struct ev_io *coio, struct iovec *iov, int iovcnt,
	    size_t size_hint)
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
			coio_fiber_yield(coio);
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(coio);
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
			ssize_t nwr = sio_sendto(coio->fd, buf, sz,
						 flags, dest_addr, addrlen);
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
			bool is_timedout = coio_fiber_yield_timeout(coio,
								    delay);
			fiber_testcancel();
			if (is_timedout) {
				errno = ETIMEDOUT;
				return 0;
			}
			evio_timeout_update(start, &delay);
		}
	} @finally {
		ev_io_stop(coio);
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
			ssize_t nrd = sio_recvfrom(coio->fd, buf, sz, flags,
						   src_addr, &addrlen);
			if (nrd >= 0)
				return nrd;

			if (! ev_is_active(coio)) {
				ev_io_set(coio, coio->fd, EV_WRITE);
				ev_io_start(coio);
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

	coio_init(&coio);
	coio.fd = fd;

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
		iobuf = iobuf_new(iobuf_name);
		f = fiber_new(fiber_name, service->handler);
	} @catch (tnt_Exception *e) {
		say_error("can't create a handler fiber, dropping client connection");
		evio_close(&coio);
		if (iobuf)
			iobuf_delete(iobuf);
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
