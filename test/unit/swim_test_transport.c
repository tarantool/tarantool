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
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>

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

static inline int
swim_test_sockaddr_in_to_fd(const struct sockaddr_in *addr)
{
	assert(addr->sin_family == AF_INET);
	return ntohs(addr->sin_port) + FAKE_FD_BASE;
}

static inline void
swim_test_fd_to_sockaddr_in(int fd, struct sockaddr_in *addr)
{
	*addr = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_port = htons(fd) - FAKE_FD_BASE,
		.sin_addr = {
			.s_addr = htonl(INADDR_LOOPBACK),
		},
	};
}

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
	rlist_del_entry(p, in_queue);
	free(p);
}

/** Fully duplicate a packet on new memory. */
static inline struct swim_test_packet *
swim_test_packet_dup(struct swim_test_packet *p)
{
	int size = sizeof(struct swim_test_packet) + p->size;
	struct swim_test_packet *res = (struct swim_test_packet *) malloc(size);
	assert(res != NULL);
	memcpy(res, p, size);
	rlist_create(&res->in_queue);
	return res;
}

/**
 * Packet filter. Each fake file descriptor has a list of filters.
 * For each incoming and outgoing packet it checks all the
 * filters in the list. If anyone wants to filter the packet out,
 * then the packet is dropped.
 */
struct swim_fd_filter {
	/** A function to decide whether to drop a packet. */
	swim_test_filter_check_f check;
	/**
	 * Arbitrary user data. Passed to each call of @a check.
	 */
	void *udata;
	/** Link in the list of filters in the descriptor. */
	struct rlist in_filters;
};

/** Create a new filter. */
static inline struct swim_fd_filter *
swim_fd_filter_new(swim_test_filter_check_f check, void *udata)
{
	struct swim_fd_filter *f = (struct swim_fd_filter *) malloc(sizeof(*f));
	assert(f != NULL);
	f->udata = udata;
	f->check = check;
	rlist_create(&f->in_filters);
	return f;
}

/** Delete @a filter and its data. */
static inline void
swim_fd_filter_delete(struct swim_fd_filter *filter)
{
	rlist_del_entry(filter, in_filters);
	free(filter);
}

/** Fake file descriptor. */
struct swim_fd {
	/** File descriptor number visible to libev. */
	int evfd;
	/**
	 * True, if the descriptor is opened and can receive new
	 * messages. Regardless of blocked or not. In case of
	 * blocked, new messages are queued, but not delivered.
	 */
	bool is_opened;
	/**
	 * List of packet filters. All of them are checked for
	 * each packet, and if at least one decides to drop, then
	 * the packet is deleted.
	 */
	struct rlist filters;
	/**
	 * Link in the list of opened and non-blocked descriptors.
	 * Used to feed them all EV_WRITE.
	 */
	struct rlist in_active;
	/** Queue of received, but not processed packets. */
	struct rlist recv_queue;
	/** Queue of sent, but not received packets. */
	struct rlist send_queue;
};

/** Table of fake file descriptors. */
static struct swim_fd swim_fd[FAKE_FD_NUMBER];
/**
 * List of active file descriptors. Used to avoid fullscan of the
 * table.
 */
static RLIST_HEAD(swim_fd_active);

/** Open a fake file descriptor. */
static inline int
swim_fd_open(struct swim_fd *fd)
{
	if (fd->is_opened) {
		errno = EADDRINUSE;
		diag_set(SocketError, "test_socket:1", "bind");
		return -1;
	}
	assert(rlist_empty(&fd->filters));
	fd->is_opened = true;
	rlist_add_tail_entry(&swim_fd_active, fd, in_active);
	return 0;
}

void
swim_test_transport_remove_filter(int fd, swim_test_filter_check_f check)
{
	struct swim_fd *sfd = &swim_fd[fd - FAKE_FD_BASE];
	assert(sfd->is_opened);
	struct swim_fd_filter *f;
	rlist_foreach_entry(f, &sfd->filters, in_filters) {
		if (check == f->check) {
			swim_fd_filter_delete(f);
			return;
		}
	}
}

