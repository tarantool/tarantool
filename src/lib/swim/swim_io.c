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
#include "swim_io.h"
#include "swim_proto.h"
#include "swim_ev.h"
#include "fiber.h"
#include "sio.h"
#include <ifaddrs.h>
#include <net/if.h>

enum {
	/**
	 * A rough estimation of how many tasks a SWIM instance
	 * would need simultaneously. 1 for an ACK, 2 for indirect
	 * ping, 1 for direct ping. Total is 4 for normal
	 * operation. Others are 1) to get a beautiful number,
	 * 2) in case random() is not perfect and this instance
	 * interacts with 2 and more other instances during one
	 * round.
	 */
	TASKS_PER_SCHEDULER = 16,
};

/**
 * All the SWIM instances and their members use the same objects
 * to send data - tasks. Each task is ~1.5KB, and on one hand it
 * would be a waste of memory to keep preallocated tasks for each
 * member. One the other hand it would be too slow to allocate
 * and delete ~1.5KB on each interaction, ~3KB on each round step.
 * Here is a pool of free tasks shared among all SWIM instances
 * to avoid allocations, but do not keep a separate task for each
 * member.
 */
static struct stailq swim_task_pool;
/** Number of pooled tasks. */
static int swim_task_pool_size = 0;
/**
 * Number of currently active schedulers. Used to limit max size
 * of the pool.
 */
static int scheduler_count = 0;

/** First scheduler should create the pool. */
static inline void
swim_task_pool_create(void)
{
	assert(scheduler_count == 1);
	assert(swim_task_pool_size == 0);
	stailq_create(&swim_task_pool);
}

/** Last scheduler destroys the pool. */
static inline void
swim_task_pool_destroy(void)
{
	assert(scheduler_count == 0);
	while (! stailq_empty(&swim_task_pool)) {
		free(stailq_shift_entry(&swim_task_pool, struct swim_task,
					in_pool));
	}
	swim_task_pool_size = 0;
}

/**
 * Allocate memory for meta. The same as mere alloc, but moves
 * body pointer.
 */
static inline void
swim_packet_alloc_meta(struct swim_packet *packet, int size)
{
	char *tmp = swim_packet_alloc(packet, size);
	assert(tmp != NULL);
	(void) tmp;
	packet->body = packet->pos;
}

void
swim_packet_create(struct swim_packet *packet)
{
	packet->body = packet->buf;
	packet->pos = packet->body;
	swim_packet_alloc_meta(packet, sizeof(struct swim_meta_header_bin));
}

/** Fill metadata prefix of a packet. */
static inline void
swim_packet_build_meta(struct swim_packet *packet,
		       const struct sockaddr_in *src,
		       const struct sockaddr_in *route_src,
		       const struct sockaddr_in *route_dst)
{
	assert((route_src != NULL) == (route_dst != NULL));
	char *meta = packet->meta;
	char *end = packet->body;
	/*
	 * Meta has already been built. It happens when the same
	 * task is resent multiple times.
	 */
	if (meta == end)
		return;
	struct swim_meta_header_bin header;
	swim_meta_header_bin_create(&header, src, route_dst != NULL);
	assert(meta + sizeof(header) <= end);
	memcpy(meta, &header, sizeof(header));
	meta += sizeof(header);
	if (route_dst != NULL) {
		struct swim_route_bin route;
		swim_route_bin_create(&route, route_src, route_dst);
		assert(meta + sizeof(route) <= end);
		memcpy(meta, &route, sizeof(route));
		meta += sizeof(route);
	}
	assert(meta == end);
	/*
	 * Once meta is built, it is consumed by the body. Used
	 * not to rebuild the meta again if the task will be
	 * scheduled again without changes in data.
	 */
	packet->body = packet->meta;
}

