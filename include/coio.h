#ifndef TARANTOOL_COIO_H_INCLUDED
#define TARANTOOL_COIO_H_INCLUDED
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
#include "evio.h"
/*
 * Co-operative I/O
 * Yield the current fiber until IO is ready.
 */
struct coio
{
	struct ev_io ev;
};

struct coio_service
{
	struct evio_service evio_service;
	/* Fiber function. */
	void (*handler)(va_list ap);
	/** Passed to the created fiber. */
	void *handler_param;
};

void
coio_clear(struct coio *coio);

static inline bool
coio_is_connected(struct coio *coio)
{
	return coio->ev.fd >= 0;
}

void
coio_connect(struct coio *coio, struct sockaddr_in *addr);

void
coio_init(struct coio *coio, int fd);

void
coio_close(struct coio *coio);

ssize_t
coio_read_ahead(struct coio *coio, void *buf, size_t sz, size_t bufsiz);

ssize_t
coio_readn_ahead(struct coio *coio, void *buf, size_t sz, size_t bufsiz);

static inline ssize_t
coio_read(struct coio *coio, void *buf, size_t sz)
{
	return coio_read_ahead(coio, buf, sz, sz);
}

static inline ssize_t
coio_readn(struct coio *coio, void *buf, size_t sz)
{
	return coio_readn_ahead(coio, buf, sz, sz);
}

void
coio_write(struct coio *coio, const void *buf, size_t sz);

ssize_t
coio_writev(struct coio *coio, struct iovec *iov, int iovcnt, size_t size);

void
coio_service_init(struct coio_service *service, const char *name,
		  const char *host, int port,
		  void (*handler)(va_list ap), void *handler_param);

#endif /* TARANTOOL_COIO_H_INCLUDED */
