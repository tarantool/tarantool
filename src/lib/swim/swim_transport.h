#ifndef TARANTOOL_SWIM_TRANSPORT_H_INCLUDED
#define TARANTOOL_SWIM_TRANSPORT_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct ifaddrs;

/** Transport implementation. */
struct swim_transport {
	/** Socket. */
	int fd;
	/** Socket address. */
	struct sockaddr_in addr;
};

/**
 * Despite there are no transport vtab, those are virtual methods.
 * But virtualization is handled on compilation time. This header
 * file has one implementation for server, and another for tests.
 * Transport source is built as a separate library.
 *
 * Methods below for server mostly are just wrappers of
 * corresponding system calls, working with UDP sockets.
 */

ssize_t
swim_transport_send(struct swim_transport *transport, const void *data,
		    size_t size, const struct sockaddr *addr,
		    socklen_t addr_size);

ssize_t
swim_transport_recv(struct swim_transport *transport, void *buffer, size_t size,
		    struct sockaddr *addr, socklen_t *addr_size);

/**
 * Bind @a transport to a new address. The old socket, if exists,
 * is closed. If @a addr is from INET family and has 0 port, then
 * @a transport will save not 0 port, but a real one, got after
 * bind() using getsockname().
 */
int
swim_transport_bind(struct swim_transport *transport,
		    const struct sockaddr *addr, socklen_t addr_len);

void
swim_transport_destroy(struct swim_transport *transport);

void
swim_transport_create(struct swim_transport *transport);

/**
 * Get a list of network interfaces. Just a wrapper around
 * getifaddrs, but setting diag.
 */
int
swim_getifaddrs(struct ifaddrs **ifaddrs);

/** Delete an interface list created earlier with getifaddrs. */
void
swim_freeifaddrs(struct ifaddrs *ifaddrs);

#endif /* TARANTOOL_SWIM_TRANSPORT_H_INCLUDED */
