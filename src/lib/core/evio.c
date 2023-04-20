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
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <trivia/util.h>
#include "exception.h"
#include "iostream.h"
#include "tt_strerror.h"
#include "uri/uri.h"

struct evio_service_entry {
	/** Bind URI */
	struct uri uri;
	/** Interface/port to bind to */
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	socklen_t addr_len;
	/** IO stream context. */
	struct iostream_ctx io_ctx;
	/** libev io object for the acceptor socket. */
	struct ev_io ev;
	/** Pointer to the root evio_service, which contains this object */
	struct evio_service *service;
};

static int
evio_setsockopt_keepalive(int fd)
{
	int on = 1;
	/*
	 * SO_KEEPALIVE to ensure connections don't hang
	 * around for too long when a link goes away.
	 */
	if (sio_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
		       &on, sizeof(on)))
		return -1;
#ifdef __linux__
	/*
	 * On Linux, we are able to fine-tune keepalive
	 * intervals. Set smaller defaults, since the system-wide
	 * defaults are in days.
	 */
	int keepcnt = 5;
	if (sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt,
		       sizeof(int)))
		return -1;
	int keepidle = 30;

	if (sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle,
		       sizeof(int)))
		return -1;

	int keepintvl = 60;
	if (sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl,
		       sizeof(int)))
		return -1;
#endif
	return 0;
}

/** Set common client socket options. */
int
evio_setsockopt_client(int fd, int family, int type)
{
	int on = 1;
	/* In case this throws, the socket is not leaked. */
	if (sio_setfl(fd, O_NONBLOCK, on))
		return -1;
	if (type == SOCK_STREAM && family != AF_UNIX) {
		/*
		 * SO_KEEPALIVE to ensure connections don't hang
		 * around for too long when a link goes away.
		 */
		if (evio_setsockopt_keepalive(fd) != 0)
			return -1;
		/*
		 * Lower latency is more important than higher
		 * bandwidth, and we usually write entire
		 * request/response in a single syscall.
		 */
		if (sio_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				   &on, sizeof(on)))
			return -1;
	}
	return 0;
}

int
evio_setsockopt_server(int fd, int family, int type)
{
	int on = 1;
	/* In case this throws, the socket is not leaked. */
	if (sio_setfl(fd, O_NONBLOCK, on))
		return -1;
	/* Allow reuse local adresses. */
	if (sio_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		       &on, sizeof(on)))
		return -1;

#ifndef TARANTOOL_WSL1_WORKAROUND_ENABLED
	/* Send all buffered messages on socket before take
	 * control out from close(2) or shutdown(2). */
	struct linger linger = { 0, 0 };

	if (sio_setsockopt(fd, SOL_SOCKET, SO_LINGER,
		       &linger, sizeof(linger)))
		return -1;
#endif
	if (type == SOCK_STREAM && family != AF_UNIX &&
	    evio_setsockopt_keepalive(fd) != 0)
		return -1;
	return 0;
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
evio_service_entry_accept_cb(ev_loop *loop, ev_io *watcher, int events)
{
	(void) loop;
	(void) events;
	struct evio_service_entry *entry =
		(struct evio_service_entry *)watcher->data;
	int fd;
	while (1) {
		/*
		 * Accept all pending connections from backlog during event
		 * loop iteration. Significally speed up acceptor with enabled
		 * io_collect_interval.
		 */
		struct sockaddr_storage addr;
		socklen_t addrlen = sizeof(addr);
		fd = sio_accept(entry->ev.fd, (struct sockaddr *)&addr,
				&addrlen);

		if (fd < 0) {
			if (! sio_wouldblock(errno))
				break;
			return;
		}
		if (evio_setsockopt_client(fd, entry->addr.sa_family,
					   SOCK_STREAM) != 0)
			break;
		struct iostream io;
		if (iostream_create(&io, fd, &entry->io_ctx) != 0)
			break;
		if (entry->service->on_accept(entry->service, &io,
					      (struct sockaddr *)&addr,
					      addrlen) != 0) {
			iostream_destroy(&io);
			break;
		}
		/* Must be moved by the callback. */
		assert(!iostream_is_initialized(&io));
	}
	if (fd >= 0)
		close(fd);
	diag_log();
}

/*
 * Check if the UNIX socket exists and no one is
 * listening on it. Unlink the file if it's the case.
 */
static int
evio_service_entry_reuse_addr(const struct uri *u)
{
	if (u->host != NULL && strcmp(u->host, URI_HOST_UNIX) != 0)
		return 0;

	struct sockaddr_un un = { 0 };
	assert(u->service != NULL);
	strlcpy(un.sun_path, u->service, sizeof(un.sun_path));
	un.sun_family = AF_UNIX;

	int cl_fd = sio_socket(un.sun_family, SOCK_STREAM, 0);
	if (cl_fd < 0)
		return -1;

	if (connect(cl_fd, (struct sockaddr *)&un, sizeof(un)) == 0)
		goto err;

	if (errno == ECONNREFUSED && unlink(un.sun_path) != 0)
		goto err;

	close(cl_fd);
	return 0;
err:
	errno = EADDRINUSE;
	diag_set(SocketError, sio_socketname(cl_fd), "unlink");
	close(cl_fd);
	return -1;
}

