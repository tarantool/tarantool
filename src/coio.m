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

#include <stdio.h>

#include "fiber.h"
#include "sio.h"

void
coio_clear(struct coio *coio)
{
	ev_init(&coio->ev, (void *) fiber_schedule);
	coio->ev.data = fiber;
	coio->ev.fd = -1;
}

/** Note: this function does not throw */
void
coio_init(struct coio *coio, int fd)
{
	assert(fd >= 0);

	/* Prepare for ev events. */
	coio->ev.data = fiber;
	ev_init(&coio->ev, (void *) fiber_schedule);
	coio->ev.fd = fd;
}

/** Note: this function does not throw. */
void
coio_close(struct coio *coio)
{
	/* Stop I/O events. Safe to do even if not started. */
	ev_io_stop(&coio->ev);

	/* Close the socket. */
	close(coio->ev.fd);
	/* Make sure coio_is_connected() returns a proper value. */
	coio->ev.fd = -1;
}

/**
 * Connect to a host and initialize coio with connected
 * socket.
 */
void
coio_connect(struct coio *coio, struct sockaddr_in *addr)
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
			ev_io_set(&coio->ev, fd, EV_WRITE);
			ev_io_start(&coio->ev);
			fiber_yield();
			ev_io_stop(&coio->ev);
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
		coio_close(coio);
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
coio_read_ahead(struct coio *coio, void *buf, size_t sz, size_t bufsiz)
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
			ssize_t nrd = sio_read(coio->ev.fd, buf, bufsiz);
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
			if (! ev_is_active(&coio->ev)) {
				ev_io_set(&coio->ev, coio->ev.fd, EV_READ);
				ev_io_start(&coio->ev);
			}
			fiber_yield();
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(&coio->ev);
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
coio_readn_ahead(struct coio *coio, void *buf, size_t sz, size_t bufsiz)
{
	ssize_t nrd = coio_read_ahead(coio, buf, sz, bufsiz);
	if (nrd < sz) {
		errno = EPIPE;
		tnt_raise(SocketError, :coio->ev.fd in:"unexpected EOF when reading "
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
coio_write(struct coio *coio, const void *buf, size_t sz)
{
	@try {
		while (true) {
			/*
			 * Sic: write as much data as possible,
			 * assuming the socket is ready.
		         */
			ssize_t nwr = sio_write(coio->ev.fd, buf, sz);
			if (nwr > 0) {
				/* Go past the data just written. */
				if (nwr >= sz)
					return;
				sz -= nwr;
				buf += nwr;
			}
			if (! ev_is_active(&coio->ev)) {
				ev_io_set(&coio->ev, coio->ev.fd, EV_WRITE);
				ev_io_start(&coio->ev);
			}
			/* Yield control to other fibers. */
			fiber_yield();
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(&coio->ev);
	}
}

ssize_t
coio_writev(struct coio *coio, struct iovec *iov, int iovcnt)
{
	ssize_t total = 0;
	@try {
		/* Avoid a syscall in case of 0 iovcnt. */
		while (iovcnt) {
			/* Write as much data as possible. */
			ssize_t nwr = sio_writev(coio->ev.fd, iov, iovcnt);
			if (nwr >= 0) {
				total += nwr;
				iov = sio_advance_iov(iov, &iovcnt, nwr);
				if (iovcnt == 0)
					break;
			}
			if (! ev_is_active(&coio->ev)) {
				ev_io_set(&coio->ev, coio->ev.fd, EV_WRITE);
				ev_io_start(&coio->ev);
			}
			/* Yield control to other fibers. */
			fiber_yield();
			fiber_testcancel();
		}
	} @finally {
		ev_io_stop(&coio->ev);
	}
	return total;
}

void
coio_service_on_accept(struct evio_service *evio_service,
		       int fd, struct sockaddr_in *addr)
{
	struct coio_service *service = evio_service->on_accept_param;
	struct coio coio;

	coio_init(&coio, fd);

	/* Set connection name. */
	char name[SERVICE_NAME_MAXLEN];
	snprintf(name, sizeof(name),
		 "%s/%s", evio_service->name, sio_strfaddr(addr));

	/* Create the worker fiber. */
	struct fiber *f = fiber_create(name, service->handler);
	if (f == NULL)
		goto error;
	/*
	 * The coio is passed on to the created fiber, reset the
	 * libev callback param to point at it.
	 */
	coio.ev.data = f;
	/*
	 * Start the created fiber. It becomes the coio object owner
	 * and will have to close it and free before termination.
	 */
	fiber_call(f, coio, service->handler_param);
	return;

error:
	say_error("can't create a handler fiber, dropping client connection");
	close(fd);
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

