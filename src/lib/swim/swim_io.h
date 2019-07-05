#ifndef TARANTOOL_SWIM_IO_H_INCLUDED
#define TARANTOOL_SWIM_IO_H_INCLUDED
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
#include "tt_static.h"
#include "small/rlist.h"
#include "salad/stailq.h"
#include "crypto/crypto.h"
#include "swim_transport.h"
#include "tarantool_ev.h"
#include "uuid/tt_uuid.h"
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/**
 * SWIM protocol transport level.
 */

struct swim_task;
struct swim_member;
struct swim_scheduler;

enum {
	/**
	 * Default MTU is 1500. MTU (when IPv4 is used) consists
	 * of IPv4 header, UDP header, Data. IPv4 has 20 bytes
	 * header, UDP - 8 bytes. So Data = 1500 - 20 - 8 = 1472.
	 * TODO: adapt to other MTUs which can be reduced in some
	 * networks by their admins. Or allow to specify MTU in
	 * configuration.
	 */
	UDP_PACKET_SIZE = 1472,
	/**
	 * Data can be encrypted, which usually makes it slightly
	 * bigger in size. Also, to decode data the receiver needs
	 * two keys: private key and public initial vector. Public
	 * initial vector is generated randomly for each packet
	 * and prepends the data. This is why maximal data size is
	 * reduced by one block and IV sizes.
	 */
	MAX_PACKET_SIZE = UDP_PACKET_SIZE - CRYPTO_MAX_BLOCK_SIZE -
			  CRYPTO_MAX_IV_SIZE,

};

/**
 * UDP packet. Works as an allocator, allowing to fill its body
 * gradually, while preserving prefix for metadata.
 *
 *          < - - - -MAX_PACKET_SIZE- - - - ->
 *          +--------+-----------------------+
 *          |  meta  |    body    |  *free*  |
 *          +--------+-----------------------+
 *          ^        ^            ^          ^
 *         meta     body         pos        end
 *         buf
 */
struct swim_packet {
	/** End of the body. */
	char *pos;
	/**
	 * Starting position of body in the buffer. Not the same
	 * as buf, because the latter has metadata at the
	 * beginning.
	 */
	char *body;
	/**
	 * Alias for swim_packet.buf. Just sugar for code working
	 * with meta.
	 */
	char meta[0];
	/** Packet body buffer. */
	char buf[MAX_PACKET_SIZE];
	/**
	 * Pointer to the end of the buffer. Just sugar to do not
	 * write 'buf + sizeof(buf)' each time.
	 */
	char end[0];
};

/**
 * Ensure that the packet can fit @a size bytes more. Multiple
 * reserves of the same size will return the same pointer until
 * advance is called.
 */
static inline char *
swim_packet_reserve(struct swim_packet *packet, int size)
{
	return packet->pos + size > packet->end ? NULL : packet->pos;
}

/**
 * Propagate body end pointer. This declares next @a size bytes as
 * occupied.
 */
static inline void
swim_packet_advance(struct swim_packet *packet, int size)
{
	assert(packet->pos + size <= packet->end);
	packet->pos += size;
}

/** Reserve + advance. */
static inline char *
swim_packet_alloc(struct swim_packet *packet, int size)
{
	char *res = swim_packet_reserve(packet, size);
	if (res == NULL)
		return NULL;
	swim_packet_advance(packet, size);
	return res;
}

/** Size of the packet body. Meta is not counted. */
static inline int
swim_packet_body_size(const struct swim_packet *packet)
{
	return packet->pos - packet->body;
}

/** Initialize @a packet, reserve some space for meta. */
void
swim_packet_create(struct swim_packet *packet);

typedef void (*swim_scheduler_on_input_f)(struct swim_scheduler *scheduler,
					  const char *buf, const char *end,
					  const struct sockaddr_in *src,
					  const struct sockaddr_in *proxy);

/** Planner and executor of input and output operations.*/
struct swim_scheduler {
	/** Transport to send/receive packets. */
	struct swim_transport transport;
	/**
	 * Codec to encode messages before sending, and decode
	 * before lifting up to the SWIM core logic.
	 */
	struct crypto_codec *codec;
	/**
	 * Function called when a packet is received. It takes
	 * packet body, while meta is handled by transport level
	 * completely.
	 */
	swim_scheduler_on_input_f on_input;
	/**
	 * Event dispatcher of incomming messages. Takes them from
	 * the network.
	 */
	struct ev_io input;
	/**
	 * Event dispatcher of outcomming messages. Takes tasks
	 * from queue_output.
	 */
	struct ev_io output;
	/** Queue of output tasks ready to write now. */
	struct rlist queue_output;
};

/** Initialize scheduler. */
void
swim_scheduler_create(struct swim_scheduler *scheduler,
		      swim_scheduler_on_input_f on_input);

