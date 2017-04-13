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
#include "evio.h"
#include "uri.h"
#include "scoped_guard.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <trivia/util.h>

static void
evio_setsockopt_server(int fd, int family, int type);

/** Note: this function does not throw. */
void
evio_close(ev_loop *loop, struct ev_io *evio)
{
	/* Stop I/O events. Safe to do even if not started. */
	ev_io_stop(loop, evio);
	/* Close the socket. */
	close(evio->fd);
	/* Make sure evio_has_fd() returns a proper value. */
	evio->fd = -1;
}

/**
 * Create an endpoint for communication.
 * Set socket as non-block and apply protocol specific options.
 */
void
evio_socket(struct ev_io *coio, int domain, int type, int protocol)
{
	assert(coio->fd == -1);
	/* Don't leak fd if setsockopt fails. */
	coio->fd = sio_socket(domain, type, protocol);
	evio_setsockopt_client(coio->fd, domain, type);
}

static void
evio_setsockopt_keepalive(int fd)
{
	int on = 1;
	/*
	 * SO_KEEPALIVE to ensure connections don't hang
	 * around for too long when a link goes away.
	 */
	sio_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
		       &on, sizeof(on));
#ifdef __linux__
	/*
	 * On Linux, we are able to fine-tune keepalive
	 * intervals. Set smaller defaults, since the system-wide
	 * defaults are in days.
	 */
	int keepcnt = 5;
	sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt,
		       sizeof(int));
	int keepidle = 30;

	sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle,
		       sizeof(int));

	int keepintvl = 60;
	sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl,
		       sizeof(int));
#endif
}

/** Set common client socket options. */
void
evio_setsockopt_client(int fd, int family, int type)
{
	int on = 1;
	/* In case this throws, the socket is not leaked. */
	sio_setfl(fd, O_NONBLOCK, on);
	if (type == SOCK_STREAM && family != AF_UNIX) {
		/*
		 * SO_KEEPALIVE to ensure connections don't hang
		 * around for too long when a link goes away.
		 */
		evio_setsockopt_keepalive(fd);
		/*
		 * Lower latency is more important than higher
		 * bandwidth, and we usually write entire
		 * request/response in a single syscall.
		 */
		sio_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	}
}

/** Set options for server sockets. */
static void
evio_setsockopt_server(int fd, int family, int type)
{
	int on = 1;
	/* In case this throws, the socket is not leaked. */
	sio_setfl(fd, O_NONBLOCK, on);
	/* Allow reuse local adresses. */
	sio_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		       &on, sizeof(on));

	/* Send all buffered messages on socket before take
	 * control out from close(2) or shutdown(2). */
	struct linger linger = { 0, 0 };

	sio_setsockopt(fd, SOL_SOCKET, SO_LINGER,
		       &linger, sizeof(linger));
	if (type == SOCK_STREAM && family != AF_UNIX)
		evio_setsockopt_keepalive(fd);
}

static inline const char *
evio_service_name(struct evio_service *service)
{
	return service->name;
}

/**
 * A callback invoked by libev when acceptor socket is ready.
 * Accept the socket, initialize it and pass to the on_accept
 * callback.
 */
static void
evio_service_accept_cb(ev_loop * /* loop */, ev_io *watcher,
		       int /* revents */)
{
	struct evio_service *service = (struct evio_service *) watcher->data;

	while (1) {
		/*
		 * Accept all pending connections from backlog during event
		 * loop iteration. Significally speed up acceptor with enabled
		 * io_collect_interval.
		 */
		int fd = -1;
		try {
			struct sockaddr_storage addr;
			socklen_t addrlen = sizeof(addr);
			fd = sio_accept(service->ev.fd,
				(struct sockaddr *)&addr, &addrlen);

			if (fd < 0) /* EAGAIN, EWOULDLOCK, EINTR */
				return;
			/* set common client socket options */
			evio_setsockopt_client(fd, service->addr.sa_family, SOCK_STREAM);
			/*
			 * Invoke the callback and pass it the accepted
			 * socket.
			 */
			service->on_accept(service, fd, (struct sockaddr *)&addr, addrlen);

		} catch (Exception *e) {
			if (fd >= 0)
				close(fd);
			e->log();
			return;
		}
	}
}

/*
 * Check if the unix socket which we file to create exists and
 * no one is listening on it. Unlink the file if it's the case.
 */
static bool
evio_service_reuse_addr(struct evio_service *service)
{
	if ((service->addr.sa_family != AF_UNIX) || (errno != EADDRINUSE))
		return false;
	int save_errno = errno;
	int cl_fd = sio_socket(service->addr.sa_family,
		SOCK_STREAM, 0);

	if (connect(cl_fd, &service->addr, service->addr_len) == 0)
		goto err;

	if (errno != ECONNREFUSED)
		goto err;

	if (unlink(((struct sockaddr_un *)(&service->addr))->sun_path))
		goto err;
	close(cl_fd);

	return true;
err:
	errno = save_errno;
	close(cl_fd);
	return false;
}

