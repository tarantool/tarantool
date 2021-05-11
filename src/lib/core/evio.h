#ifndef TARANTOOL_LIB_CORE_EVIO_H_INCLUDED
#define TARANTOOL_LIB_CORE_EVIO_H_INCLUDED
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
/**
 * Asynchronous IO in libev event loop.
 * Requires a running libev loop.
 */
#include <stdbool.h>
#include "tarantool_ev.h"
#include "sio.h"
#include "uri/uri.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * A way to add a listening socket to the event loop. Callbacks
 * are invoked on bind and accept events.
 *
 * Coroutines/fibers are not used for port listeners
 * since listener's job is usually simple and only involves
 * creating a session for the accepted socket. The session itself
 * can be built around simple libev callbacks, or around
 * cooperative multitasking (on_accept callback can create
 * a fiber and use coio.h (cooperative multi-tasking I/O)) API.
 *
 * How to use a service:
 * struct evio_service *service;
 * service = malloc(sizeof(struct evio_service));
 * evio_service_init(service, ..., on_accept_cb, ...);
 * evio_service_bind(service);
 * evio_service_listen(service);
 * ...
 * evio_service_stop(service);
 * free(service);
 *
 * If a service is not started, but only initialized, no
 * dedicated cleanup/destruction is necessary.
 */
struct evio_service;

typedef int (*evio_accept_f)(struct evio_service *, int, struct sockaddr *,
			      socklen_t);

struct evio_service
{
	/** Service name. E.g. 'primary', 'secondary', etc. */
	char name[SERVICE_NAME_MAXLEN];
	/** Bind host:service, useful for logging */
	char host[URI_MAXHOST];
	char serv[URI_MAXSERVICE];

	/** Interface/port to bind to */
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	socklen_t addr_len;

	/**
	 * A callback invoked on every accepted client socket.
	 * If a callback returned != 0, the accepted socket is
	 * closed and the error is logged.
	 */
	evio_accept_f on_accept;
	void *on_accept_param;

	/** libev io object for the acceptor socket. */
	struct ev_io ev;
	ev_loop *loop;
};

/** Initialize the service. Don't bind to the port yet. */
void
evio_service_init(ev_loop *loop, struct evio_service *service, const char *name,
		  evio_accept_f on_accept, void *on_accept_param);

/** Bind service to specified uri */
int
evio_service_bind(struct evio_service *service, const char *uri);

/**
 * Listen on bounded socket
 *
 * @retval 0 for success
 */
int
evio_service_listen(struct evio_service *service);

/** If started, stop event flow, without closing the acceptor socket. */
void
evio_service_detach(struct evio_service *service);

/** If started, stop event flow and close the acceptor socket. */
void
evio_service_stop(struct evio_service *service);

int
evio_socket(struct ev_io *coio, int domain, int type, int protocol);

void
evio_close(ev_loop *loop, struct ev_io *evio);

static inline bool
evio_service_is_active(struct evio_service *service)
{
	return service->ev.fd >= 0;
}

static inline bool
evio_has_fd(struct ev_io *ev)
{
	return ev->fd >= 0;
}

static inline void
evio_timeout_init(ev_loop *loop, ev_tstamp *start, ev_tstamp *delay,
		  ev_tstamp timeout)
{
	*start = ev_monotonic_now(loop);
	*delay = timeout;
}

static inline void
evio_timeout_update(ev_loop *loop, ev_tstamp *start, ev_tstamp *delay)
{
	ev_tstamp elapsed = ev_monotonic_now(loop) - *start;
	*start += elapsed;
	*delay = (elapsed >= *delay) ? 0 : *delay - elapsed;
}

int
evio_setsockopt_client(int fd, int family, int type);

/** Set options for server sockets. */
int
evio_setsockopt_server(int fd, int family, int type);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_EVIO_H_INCLUDED */
