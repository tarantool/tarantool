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
#include "swim_test_transport.h"
#include "swim/swim_transport.h"
#include "swim/swim_io.h"
#include "fiber.h"
#include <errno.h>

enum {
	/**
	 * All fake sockets have fd >= 1000 in order to prevent
	 * possible intersections with real file descriptors.
	 */
	FAKE_FD_BASE = 1000,
	/**
	 * Maximal number of fake file descriptors. Nothing
	 * special about this value and fixed fd table size. It
	 * just simplifies code.
	 */
	FAKE_FD_NUMBER = 1000,
};

/** UDP packet wrapper. It is stored in send/recv queues. */
struct swim_test_packet {
	/** Source address. */
	struct sockaddr_in src;
	/** Destination address. */
	struct sockaddr_in dst;
	/** A link in send/recv queue. */
	struct rlist in_queue;
	/** Packet data size. */
	int size;
	/** Packet data. */
	char data[0];
};

/** Wrap @a data into a new packet. */
static inline struct swim_test_packet *
swim_test_packet_new(const char *data, int size, const struct sockaddr_in *src,
		     const struct sockaddr_in *dst)
{
	struct swim_test_packet *p =
		(struct swim_test_packet *) malloc(sizeof(*p) + size);
	assert(p != NULL);
	rlist_create(&p->in_queue);
	p->src = *src;
	p->dst = *dst;
	p->size = size;
	memcpy(p->data, data, size);
	return p;
}

/** Free packet memory. */
static inline void
swim_test_packet_delete(struct swim_test_packet *p)
{
	free(p);
}

/** Fake file descriptor. */
struct swim_fd {
	/** File descriptor number visible to libev. */
	int evfd;
	/**
	 * Link in the list of opened descriptors. Used to feed
	 * them all EV_WRITE.
	 */
	struct rlist in_opened;
	/** Queue of received, but not processed packets. */
	struct rlist recv_queue;
	/** Queue of sent, but not received packets. */
	struct rlist send_queue;
};

/** Table of fake file descriptors. */
static struct swim_fd swim_fd[FAKE_FD_NUMBER];
/**
 * List of opened file descriptors. Used to avoid fullscan of the
 * table.
 */
static RLIST_HEAD(swim_fd_opened);

/** Open a fake file descriptor. */
static inline int
swim_fd_open(struct swim_fd *fd)
{
	if (! rlist_empty(&fd->in_opened)) {
		errno = EADDRINUSE;
		diag_set(SocketError, "test_socket:1", "bind");
		return -1;
	}
	rlist_add_tail_entry(&swim_fd_opened, fd, in_opened);
	rlist_create(&fd->recv_queue);
	rlist_create(&fd->send_queue);
	return 0;
}

/** Close a fake file descriptor. */
static inline void
swim_fd_close(struct swim_fd *fd)
{
	struct swim_test_packet *i, *tmp;
	rlist_foreach_entry_safe(i, &fd->recv_queue, in_queue, tmp)
		swim_test_packet_delete(i);
	rlist_foreach_entry_safe(i, &fd->send_queue, in_queue, tmp)
		swim_test_packet_delete(i);
	rlist_del_entry(fd, in_opened);
}

void
swim_test_transport_init(void)
{
	for (int i = 0, evfd = FAKE_FD_BASE; i < FAKE_FD_NUMBER; ++i, ++evfd) {
		swim_fd[i].evfd = evfd;
		rlist_create(&swim_fd[i].in_opened);
		rlist_create(&swim_fd[i].recv_queue);
		rlist_create(&swim_fd[i].send_queue);
	}
}

void
swim_test_transport_free(void)
{
	struct swim_test_packet *p, *tmp;
	for (int i = 0; i < (int)lengthof(swim_fd); ++i)
		swim_fd_close(&swim_fd[i]);
}

/**
 * Wrap a packet and put into send queue. Packets are popped from
 * it on EV_WRITE event.
 */