void
swim_test_transport_add_filter(int fd, swim_test_filter_check_f check,
			       void *udata)
{
	struct swim_fd *sfd = &swim_fd[fd - FAKE_FD_BASE];
	assert(sfd->is_opened);
	struct swim_fd_filter *f = swim_fd_filter_new(check, udata);
	swim_test_transport_remove_filter(fd, check);
	rlist_add_tail_entry(&sfd->filters, f, in_filters);
}

/** Send one packet to destination's recv queue. */
static inline void
swim_fd_send_packet(struct swim_fd *fd);

/** Close a fake file descriptor. */
static inline void
swim_fd_close(struct swim_fd *fd)
{
	if (! fd->is_opened)
		return;
	struct swim_fd_filter *f, *f_tmp;
	rlist_foreach_entry_safe(f, &fd->filters, in_filters, f_tmp)
		swim_fd_filter_delete(f);
	struct swim_test_packet *i, *tmp;
	rlist_foreach_entry_safe(i, &fd->recv_queue, in_queue, tmp)
		swim_test_packet_delete(i);
	while (! rlist_empty(&fd->send_queue))
		swim_fd_send_packet(fd);
	rlist_del_entry(fd, in_active);
	fd->is_opened = false;
}

/**
 * Check all the packet filters if any wants to drop @a p packet.
 * @a dir parameter says direction. Values are the same as for
 * standard in/out descriptors: 0 for input, 1 for output.
 * @a peer_fd says sender/receiver file descriptor depending on
 * @a dir.
 */
static inline bool
swim_fd_test_if_drop(struct swim_fd *fd, const struct swim_test_packet *p,
		     int dir, int peer_fd)
{
	struct swim_fd_filter *f;
	rlist_foreach_entry(f, &fd->filters, in_filters) {
		if (f->check(p->data, p->size, f->udata, dir, peer_fd))
			return true;
	}
	return false;
}

