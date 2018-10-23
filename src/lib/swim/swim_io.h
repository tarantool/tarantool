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
#include "trivia/util.h"
#include "small/rlist.h"
#include "salad/stailq.h"
#include "swim_transport.h"
#include "tarantool_ev.h"
#include <stdbool.h>
#include <arpa/inet.h>

/**
 * SWIM protocol transport level.
 */

struct swim_task;
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
};

/**
 * UDP packet. Works as an allocator, allowing to fill its body
 * gradually, while preserving prefix for metadata.
 *
 *          < - - - -UDP_PACKET_SIZE- - - - ->
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
	char buf[UDP_PACKET_SIZE];
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

/** Initialize @a packet, reserve some space for meta. */
void
swim_packet_create(struct swim_packet *packet);

typedef void (*swim_scheduler_on_input_f)(struct swim_scheduler *scheduler,
					  const char *buf, const char *end,
					  const struct sockaddr_in *src);

/** Planner and executor of input and output operations.*/
struct swim_scheduler {
	/** Transport to send/receive packets. */
	struct swim_transport transport;
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
	/** Packet to send. */
	struct swim_packet packet;
	/** Destination address. */
	struct sockaddr_in dst;
	/** Place in a queue of tasks. */
	struct rlist in_queue_output;
};

/**
 * Put the task into a queue of tasks. Eventually it will be sent.
 */
void
swim_task_send(struct swim_task *task, const struct sockaddr_in *dst,
	       struct swim_scheduler *scheduler);

/** Initialize the task, without scheduling. */
void
swim_task_create(struct swim_task *task, swim_task_f complete,
		 swim_task_f cancel);

/** Destroy the task, pop from the queue. */
static inline void
swim_task_destroy(struct swim_task *task)
{
	rlist_del_entry(task, in_queue_output);
}

#endif /* TARANTOOL_SWIM_IO_H_INCLUDED */