ssize_t
swim_transport_send(struct swim_transport *transport, const void *data,
		    size_t size, const struct sockaddr *addr,
		    socklen_t addr_size)
{
	/*
	 * Create packet. Put into sending queue.
	 */
	(void) addr_size;
	assert(addr->sa_family == AF_INET);
	struct swim_test_packet *p =
		swim_test_packet_new(data, size, &transport->addr,
				     (const struct sockaddr_in *) addr);
	struct swim_fd *src = &swim_fd[transport->fd - FAKE_FD_BASE];
	rlist_add_tail_entry(&src->send_queue, p, in_queue);
	return size;
}

/**
 * Move a packet from send to recv queue. The packet is popped and
 * processed on EV_READ event.
 */
ssize_t
swim_transport_recv(struct swim_transport *transport, void *buffer, size_t size,
		    struct sockaddr *addr, socklen_t *addr_size)
{
	/*
	 * Pop a packet from a receving queue.
	 */
	struct swim_fd *dst = &swim_fd[transport->fd - FAKE_FD_BASE];
	struct swim_test_packet *p =
		rlist_shift_entry(&dst->recv_queue, struct swim_test_packet,
				  in_queue);
	*(struct sockaddr_in *) addr = p->src;
	*addr_size = sizeof(p->src);
	ssize_t result = MIN((size_t) p->size, size);
	memcpy(buffer, p->data, result);
	swim_test_packet_delete(p);
	return result;
}

int
swim_transport_bind(struct swim_transport *transport,
		    const struct sockaddr *addr, socklen_t addr_len)
{
	assert(addr->sa_family == AF_INET);
	const struct sockaddr_in *new_addr = (const struct sockaddr_in *) addr;
	int new_fd = ntohs(new_addr->sin_port) + FAKE_FD_BASE;
	int old_fd = transport->fd;
	if (old_fd == new_fd)
		return 0;
	if (swim_fd_open(&swim_fd[new_fd - FAKE_FD_BASE]) != 0)
		return -1;
	transport->fd = new_fd;
	transport->addr = *new_addr;
	if (old_fd != -1)
		swim_fd_close(&swim_fd[old_fd - FAKE_FD_BASE]);
	return 0;
}

void
swim_transport_destroy(struct swim_transport *transport)
{
	if (transport->fd != -1)
		swim_fd_close(&swim_fd[transport->fd - FAKE_FD_BASE]);
}

void
swim_transport_create(struct swim_transport *transport)
{
	transport->fd = -1;
	memset(&transport->addr, 0, sizeof(transport->addr));
}

void
swim_test_transport_block_fd(int fd)
{
	struct swim_fd *sfd = &swim_fd[fd - FAKE_FD_BASE];
	assert(! rlist_empty(&sfd->in_opened));
	rlist_del_entry(sfd, in_opened);
}

void
swim_test_transport_unblock_fd(int fd)
{
	struct swim_fd *sfd = &swim_fd[fd - FAKE_FD_BASE];
	assert(rlist_empty(&sfd->in_opened));
	rlist_add_tail_entry(&swim_fd_opened, sfd, in_opened);
}

/** Send one packet to destination's recv queue. */
static inline void
swim_fd_send_packet(struct swim_fd *fd)
{
	if (rlist_empty(&fd->send_queue))
		return;
	struct swim_test_packet *p =
		rlist_shift_entry(&fd->send_queue, struct swim_test_packet,
				  in_queue);
	int dst_i = ntohs(p->dst.sin_port);
	rlist_add_tail_entry(&swim_fd[dst_i].recv_queue, p, in_queue);
}

void
swim_transport_do_loop_step(struct ev_loop *loop)
{
	struct swim_fd *fd;
	/*
	 * Reversed because libev invokes events in reversed
	 * order. So this reverse + libev reverse = normal order.
	 */
	rlist_foreach_entry_reverse(fd, &swim_fd_opened, in_opened) {
		swim_fd_send_packet(fd);
		ev_feed_fd_event(loop, fd->evfd, EV_WRITE);
	}
	rlist_foreach_entry_reverse(fd, &swim_fd_opened, in_opened) {
		if (!rlist_empty(&fd->recv_queue))
			ev_feed_fd_event(loop, fd->evfd, EV_READ);
	}
}
