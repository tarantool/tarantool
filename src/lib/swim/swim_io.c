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
	memset(task, 0, sizeof(*task));
	task->complete = complete;
	task->cancel = cancel;
	task->desc = desc;
	swim_packet_create(&task->packet);
	rlist_create(&task->in_queue_output);
}

struct swim_task *
swim_task_new(swim_task_f complete, swim_task_f cancel, const char *desc)
{
	struct swim_task *task = (struct swim_task *) malloc(sizeof(*task));
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "malloc", "task");
		return NULL;
	}
	swim_task_create(task, complete, cancel, desc);
	return task;
}

void
swim_task_delete_cb(struct swim_task *task, struct swim_scheduler *scheduler,
		    int rc)
{
	(void) rc;
	(void) scheduler;
	free(task);
}

/** Put the task into the queue of output tasks. */
static inline void
swim_task_schedule(struct swim_task *task, struct swim_scheduler *scheduler)
{
	assert(! swim_task_is_scheduled(task));
	rlist_add_tail_entry(&scheduler->queue_output, task, in_queue_output);
	swim_ev_io_start(loop(), &scheduler->output);
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

		if ((flags & IFF_BROADCAST) != 0 &&
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

/**
 * Dispatch a next output event. Build packet meta and send the
 * packet.
 */
static void
swim_scheduler_on_output(struct ev_loop *loop, struct ev_io *io, int events);

/**
 * Dispatch a next input event. Unpack meta, forward a packet or
 * propagate further to protocol logic.
 */
static void
swim_scheduler_on_input(struct ev_loop *loop, struct ev_io *io, int events);

void
swim_scheduler_create(struct swim_scheduler *scheduler,
		      swim_scheduler_on_input_f on_input)
{
	swim_ev_init(&scheduler->output, swim_scheduler_on_output);
	scheduler->output.data = (void *) scheduler;
	swim_ev_init(&scheduler->input, swim_scheduler_on_input);
	scheduler->input.data = (void *) scheduler;
	rlist_create(&scheduler->queue_output);
	scheduler->on_input = on_input;
	swim_transport_create(&scheduler->transport);
}

int
swim_scheduler_bind(struct swim_scheduler *scheduler,
		    const struct sockaddr_in *addr)
{
	struct swim_transport *t = &scheduler->transport;
	if (swim_transport_bind(t, (const struct sockaddr *) addr,
				sizeof(*addr)) != 0)
		return -1;
	swim_ev_io_set(&scheduler->output, t->fd, EV_WRITE);
	swim_ev_io_set(&scheduler->input, t->fd, EV_READ);
	swim_ev_io_start(loop(), &scheduler->input);
	swim_ev_io_start(loop(), &scheduler->output);
	return 0;
}

void
swim_scheduler_stop_input(struct swim_scheduler *scheduler)
{
	swim_ev_io_stop(loop(), &scheduler->input);
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
	swim_ev_io_stop(loop(), &scheduler->output);
	swim_scheduler_stop_input(scheduler);
}

static void
swim_scheduler_on_output(struct ev_loop *loop, struct ev_io *io, int events)
{
	assert((events & EV_WRITE) != 0);
	(void) events;
	struct swim_scheduler *scheduler = (struct swim_scheduler *) io->data;
	if (rlist_empty(&scheduler->queue_output)) {
		/*
		 * Possible, if a member pushed a task and then
		 * was deleted together with it.
		 */
		swim_ev_io_stop(loop, io);
		return;
	}
	struct swim_task *task =
		rlist_shift_entry(&scheduler->queue_output, struct swim_task,
				  in_queue_output);
	const struct sockaddr_in *src = &scheduler->transport.addr;
	const struct sockaddr_in *dst = &task->dst;
	const char *dst_str = swim_inaddr_str(dst);
	if (! swim_inaddr_is_empty(&task->proxy)) {
		dst = &task->proxy;
		dst_str = tt_sprintf("%s via %s", dst_str,
				     swim_inaddr_str(dst));
		swim_packet_build_meta(&task->packet, src, src, &task->dst);
	} else {
		swim_packet_build_meta(&task->packet, src, NULL, NULL);
	}
	say_verbose("SWIM %d: send %s to %s", swim_scheduler_fd(scheduler),
		    task->desc, dst_str);
	int rc = swim_transport_send(&scheduler->transport, task->packet.buf,
				     task->packet.pos - task->packet.buf,
				     (const struct sockaddr *) dst,
				     sizeof(*dst));
	if (rc < 0)
		diag_log();
	if (task->complete != NULL)
		task->complete(task, scheduler, rc);
}

static void
swim_scheduler_on_input(struct ev_loop *loop, struct ev_io *io, int events)
{
	assert((events & EV_READ) != 0);
	(void) events;
	(void) loop;
	struct swim_scheduler *scheduler = (struct swim_scheduler *) io->data;
	struct sockaddr_in src;
	socklen_t len = sizeof(src);
	char buf[UDP_PACKET_SIZE];
	ssize_t size = swim_transport_recv(&scheduler->transport, buf,
					   sizeof(buf),
					   (struct sockaddr *) &src, &len);
	if (size <= 0) {
		if (size < 0)
			goto error;
		return;
	}
	say_verbose("SWIM %d: received from %s", swim_scheduler_fd(scheduler),
		    swim_inaddr_str(&src));
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

const char *
swim_inaddr_str(const struct sockaddr_in *addr)
{
	return sio_strfaddr((struct sockaddr *) addr, sizeof(*addr));
}
