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
#include "swim_transport.h"
#include "evio.h"
#include "diag.h"

ssize_t
swim_transport_send(struct swim_transport *transport, const void *data,
		    size_t size, const struct sockaddr *addr,
		    socklen_t addr_size)
{
	ssize_t ret = sio_sendto(transport->fd, data, size, 0, addr, addr_size);
	if (ret == -1 && sio_wouldblock(errno))
		return 0;
	return ret;
}

ssize_t
swim_transport_recv(struct swim_transport *transport, void *buffer, size_t size,
		    struct sockaddr *addr, socklen_t *addr_size)
{
	ssize_t ret = sio_recvfrom(transport->fd, buffer, size, 0, addr,
				   addr_size);
	if (ret == -1 && sio_wouldblock(errno))
		return 0;
	return ret;
}

int
swim_transport_bind(struct swim_transport *transport,
		    const struct sockaddr *addr, socklen_t addr_len)
{
	assert(addr->sa_family == AF_INET);
	const struct sockaddr_in *new_addr = (const struct sockaddr_in *) addr;
	const struct sockaddr_in *old_addr = &transport->addr;
	assert(addr_len == sizeof(*new_addr));

	if (transport->fd != -1 &&
	    new_addr->sin_addr.s_addr == old_addr->sin_addr.s_addr &&
	    new_addr->sin_port == old_addr->sin_port)
		return 0;

	int fd = sio_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0)
		return -1;
	if (sio_bind(fd, (struct sockaddr *) addr, addr_len) != 0 ||
	    evio_setsockopt_server(fd, AF_INET, SOCK_DGRAM) != 0) {
		if (errno == EADDRINUSE)
			diag_set(SocketError, sio_socketname(fd), "bind");
		close(fd);
		return -1;
	}
	int real_port = new_addr->sin_port;
	if (new_addr->sin_port == 0) {
		struct sockaddr_in real_addr;
		addr_len = sizeof(real_addr);
		if (sio_getsockname(fd, (struct sockaddr *) &real_addr,
				    &addr_len) != 0) {
			close(fd);
			return -1;
		}
		real_port = real_addr.sin_port;
	}
	if (transport->fd != -1)
		close(transport->fd);
	transport->fd = fd;
	transport->addr = *new_addr;
	transport->addr.sin_port = real_port;
	return 0;
}

void
swim_transport_destroy(struct swim_transport *transport)
{
	if (transport->fd != -1)
		close(transport->fd);
}

void
swim_transport_create(struct swim_transport *transport)
{
	transport->fd = -1;
	memset(&transport->addr, 0, sizeof(transport->addr));
}