/**
 * Try to bind on the configured port.
 *
 * Throws an exception if error.
 */
static int
evio_service_entry_bind_addr(struct evio_service_entry *entry)
{
	say_debug("%s: binding to %s...",
		  evio_service_name(entry->service),
		  sio_strfaddr(&entry->addr, entry->addr_len));
	/* Create a socket. */
	int fd = sio_socket(entry->addr.sa_family,
			    SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0)
		return -1;

	if (evio_setsockopt_server(fd, entry->addr.sa_family,
				   SOCK_STREAM) != 0)
		goto error;

	if (sio_bind(fd, &entry->addr, entry->addr_len) != 0)
		goto error;

	/*
	 * After binding a result address may be different. For
	 * example, if a port was 0.
	 */
	if (sio_getsockname(fd, &entry->addr, &entry->addr_len) != 0)
		goto error;

	say_info("%s: bound to %s",
		 evio_service_name(entry->service),
		 sio_strfaddr(&entry->addr, entry->addr_len));

	/* Register the socket in the event loop. */
	ev_io_set(&entry->ev, fd, EV_READ);
	return 0;
error:
	close(fd);
	return -1;
}

/**
 * Listen on bounded port.
 *
 * @retval 0 for success
 */
static int
evio_service_entry_listen(struct evio_service_entry *entry)
{
	say_debug("%s: listening on %s...",
		  evio_service_name(entry->service),
		  sio_strfaddr(&entry->addr, entry->addr_len));

	int fd = entry->ev.fd;
	if (sio_listen(fd))
		return -1;
	if (entry->service->on_accept != NULL)
		ev_io_start(entry->service->loop, &entry->ev);
	return 0;
}

static void
evio_service_entry_create(struct evio_service_entry *entry,
			  struct evio_service *service)
{
	uri_create(&entry->uri, NULL);
	memset(&entry->addrstorage, 0, sizeof(entry->addrstorage));
	entry->addr_len = 0;
	iostream_ctx_clear(&entry->io_ctx);
	/*
	 * Initialize libev objects to be able to detect if they
	 * are active or not in evio_service_entry_stop().
	 */
	ev_init(&entry->ev, evio_service_entry_accept_cb);
	ev_io_set(&entry->ev, -1, 0);
	entry->ev.data = entry;
	entry->service = service;
}

/**
 * Try to bind.
 */
static int
evio_service_entry_bind(struct evio_service_entry *entry, const struct uri *u)
{
	assert(u->service != NULL);
	assert(!ev_is_active(&entry->ev));

	if (iostream_ctx_create(&entry->io_ctx, IOSTREAM_SERVER, u) != 0)
		return -1;

	uri_destroy(&entry->uri);
	uri_copy(&entry->uri, u);

	if (u->host != NULL && strcmp(u->host, URI_HOST_UNIX) == 0) {
		/* UNIX domain socket */
		struct sockaddr_un *un = (struct sockaddr_un *) &entry->addr;
		entry->addr_len = sizeof(*un);
		strlcpy(un->sun_path, u->service, sizeof(un->sun_path));
		un->sun_family = AF_UNIX;
		return evio_service_entry_bind_addr(entry);
	}

	/* IP socket */
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;

	if (getaddrinfo(u->host, u->service, &hints, &res) != 0 ||
	    res == NULL) {
		diag_set(SocketError, sio_socketname(-1),
			 "can't resolve uri for bind");
		return -1;
	}
	for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
		memcpy(&entry->addr, ai->ai_addr, ai->ai_addrlen);
		entry->addr_len = ai->ai_addrlen;
		if (evio_service_entry_bind_addr(entry) == 0) {
			freeaddrinfo(res);
			return 0;
		}
		say_error("%s: failed to bind on %s: %s",
			  evio_service_name(entry->service),
			  sio_strfaddr(ai->ai_addr, ai->ai_addrlen),
			  diag_last_error(diag_get())->errmsg);
	}
	freeaddrinfo(res);
	diag_set(SocketError, sio_socketname(-1), "%s: failed to bind",
		 evio_service_name(entry->service));
	return -1;
}

static void
evio_service_entry_detach(struct evio_service_entry *entry)
{
	iostream_ctx_destroy(&entry->io_ctx);
	if (ev_is_active(&entry->ev)) {
		ev_io_stop(entry->service->loop, &entry->ev);
		entry->addr_len = 0;
	}
	ev_io_set(&entry->ev, -1, 0);
	uri_destroy(&entry->uri);
}