void
swim_task_set_proxy(struct swim_task *task, const struct sockaddr_in *proxy)
{
	/*
	 * Route meta should be reserved before body encoding is
	 * started. Otherwise later it would be necessary to move
	 * already encoded body, maybe having its tail trimmed off
	 * because of limited UDP packet size.
	 */
	assert(swim_packet_body_size(&task->packet) == 0);
	assert(! swim_inaddr_is_empty(proxy));
	task->proxy = *proxy;
	swim_packet_alloc_meta(&task->packet, sizeof(struct swim_route_bin));
}

void
swim_task_create(struct swim_task *task, swim_task_f complete,
		 swim_task_f cancel, const char *desc)
{
	/* Do not nullify the whole structure! It is too big. */
	memset(task, 0, offsetof(struct swim_task, packet));
	task->complete = complete;
	task->cancel = cancel;
	task->desc = desc;
	swim_packet_create(&task->packet);
	rlist_create(&task->in_queue_output);
}

struct swim_task *
swim_task_new(swim_task_f complete, swim_task_f cancel, const char *desc)
{
	struct swim_task *task;
	if (swim_task_pool_size > 0) {
		assert(! stailq_empty(&swim_task_pool));
		--swim_task_pool_size;
		task = stailq_shift_entry(&swim_task_pool, struct swim_task,
					  in_pool);
	} else {
		task = (struct swim_task *) malloc(sizeof(*task));
		if (task == NULL) {
			diag_set(OutOfMemory, sizeof(*task), "malloc", "task");
			return NULL;
		}
	}
	swim_task_create(task, complete, cancel, desc);
	return task;
}

void
swim_task_delete(struct swim_task *task)
{
	swim_task_destroy(task);
	if (swim_task_pool_size < TASKS_PER_SCHEDULER * scheduler_count) {
		stailq_add_entry(&swim_task_pool, task, in_pool);
		++swim_task_pool_size;
	} else {
		free(task);
	}
}

void
swim_task_delete_cb(struct swim_task *task, struct swim_scheduler *scheduler,
		    int rc)
{
	(void) rc;
	(void) scheduler;
	swim_task_delete(task);
}

/** Put the task into the queue of output tasks. */
static inline void
swim_task_schedule(struct swim_task *task, struct swim_scheduler *scheduler)
{
	assert(! swim_task_is_scheduled(task));
	rlist_add_tail_entry(&scheduler->queue_output, task, in_queue_output);
	swim_ev_io_start(swim_loop(), &scheduler->output);
}

void
swim_task_send(struct swim_task *task, const struct sockaddr_in *dst,
	       struct swim_scheduler *scheduler)
{
	task->dst = *dst;
	swim_task_schedule(task, scheduler);
}

/** Delete a broadcast task. */
static void
swim_bcast_task_delete(struct swim_bcast_task *task)
{
	swim_freeifaddrs(task->addrs);
	swim_task_destroy(&task->base);
	free(task);
}

/** Delete broadcast task on its cancelation. */
static void
swim_bcast_task_delete_cb(struct swim_task *task,
			  struct swim_scheduler *scheduler, int rc)
{
	(void) scheduler;
	(void) rc;
	swim_bcast_task_delete((struct swim_bcast_task *) task);
}

/**
 * Write down a next available broadcast address into the task
 * destination field.
 * @param task Broadcast task to update.
 * @retval 0 Success. @a dst field is updated.
 * @retval -1 No more addresses.
 */
static int
swim_bcast_task_next_addr(struct swim_bcast_task *task)
{
	/*
	 * Broadcast + proxy is not supported yet, and barely it
	 * will be needed anytime.
	 */
	assert(swim_inaddr_is_empty(&task->base.proxy));
	for (struct ifaddrs *i = task->i; i != NULL; i = i->ifa_next) {
		int flags = i->ifa_flags;
		if ((flags & IFF_UP) == 0)
			continue;

		if ((flags & IFF_BROADCAST) != 0 && i->ifa_broadaddr != NULL &&
		    i->ifa_broadaddr->sa_family == AF_INET) {
			task->base.dst =
				*(struct sockaddr_in *) i->ifa_broadaddr;
		} else if (i->ifa_addr != NULL &&
			   i->ifa_addr->sa_family == AF_INET) {
			task->base.dst = *(struct sockaddr_in *) i->ifa_addr;
		} else {
			continue;
		}
		task->base.dst.sin_port = task->port;
		task->i = task->i->ifa_next;
		return 0;
	}
	return -1;
}

