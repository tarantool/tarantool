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
 * Connect to a host and initialize coio with connected
 * socket.
 */
void
coio_connect(struct ev_io *coio, struct sockaddr_in *addr)
{
	int fd = sio_socket();
	@try {
		coio_init(coio, fd);

                int on = 1;
                /* libev is non-blocking */
                sio_setfl(fd, O_NONBLOCK, on);

                /*
		 * SO_KEEPALIVE to ensure connections don't hang
                 * around for too long when a link goes away
                 */
                sio_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                               &on, sizeof(on));
                /*
                 * Lower latency is more important than higher
                 * bandwidth, and we usually write entire
                 * request/response in a single syscall.
                 */
                sio_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                               &on, sizeof(on));

		if (sio_connect(fd, addr, sizeof(*addr)) < 0) {
			assert(errno == EINPROGRESS);
			/* Wait until socket is ready for writing. */
			ev_io_set(coio, fd, EV_WRITE);
			ev_io_start(coio);
			fiber_yield();
			ev_io_stop(coio);
			fiber_testcancel();

			int error = EINPROGRESS;
			socklen_t sz = sizeof(error);
			sio_getsockopt(fd, SOL_SOCKET, SO_ERROR,
				       &error, &sz);
			if (error != 0) {
				errno = error;
				tnt_raise(SocketError, :fd in:"connect");
			}
		}
	} @catch (tnt_Exception *e) {
		evio_close(coio);
		@throw;
	}
}

/**
 * Read at least sz bytes from socket with readahead.
 *
 * In case of EOF returns 0.
 * Can read up to bufsiz bytes.
 *
 * Returns the number of bytes read.
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

ssize_t
coio_writev(struct ev_io *coio, struct iovec *iov, int iovcnt, size_t size_hint)
{
	ssize_t total = 0, iov_len = 0;
	struct iovec *end = iov + iovcnt;
	@try {
		/* Avoid a syscall in case of 0 iovcnt. */
		while (iov < end) {
			/* Write as much data as possible. */
			ssize_t nwr;
			@try {
				sio_add_to_iov(iov, -iov_len);
				nwr = sio_writev(coio->fd, iov, end - iov);
			} @finally {
				sio_add_to_iov(iov, iov_len);
			}
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

