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

/** Put the task into the queue of output tasks. */
static inline void
swim_task_schedule(struct swim_task *task, struct swim_scheduler *scheduler)
{
	assert(rlist_empty(&task->in_queue_output));
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
	swim_ev_io_stop(loop(), &scheduler->input);
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
	say_verbose("SWIM %d: send %s to %s", swim_scheduler_fd(scheduler),
		    task->desc, sio_strfaddr((struct sockaddr *) &task->dst,
					     sizeof(task->dst)));
	struct swim_meta_header_bin header;
	swim_meta_header_bin_create(&header, &scheduler->transport.addr);
	memcpy(task->packet.meta, &header, sizeof(header));
	int rc = swim_transport_send(&scheduler->transport, task->packet.buf,
				     task->packet.pos - task->packet.buf,
				     (const struct sockaddr *) &task->dst,
				     sizeof(task->dst));
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
		    sio_strfaddr((struct sockaddr *) &src, len));
	struct swim_meta_def meta;
	const char *pos = buf, *end = pos + size;
	if (swim_meta_def_decode(&meta, &pos, end) < 0)
		goto error;
	scheduler->on_input(scheduler, pos, end, &meta.src);
	return;
error:
	diag_log();
}