/**
 * Callback on send completion. If there are more broadcast
 * addresses to use, then the task is rescheduled.
 */
static void
swim_bcast_task_complete(struct swim_task *base_task,
			 struct swim_scheduler *scheduler, int rc)
{
	(void) rc;
	struct swim_bcast_task *task = (struct swim_bcast_task *) base_task;
	if (swim_bcast_task_next_addr(task) != 0)
		swim_bcast_task_delete(task);
	else
		swim_task_schedule(base_task, scheduler);
}

struct swim_bcast_task *
swim_bcast_task_new(int port, const char *desc)
{
	struct swim_bcast_task *task =
		(struct swim_bcast_task *) malloc(sizeof(*task));
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "malloc", "task");
		return NULL;
	}
	struct ifaddrs *addrs;
	if (swim_getifaddrs(&addrs) != 0) {
		free(task);
		return NULL;
	}
	task->port = htons(port);
	task->addrs = addrs;
	task->i = addrs;
	swim_task_create(&task->base, swim_bcast_task_complete,
			 swim_bcast_task_delete_cb, desc);
	if (swim_bcast_task_next_addr(task) != 0) {
		diag_set(SwimError, "broadcast has failed - no available "\
			 "interfaces");
		swim_bcast_task_delete(task);
		return NULL;
	}
	return task;
}

/**
 * Scheduler fd mainly is needed to be printed into the logs in
 * order to distinguish between different SWIM instances logs.
 */
static inline int
swim_scheduler_fd(const struct swim_scheduler *scheduler)
{
	return scheduler->transport.fd;
}

void
swim_scheduler_create(struct swim_scheduler *scheduler,
		      swim_scheduler_on_input_f on_input)
{
	scheduler->output.data = (void *) scheduler;
	scheduler->input.data = (void *) scheduler;
	rlist_create(&scheduler->queue_output);
	scheduler->on_input = on_input;
	swim_transport_create(&scheduler->transport);
	scheduler->codec = NULL;
	int rc = swim_scheduler_set_codec(scheduler, CRYPTO_ALGO_NONE,
					  CRYPTO_MODE_ECB, NULL, 0);
	assert(rc == 0);
	(void) rc;
	if (++scheduler_count == 1)
		swim_task_pool_create();
}

int
swim_scheduler_bind(struct swim_scheduler *scheduler,
		    const struct sockaddr_in *addr)
{
	struct ev_loop *l = swim_loop();
	swim_ev_io_stop(l, &scheduler->input);
	swim_ev_io_stop(l, &scheduler->output);
	struct swim_transport *t = &scheduler->transport;
	int rc = swim_transport_bind(t, (const struct sockaddr *) addr,
				     sizeof(*addr));
	if (t->fd >= 0) {
		swim_ev_io_set(&scheduler->output, t->fd, EV_WRITE);
		swim_ev_io_set(&scheduler->input, t->fd, EV_READ);
		swim_ev_io_start(l, &scheduler->input);
		swim_ev_io_start(l, &scheduler->output);
	}
	return rc;
}

void
swim_scheduler_stop_input(struct swim_scheduler *scheduler)
{
	swim_ev_io_stop(swim_loop(), &scheduler->input);
}

void
swim_scheduler_destroy(struct swim_scheduler *scheduler)
{
	struct swim_task *t, *tmp;
	/*
	 * Use 'safe', because cancelation can delete the task
	 * from the queue, or even delete the task itself.
	 */
	rlist_foreach_entry_safe(t, &scheduler->queue_output, in_queue_output,
				 tmp) {
		if (t->cancel != NULL)
			t->cancel(t, scheduler, -1);
	}
	swim_transport_destroy(&scheduler->transport);
	swim_ev_io_stop(swim_loop(), &scheduler->output);
	swim_scheduler_stop_input(scheduler);
	if (scheduler->codec != NULL)
		crypto_codec_delete(scheduler->codec);
	assert(scheduler_count > 0);
	if (--scheduler_count == 0)
		swim_task_pool_destroy();
}