/** It's safe to stop a service entry which is not started yet. */
static void
evio_service_entry_stop(struct evio_service_entry *entry)
{
	int service_fd = entry->ev.fd;
	evio_service_entry_detach(entry);
	if (service_fd < 0)
		return;

	if (close(service_fd) < 0)
		say_error("Failed to close socket: %s", tt_strerror(errno));

	if (entry->addr.sa_family != AF_UNIX)
		return;

	if (unlink(((struct sockaddr_un *)&entry->addr)->sun_path) < 0) {
		say_error("Failed to unlink unix "
			  "socket path: %s", tt_strerror(errno));
	}
}

static void
evio_service_entry_attach(struct evio_service_entry *dst,
			 const struct evio_service_entry *src)
{
	assert(!ev_is_active(&dst->ev));
	uri_destroy(&dst->uri);
	uri_copy(&dst->uri, &src->uri);
	dst->addrstorage = src->addrstorage;
	dst->addr_len = src->addr_len;
	iostream_ctx_copy(&dst->io_ctx, &src->io_ctx);
	ev_io_set(&dst->ev, src->ev.fd, EV_READ);
	ev_io_start(dst->service->loop, &dst->ev);
}

/** Recreate the IO stream contexts from the service entry URI. */
static int
evio_service_entry_reload_uri(struct evio_service_entry *entry)
{
	struct iostream_ctx io_ctx;
	if (iostream_ctx_create(&io_ctx, IOSTREAM_SERVER, &entry->uri) != 0)
		return -1;
	iostream_ctx_destroy(&entry->io_ctx);
	iostream_ctx_move(&entry->io_ctx, &io_ctx);
	return 0;
}

static inline int
evio_service_reuse_addr(const struct uri_set *uri_set)
{
	for (int i = 0; i < uri_set->uri_count; i++) {
		const struct uri *uri = &uri_set->uris[i];
		if (evio_service_entry_reuse_addr(uri) != 0)
			return -1;
	}
	return 0;
}

static void
evio_service_create_entries(struct evio_service *service, int size)
{
	service->entry_count = size;
	service->entries = (size != 0 ?
		 xmalloc(size *sizeof(struct evio_service_entry)) : NULL);
	for (int i = 0; i < service->entry_count; i++)
		evio_service_entry_create(&service->entries[i], service);
}

int
evio_service_count(const struct evio_service *service)
{
	return service->entry_count;
}

const struct sockaddr *
evio_service_addr(const struct evio_service *service, int idx, socklen_t *size)
{
	assert(idx < service->entry_count);
	const struct evio_service_entry *e = &service->entries[idx];
	*size = e->addr_len;
	return &e->addr;
}

void
evio_service_create(struct ev_loop *loop, struct evio_service *service,
		    const char *name, evio_accept_f on_accept,
		    void *on_accept_param)
{
	memset(service, 0, sizeof(struct evio_service));
	snprintf(service->name, sizeof(service->name), "%s", name);
	service->loop = loop;
	service->on_accept = on_accept;
	service->on_accept_param = on_accept_param;
}

void
evio_service_attach(struct evio_service *dst, const struct evio_service *src)
{
	assert(dst->entry_count == 0);
	evio_service_create_entries(dst, src->entry_count);
	for (int i = 0; i < src->entry_count; i++)
		evio_service_entry_attach(&dst->entries[i], &src->entries[i]);
}

void
evio_service_detach(struct evio_service *service)
{
	if (service->entries == NULL)
		return;
	for (int i = 0; i < service->entry_count; i++)
		evio_service_entry_detach(&service->entries[i]);
	free(service->entries);
	service->entry_count = 0;
	service->entries = NULL;
}

/** Listen on bound socket. */
static int
evio_service_listen(struct evio_service *service)
{
	for (int i = 0; i < service->entry_count; i++) {
		if (evio_service_entry_listen(&service->entries[i]) != 0)
			return -1;
	}
	return 0;
}

void
evio_service_stop(struct evio_service *service)
{
	if (service->entries == NULL)
		return;
	say_info("%s: stopped", evio_service_name(service));
	for (int i = 0; i < service->entry_count; i++)
		evio_service_entry_stop(&service->entries[i]);
	free(service->entries);
	service->entry_count = 0;
	service->entries = NULL;
}

/** Bind service to specified URI. */
static int
evio_service_bind(struct evio_service *service, const struct uri_set *uri_set)
{
	if (evio_service_reuse_addr(uri_set) != 0)
		return -1;
	evio_service_create_entries(service, uri_set->uri_count);
	for (int i = 0; i < uri_set->uri_count; i++) {
		const struct uri *uri = &uri_set->uris[i];
		if (evio_service_entry_bind(&service->entries[i], uri) != 0)
			return -1;
	}
	return 0;
}

int
evio_service_start(struct evio_service *service, const struct uri_set *uri_set)
{
	if (evio_service_bind(service, uri_set) != 0)
		return -1;
	if (evio_service_listen(service) != 0)
		return -1;
	return 0;
}

int
evio_service_reload_uris(struct evio_service *service)
{
	for (int i = 0; i < service->entry_count; i++) {
		if (evio_service_entry_reload_uri(&service->entries[i]) != 0)
			return -1;
	}
	return 0;
}