/**
 * Try to bind on the configured port.
 *
 * Throws an exception if error.
 */
static void
evio_service_bind_addr(struct evio_service *service)
{
	say_debug("%s: binding to %s...", evio_service_name(service),
		  sio_strfaddr(&service->addr, service->addr_len));
	/* Create a socket. */
	int fd = sio_socket(service->addr.sa_family,
		SOCK_STREAM, IPPROTO_TCP);

	auto fd_guard = make_scoped_guard([=]{ close(fd); });

	evio_setsockopt_server(fd, service->addr.sa_family, SOCK_STREAM);

	if (sio_bind(fd, &service->addr, service->addr_len)) {
		assert(errno == EADDRINUSE);
		if (!evio_service_reuse_addr(service) ||
			sio_bind(fd, &service->addr, service->addr_len)) {
			tnt_raise(SocketError, fd, "bind");
		}
	}

	say_info("%s: bound to %s", evio_service_name(service),
		 sio_strfaddr(&service->addr, service->addr_len));

	/* Register the socket in the event loop. */
	ev_io_set(&service->ev, fd, EV_READ);

	fd_guard.is_active = false;
}

/**
 * Listen on bounded port.
 *
 * @retval 0 for success
 */
void
evio_service_listen(struct evio_service *service)
{
	say_debug("%s: listening on %s...", evio_service_name(service),
		  sio_strfaddr(&service->addr, service->addr_len));

	int fd = service->ev.fd;
	if (sio_listen(fd)) {
		/* raise for addr in use to */
		tnt_raise(SocketError, fd, "listen");
	}
	ev_io_start(service->loop, &service->ev);
}

void
evio_service_init(ev_loop *loop,
		  struct evio_service *service, const char *name,
		  void (*on_accept)(struct evio_service *, int,
				    struct sockaddr *, socklen_t),
		  void *on_accept_param)
{
	memset(service, 0, sizeof(struct evio_service));
	snprintf(service->name, sizeof(service->name), "%s", name);

	service->loop = loop;

	service->on_accept = on_accept;
	service->on_accept_param = on_accept_param;
	/*
	 * Initialize libev objects to be able to detect if they
	 * are active or not in evio_service_stop().
	 */
	ev_init(&service->ev, evio_service_accept_cb);
	ev_io_set(&service->ev, -1, 0);
	service->ev.data = service;
}

/**
 * Try to bind.
 */
void
evio_service_bind(struct evio_service *service, const char *uri)
{
	struct uri u;
	if (uri_parse(&u, uri) || u.service == NULL)
		tnt_raise(SocketError, -1, "invalid uri for bind: %s", uri);

	snprintf(service->serv, sizeof(service->serv), "%.*s",
		 (int) u.service_len, u.service);
	if (u.host != NULL && strncmp(u.host, "*", u.host_len) != 0) {
		snprintf(service->host, sizeof(service->host), "%.*s",
			(int) u.host_len, u.host);
	} /* else { service->host[0] = '\0'; } */

	assert(! ev_is_active(&service->ev));

	if (strcmp(service->host, URI_HOST_UNIX) == 0) {
		/* UNIX domain socket */
		struct sockaddr_un *un = (struct sockaddr_un *) &service->addr;
		service->addr_len = sizeof(*un);
		snprintf(un->sun_path, sizeof(un->sun_path), "%s",
			 service->serv);
		un->sun_family = AF_UNIX;
		return evio_service_bind_addr(service);
	}

	/* IP socket */
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;

	/* make no difference between empty string and NULL for host */
	if (getaddrinfo(*service->host ? service->host : NULL, service->serv,
			&hints, &res) != 0 || res == NULL)
		tnt_raise(SocketError, -1, "can't resolve uri for bind");
	auto addrinfo_guard = make_scoped_guard([=]{ freeaddrinfo(res); });

	for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
		memcpy(&service->addr, ai->ai_addr, ai->ai_addrlen);
		service->addr_len = ai->ai_addrlen;
		try {
			return evio_service_bind_addr(service);
		} catch (SocketError *e) {
			say_error("%s: failed to bind on %s: %s",
				  evio_service_name(service),
				  sio_strfaddr(ai->ai_addr, ai->ai_addrlen),
				  e->get_errmsg());
			/* ignore */
		}
	}
	tnt_raise(SocketError, -1, "%s: failed to bind",
		  evio_service_name(service));
}

/** It's safe to stop a service which is not started yet. */
void
evio_service_stop(struct evio_service *service)
{
	say_info("%s: stopped", evio_service_name(service));

	if (ev_is_active(&service->ev)) {
		ev_io_stop(service->loop, &service->ev);
	}

	if (service->ev.fd >= 0) {
		close(service->ev.fd);
		ev_io_set(&service->ev, -1, 0);
		if (service->addr.sa_family == AF_UNIX) {
			unlink(((struct sockaddr_un *) &service->addr)->sun_path);
		}
	}
}