/**
 * Encrypt data and prepend it with a fresh crypto algorithm's
 * initial vector.
 */
static inline int
swim_encrypt(struct crypto_codec *c, const char *in, int in_size,
	     char *out, int out_size)
{
	assert(out_size >= crypto_codec_iv_size(c));
	int iv_size = crypto_codec_gen_iv(c, out, out_size);
	char *iv = out;
	out += iv_size;
	out_size -= iv_size;
	int rc = crypto_codec_encrypt(c, iv, in, in_size, out, out_size);
	if (rc < 0)
		return -1;
	return rc + iv_size;
}

/** Decrypt data prepended with an initial vector. */
static inline int
swim_decrypt(struct crypto_codec *c, const char *in, int in_size,
	     char *out, int out_size)
{
	int iv_size = crypto_codec_iv_size(c);
	if (in_size < iv_size) {
		diag_set(SwimError, "too small message, can't extract IV for "\
			 "decryption");
		return -1;
	}
	const char *iv = in;
	in += iv_size;
	in_size -= iv_size;
	return crypto_codec_decrypt(c, iv, in, in_size, out, out_size);
}

/**
 * Begin packet transmission. Prepare a next task in the queue to
 * send its packet: build a meta header, pop the task from the
 * queue.
 * @param scheduler Scheduler to pop a task from.
 * @param loop Event loop passed by libev.
 * @param io Descriptor to send to.
 * @param events Mask of happened events passed by libev.
 * @param[out] dst Destination address to send the packet to. Can
 *        be different from task.dst, for example, if task.proxy
 *        is specified.
 *
 * @retval NULL The queue is empty. Input has been stopped.
 * @retval not NULL A task ready to be sent.
 */
static struct swim_task *
swim_begin_send(struct swim_scheduler *scheduler, struct ev_loop *loop,
		struct ev_io *io, int events, const struct sockaddr_in **dst)
{
	assert((events & EV_WRITE) != 0);
	(void) events;
	if (rlist_empty(&scheduler->queue_output)) {
		/*
		 * Possible, if a member pushed a task and then
		 * was deleted together with it.
		 */
		swim_ev_io_stop(loop, io);
		return NULL;
	}
	struct swim_task *task =
		rlist_shift_entry(&scheduler->queue_output, struct swim_task,
				  in_queue_output);
	const struct sockaddr_in *src = &scheduler->transport.addr;
	const char *dst_str = swim_inaddr_str(&task->dst);
	if (! swim_inaddr_is_empty(&task->proxy)) {
		*dst = &task->proxy;
		dst_str = tt_sprintf("%s via %s", dst_str,
				     swim_inaddr_str(*dst));
		swim_packet_build_meta(&task->packet, src, src, &task->dst);
	} else {
		*dst = &task->dst;
		swim_packet_build_meta(&task->packet, src, NULL, NULL);
	}
	say_verbose("SWIM %d: send %s to %s", swim_scheduler_fd(scheduler),
		    task->desc, dst_str);
	return task;
}

/** Send a packet into the network. */
static inline ssize_t
swim_do_send(struct swim_scheduler *scheduler, const char *buf, int size,
	     const struct sockaddr_in *dst)
{
	return swim_transport_send(&scheduler->transport, buf, size,
				   (const struct sockaddr *) dst, sizeof(*dst));
}

/**
 * Finalize packet transmission, call the completion callback.
 * @param scheduler Scheduler owning @a task.
 * @param task Sent (or failed to be sent) task.
 * @param size Result of send().
 */
