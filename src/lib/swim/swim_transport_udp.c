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
#include <ifaddrs.h>
#include <net/if.h>

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
	bool is_new_port_any = new_addr->sin_port == 0;

	if (transport->fd != -1 &&
	    new_addr->sin_addr.s_addr == old_addr->sin_addr.s_addr &&
	    (new_addr->sin_port == old_addr->sin_port || is_new_port_any)) {
		/*
		 * Note, that new port == 0 means that any port is
		 * ok. If at the same time old and new IP
		 * addresses are the same and the socket is
		 * already bound (fd != -1), then the existing
		 * socket 'matches' the new URI and rebind is not
		 * needed.
		 */
		return 0;
	}

	int is_on = 1;
	int real_port = new_addr->sin_port;
	int fd = sio_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0)
		return -1;
	if (sio_bind(fd, (struct sockaddr *) addr, addr_len) != 0) {
		if (errno == EADDRINUSE)
			diag_set(SocketError, sio_socketname(fd), "bind");
		goto end_error;
	}
	if (sio_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &is_on,
			   sizeof(is_on)) != 0)
		goto end_error;
	if (evio_setsockopt_server(fd, AF_INET, SOCK_DGRAM) != 0)
		goto end_error;
	if (is_new_port_any) {
		struct sockaddr_in real_addr;
		addr_len = sizeof(real_addr);
		if (sio_getsockname(fd, (struct sockaddr *) &real_addr,
				    &addr_len) != 0)
			goto end_error;
		real_port = real_addr.sin_port;
	}
	if (transport->fd != -1)
		close(transport->fd);
	transport->fd = fd;
	transport->addr = *new_addr;
	transport->addr.sin_port = real_port;
	return 0;
end_error:
	close(fd);
	return -1;
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

int
swim_getifaddrs(struct ifaddrs **ifaddrs)
{
	if (getifaddrs(ifaddrs) == 0)
		return 0;
	diag_set(SystemError, "failed to take an interface list by getifaddrs");
	return -1;
}

void
swim_freeifaddrs(struct ifaddrs *ifaddrs)
{
	freeifaddrs(ifaddrs);
}