void
swim_test_transport_init(void)
{
	for (int i = 0, evfd = FAKE_FD_BASE; i < FAKE_FD_NUMBER; ++i, ++evfd) {
		rlist_create(&swim_fd[i].filters);
		swim_fd[i].evfd = evfd;
		swim_fd[i].is_opened = false;
		rlist_create(&swim_fd[i].in_active);
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
	struct sockaddr_in src_addr;
	swim_test_fd_to_sockaddr_in(transport->fd, &src_addr);
	struct swim_test_packet *p =
		swim_test_packet_new(data, size, &src_addr,
				     (const struct sockaddr_in *) addr);
	struct swim_fd *src = &swim_fd[transport->fd - FAKE_FD_BASE];
	assert(src->is_opened);
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
	 * Pop a packet from a receiving queue.
	 */
	struct swim_fd *dst = &swim_fd[transport->fd - FAKE_FD_BASE];
	assert(dst->is_opened);
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
	int new_fd = swim_test_sockaddr_in_to_fd(new_addr);
	int old_fd = transport->fd;
	if (old_fd == new_fd) {
		transport->addr = *new_addr;
		return 0;
	}
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
	assert(! rlist_empty(&sfd->in_active));
	rlist_del_entry(sfd, in_active);
}

void
swim_test_transport_unblock_fd(int fd)
{
	struct swim_fd *sfd = &swim_fd[fd - FAKE_FD_BASE];
	if (sfd->is_opened && rlist_empty(&sfd->in_active))
		rlist_add_tail_entry(&swim_fd_active, sfd, in_active);
}

/**
 * Move @a p packet, originated from @a src descriptor's send
 * queue, to @a dst descriptor's recv queue. The function checks
 * if @a dst is opened, and tries a chance to drop the packet, if
 * drop rate is not 0.
 */
static inline void
swim_move_packet(struct swim_fd *src, struct swim_fd *dst,
		 struct swim_test_packet *p)
{
	if (dst->is_opened && !swim_fd_test_if_drop(dst, p, 0, src->evfd) &&
	    !swim_fd_test_if_drop(src, p, 1, dst->evfd))
		rlist_add_tail_entry(&dst->recv_queue, p, in_queue);
	else
		swim_test_packet_delete(p);
}

static inline void
swim_fd_send_packet(struct swim_fd *fd)
{
	assert(! rlist_empty(&fd->send_queue));
	struct swim_fd *dst;
	struct swim_test_packet *dup, *p =
		rlist_shift_entry(&fd->send_queue, struct swim_test_packet,
				  in_queue);
	if (p->dst.sin_addr.s_addr == INADDR_BROADCAST &&
	    p->dst.sin_port == 0) {
		rlist_foreach_entry(dst, &swim_fd_active, in_active) {
			dup = swim_test_packet_dup(p);
			swim_move_packet(fd, dst, dup);
		}
		swim_test_packet_delete(p);
	} else {
		int fdnum = swim_test_sockaddr_in_to_fd(&p->dst);
		dst = &swim_fd[fdnum - FAKE_FD_BASE];
		swim_move_packet(fd, dst, p);
	}
}

/**
 * Feed EV_WRITE/READ events to the descriptors having something
 * to send/recv.
 */
static inline void
swim_test_transport_feed_events(struct ev_loop *loop)
{
	struct swim_fd *fd;
	/*
	 * Reversed because libev invokes events in reversed
	 * order. So this reverse + libev reverse = normal order.
	 */
	rlist_foreach_entry_reverse(fd, &swim_fd_active, in_active) {
		if (! rlist_empty(&fd->send_queue))
			swim_fd_send_packet(fd);
		ev_feed_fd_event(loop, fd->evfd, EV_WRITE);
	}
	rlist_foreach_entry_reverse(fd, &swim_fd_active, in_active) {
		if (!rlist_empty(&fd->recv_queue))
			ev_feed_fd_event(loop, fd->evfd, EV_READ);
	}
}

void
swim_test_transport_do_loop_step(struct ev_loop *loop)
{
	do {
		ev_invoke_pending(loop);
		swim_test_transport_feed_events(loop);
		/*
		 * Just a single loop + invoke is not enough. At
		 * least two are necessary.
		 *
		 * First loop does nothing since send queues are
		 * empty. First invoke fills send queues.
		 *
		 * Second loop moves messages from send to recv
		 * queues. Second invoke processes messages in
		 * recv queues.
		 *
		 * With indirect messages even 2 cycles is not
		 * enough - processing of one received message can
		 * add a new message into another send queue.
		 */
	} while (ev_pending_count(loop) > 0);
}

int
swim_getifaddrs(struct ifaddrs **ifaddrs)
{
	/*
	 * This is a fake implementation of getifaddrs. It always
	 * returns two interfaces. First is a normal broadcast,
	 * which is later used to send a packet to all the opened
	 * descriptors. Second is a dummy interface leading to
	 * nowhere. The latter is used just for testing that the
	 * real SWIM code correctly iterates through the
	 * interface list.
	 */
	int size = (sizeof(struct ifaddrs) + sizeof(struct sockaddr_in)) * 2;
	struct ifaddrs *iface = (struct ifaddrs *) calloc(1, size);
	assert(iface != NULL);
	struct ifaddrs *iface_next = &iface[1];
	iface->ifa_next = iface_next;

	struct sockaddr_in *broadaddr = (struct sockaddr_in *) &iface_next[1];
	broadaddr->sin_family = AF_INET;
	broadaddr->sin_addr.s_addr = INADDR_BROADCAST;
	iface->ifa_flags = IFF_UP | IFF_BROADCAST;
	iface->ifa_broadaddr = (struct sockaddr *) broadaddr;

	struct sockaddr_in *dummy_addr = &broadaddr[1];
	dummy_addr->sin_family = AF_INET;
	iface_next->ifa_flags = IFF_UP;
	iface_next->ifa_addr = (struct sockaddr *) dummy_addr;

	*ifaddrs = iface;
	return 0;
}

void
swim_freeifaddrs(struct ifaddrs *ifaddrs)
{
	/*
	 * The whole list is packed into a single allocation
	 * above.
	 */
	free(ifaddrs);
}