static inline void
swim_complete_send(struct swim_scheduler *scheduler, struct swim_task *task,
		   ssize_t size)
{
	if (size < 0) {
		bool is_critical = true;
		int err = diag_last_error(diag_get())->saved_errno;
#if TARGET_OS_DARWIN
		/*
		 * On Mac this error happens regularly if SWIM is bound to
		 * the localhost and tries to broadcast out of the machine. This
		 * is not critical, because will happen in the tests a lot, and
		 * in prod it simply should not bind to localhost if there are
		 * multiple machines in the cluster. Besides, Mac as a platform
		 * is not supposed to be used in prod.
		 */
		is_critical = (err != EADDRNOTAVAIL);
#else
		/* The same as EADDRNOTAVAIL, but happens on Linux as EINVAL. */
		is_critical = (err != EINVAL);
#endif
		if (is_critical)
			diag_log();
	}
	if (task->complete != NULL)
		task->complete(task, scheduler, size);
}

/**
 * On a new EV_WRITE event send a next packet from the queue
 * encrypted with the currently chosen algorithm.
 */
static void
swim_on_encrypted_output(struct ev_loop *loop, struct ev_io *io, int events)
{
	struct swim_scheduler *scheduler = (struct swim_scheduler *) io->data;
	const struct sockaddr_in *dst;
	struct swim_task *task = swim_begin_send(scheduler, loop, io, events,
						 &dst);
	if (task == NULL)
		return;
	char *buf = static_alloc(UDP_PACKET_SIZE);
	assert(buf != NULL);
	ssize_t size = swim_encrypt(scheduler->codec, task->packet.buf,
				    task->packet.pos - task->packet.buf,
				    buf, UDP_PACKET_SIZE);
	if (size > 0)
		size = swim_do_send(scheduler, buf, size, dst);
	swim_complete_send(scheduler, task, size);
}

/**
 * On a new EV_WRITE event send a next packet without encryption.
 */
static void
swim_on_plain_output(struct ev_loop *loop, struct ev_io *io, int events)
{
	struct swim_scheduler *scheduler = (struct swim_scheduler *) io->data;
	const struct sockaddr_in *dst;
	struct swim_task *task = swim_begin_send(scheduler, loop, io, events,
						 &dst);
	if (task == NULL)
		return;
	ssize_t size = swim_do_send(scheduler, task->packet.buf,
				    task->packet.pos - task->packet.buf, dst);
	swim_complete_send(scheduler, task, size);
}

/**
 * Begin packet receipt. Note, this function is no-op, and exists
 * just for consistency with begin/do/complete_send() functions.
 */
static inline void
swim_begin_recv(struct swim_scheduler *scheduler, struct ev_loop *loop,
		struct ev_io *io, int events)
{
	assert((events & EV_READ) != 0);
	(void) io;
	(void) scheduler;
	(void) events;
	(void) loop;
}

/** Receive a packet from the network. */
static ssize_t
swim_do_recv(struct swim_scheduler *scheduler, char *buf, int size)
{
	struct sockaddr_in src;
	socklen_t len = sizeof(src);
	ssize_t rc = swim_transport_recv(&scheduler->transport, buf, size,
					 (struct sockaddr *) &src, &len);
	if (rc <= 0)
		return rc;
	say_verbose("SWIM %d: received from %s", swim_scheduler_fd(scheduler),
		    swim_inaddr_str(&src));
	return rc;
}

/**
 * Finalize packet receipt, call the SWIM core callbacks, or
 * forward the packet to a next node.
 */