/**
 * Bind or rebind the scheduler to an address. In case of rebind
 * the old socket is closed.
 */
int
swim_scheduler_bind(struct swim_scheduler *scheduler,
		    const struct sockaddr_in *addr);

/** Set a new codec to encrypt/decrypt messages. */
int
swim_scheduler_set_codec(struct swim_scheduler *scheduler,
			 enum crypto_algo algo, enum crypto_mode mode,
			 const char *key, int key_size);

/** Stop accepting new packets from the network. */
void
swim_scheduler_stop_input(struct swim_scheduler *scheduler);

/** Destroy scheduler, its queues, close the socket. */
void
swim_scheduler_destroy(struct swim_scheduler *scheduler);

/**
 * Each SWIM component in a common case independently may want to
 * push some data into the network. Dissemination sends events,
 * failure detection sends pings, acks. Anti-entropy sends member
 * tables. The intention to send a data is called IO task and is
 * stored in a queue that is dispatched when output is possible.
 */
typedef void (*swim_task_f)(struct swim_task *,
			    struct swim_scheduler *scheduler, int rc);

struct swim_task {
	/**
	 * Function called when the task has completed. Error code
	 * or 0 are passed as an argument.
	 */
	swim_task_f complete;
	/**
	 * Function, called when a scheduler is under destruction,
	 * and it cancels all its tasks.
	 */
	swim_task_f cancel;
	/** Destination address. */
	struct sockaddr_in dst;
	/**
	 * Optional proxy via which the destination should be
	 * reached.
	 */
	struct sockaddr_in proxy;
	/** Place in a queue of tasks. */
	struct rlist in_queue_output;
	/**
	 * A short description of the packet content. For logging.
	 */
	const char *desc;
	union {
		/**
		 * Receiver's UUID used by ping tasks to schedule
		 * deadline for an ACK.
		 */
		struct tt_uuid uuid;
		/**
		 * Alternative to UUID - direct pointer to the
		 * receiver member. It works, when members and
		 * tasks of a certain type are isomorphic. It is
		 * faster than lookup by UUID.
		 */
		struct swim_member *member;
	};
	/** Link in the task pool. */
	struct stailq_entry in_pool;
	/** Packet to send. */
	struct swim_packet packet;
};

/** Check if @a task is already scheduled. */
static inline bool
swim_task_is_scheduled(struct swim_task *task)
{
	return ! rlist_empty(&task->in_queue_output);
}

/**
 * Set the proxy for the task. Before sending this proxy is dumped
 * into metadata section.
 */
void
swim_task_set_proxy(struct swim_task *task, const struct sockaddr_in *proxy);

/**
 * Put the task into a queue of tasks. Eventually it will be sent.
 */
void
swim_task_send(struct swim_task *task, const struct sockaddr_in *dst,
	       struct swim_scheduler *scheduler);

/** Initialize the task, without scheduling. */
void
swim_task_create(struct swim_task *task, swim_task_f complete,
		 swim_task_f cancel, const char *desc);

/** Allocate and create a new task. */
struct swim_task *
swim_task_new(swim_task_f complete, swim_task_f cancel, const char *desc);

/** Destroy a task, free its memory. */
void
swim_task_delete(struct swim_task *task);

/** Callback to delete a task after its completion. */
void
swim_task_delete_cb(struct swim_task *task, struct swim_scheduler *scheduler,
		    int rc);

/** Destroy the task, pop from the queue. */
static inline void
swim_task_destroy(struct swim_task *task)
{
	rlist_del_entry(task, in_queue_output);
}

/**
 * Broadcast task. Besides usual task fields, stores a list of
 * interfaces available for broadcast packets. The task works
 * asynchronously just like its ancestor, because even broadcast
 * packets can not be sent without explicit permission from libev
 * in a form of EV_WRITE event.
 *
 * Despite usually having multiple network interfaces supporting
 * broadcast, there is only one task to send a packet to all of
 * them. The same task works multiple times, each time sending a
 * packet to one interface. After completion it is self-deleted.
 * There are no any concrete reason behind that except stint on
 * memory for multiple tasks.
 */
struct swim_bcast_task {
	/** Base structure. */
	struct swim_task base;
	/** Port to use for broadcast, in network byte order. */
	int port;
	/** A list of interfaces. */
	struct ifaddrs *addrs;
	/** A next interface to send to. */
	struct ifaddrs *i;
};

/**
 * Create a new broadcast task with a specified port. The port is
 * expected to have host byte order.
 */
struct swim_bcast_task *
swim_bcast_task_new(int port, const char *desc);

/**
 * A wrapper around sio_strfaddr() so as to do not clog SWIM
 * code with huge casts to 'struct sockaddr *' and passes of
 * sizeof(struct sockaddr_in).
 */
const char *
swim_inaddr_str(const struct sockaddr_in *addr);

#endif /* TARANTOOL_SWIM_IO_H_INCLUDED */