static void
swim_complete_recv(struct swim_scheduler *scheduler, const char *buf,
		   ssize_t size)
{
	if (size < 0)
		goto error;
	if (size == 0)
		return;
	struct swim_meta_def meta;
	const char *pos = buf, *end = pos + size;
	if (swim_meta_def_decode(&meta, &pos, end) < 0)
		goto error;
	/*
	 * Check if this instance is not the destination and
	 * possibly forward the packet.
	 */
	if (! meta.is_route_specified) {
		scheduler->on_input(scheduler, pos, end, &meta.src, NULL);
		return;
	}
	struct sockaddr_in *self = &scheduler->transport.addr;
	if (swim_inaddr_eq(&meta.route.dst, self)) {
		scheduler->on_input(scheduler, pos, end, &meta.route.src,
				    &meta.src);
		return;
	}
	/* Forward the foreign packet. */
	struct swim_task *task = swim_task_new(swim_task_delete_cb,
					       swim_task_delete_cb,
					       "foreign packet");
	if (task == NULL)
		goto error;
	/*
	 * Allocate route meta explicitly, because the packet
	 * should keep route meta even being sent to the final
	 * destination directly.
	 */
	swim_packet_alloc_meta(&task->packet, sizeof(struct swim_route_bin));
	/*
	 * Meta should be rebuilt with the different source
	 * address - this instance. It is used by the receiver to
	 * send a reply through this instance again.
	 */
	swim_packet_build_meta(&task->packet, self, &meta.route.src,
			       &meta.route.dst);
	/* Copy the original body without a touch. */
	size = end - pos;
	char *body = swim_packet_alloc(&task->packet, size);
	assert(body != NULL);
	memcpy(body, pos, size);
	swim_task_send(task, &meta.route.dst, scheduler);
	return;
error:
	diag_log();
}

/**
 * On a new EV_READ event receive an encrypted packet from the
 * network.
 */
static void
swim_on_encrypted_input(struct ev_loop *loop, struct ev_io *io, int events)
{
	struct swim_scheduler *scheduler = (struct swim_scheduler *) io->data;
	/*
	 * Buffer for decrypted data is on stack, not on static
	 * memory, because the SWIM code uses static memory as
	 * well and can accidentally rewrite the packet data.
	 */
	char buf[UDP_PACKET_SIZE];
	swim_begin_recv(scheduler, loop, io, events);

	char *ibuf = static_alloc(UDP_PACKET_SIZE);
	assert(ibuf != NULL);
	ssize_t size = swim_do_recv(scheduler, ibuf, UDP_PACKET_SIZE);
	if (size > 0) {
		size = swim_decrypt(scheduler->codec, ibuf, size,
				    buf, UDP_PACKET_SIZE);
	}
	swim_complete_recv(scheduler, buf, size);
}

/** On a new EV_READ event receive a packet from the network. */
static void
swim_on_plain_input(struct ev_loop *loop, struct ev_io *io, int events)
{
	struct swim_scheduler *scheduler = (struct swim_scheduler *) io->data;
	char buf[UDP_PACKET_SIZE];
	swim_begin_recv(scheduler, loop, io, events);
	ssize_t size = swim_do_recv(scheduler, buf, UDP_PACKET_SIZE);
	swim_complete_recv(scheduler, buf, size);
}

int
swim_scheduler_set_codec(struct swim_scheduler *scheduler,
			 enum crypto_algo algo, enum crypto_mode mode,
			 const char *key, int key_size)
{
	if (algo == CRYPTO_ALGO_NONE) {
		if (scheduler->codec != NULL) {
			crypto_codec_delete(scheduler->codec);
			scheduler->codec = NULL;
		}
		swim_ev_set_cb(&scheduler->output, swim_on_plain_output);
		swim_ev_set_cb(&scheduler->input, swim_on_plain_input);
		return 0;
	}
	struct crypto_codec *newc = crypto_codec_new(algo, mode, key, key_size);
	if (newc == NULL)
		return -1;
	if (scheduler->codec != NULL)
		crypto_codec_delete(scheduler->codec);
	scheduler->codec = newc;
	swim_ev_set_cb(&scheduler->output, swim_on_encrypted_output);
	swim_ev_set_cb(&scheduler->input, swim_on_encrypted_input);
	return 0;
}

const char *
swim_inaddr_str(const struct sockaddr_in *addr)
{
	return sio_strfaddr((struct sockaddr *) addr, sizeof(*addr));
}
