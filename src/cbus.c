/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "cbus.h"

const char *cbus_stat_strings[CBUS_STAT_LAST] = {
	"EVENTS",
	"LOCKS",
};

static void
cbus_flush_cb(ev_loop * /* loop */, struct ev_async *watcher,
	      int /* events */);

static void
cpipe_fetch_output_cb(ev_loop *loop, struct ev_async *watcher,
			int events)
{
	(void) loop;
	(void) events;
	struct cpipe *pipe = (struct cpipe *) watcher->data;
	struct cmsg *msg;
	/* Force an exchange if there is nothing to do. */
	while (cpipe_peek(pipe)) {
		while ((msg = cpipe_pop_output(pipe)))
			cmsg_deliver(msg);
	}
}

void
cpipe_create(struct cpipe *pipe)
{
	stailq_create(&pipe->pipe);
	stailq_create(&pipe->input);
	stailq_create(&pipe->output);

	pipe->n_input = 0;
	pipe->max_input = INT_MAX;

	ev_async_init(&pipe->flush_input, cbus_flush_cb);
	pipe->flush_input.data = pipe;

	ev_async_init(&pipe->fetch_output, cpipe_fetch_output_cb);
	pipe->fetch_output.data = pipe;
	/* Set in join(), which is always called by the consumer thread. */
	pipe->consumer = NULL;
	/* Set in join() under a mutex. */
	pipe->producer = NULL;
}

void
cpipe_destroy(struct cpipe *pipe)
{
	(void) pipe;
}

static void
cpipe_join(struct cpipe *pipe, struct cbus *bus, struct cpipe *peer)
{
	assert(loop() == pipe->consumer);
	pipe->bus = bus;
	pipe->peer = peer;
	pipe->producer = peer->consumer;
}

void
cbus_create(struct cbus *bus)
{
	bus->pipe[0] = bus->pipe[1] = NULL;
	bus->stats = rmean_new(cbus_stat_strings, CBUS_STAT_LAST);
	if (bus->stats == NULL)
		panic_syserror("cbus_create");

	pthread_mutexattr_t errorcheck;

	(void) tt_pthread_mutexattr_init(&errorcheck);

#ifndef NDEBUG
	(void) tt_pthread_mutexattr_settype(&errorcheck,
					    PTHREAD_MUTEX_ERRORCHECK);
#endif
	/* Initialize queue lock mutex. */
	(void) tt_pthread_mutex_init(&bus->mutex, &errorcheck);
	(void) tt_pthread_mutexattr_destroy(&errorcheck);

	(void) tt_pthread_cond_init(&bus->cond, NULL);
}

void
cbus_destroy(struct cbus *bus)
{
	(void) tt_pthread_mutex_destroy(&bus->mutex);
	(void) tt_pthread_cond_destroy(&bus->cond);
	rmean_delete(bus->stats);
}

/**
 * @pre both consumers initialized their pipes
 * @post each consumers gets the input end of the opposite pipe
 */
struct cpipe *
cbus_join(struct cbus *bus, struct cpipe *pipe)
{
	pipe->consumer = loop();
	ev_async_start(pipe->consumer, &pipe->fetch_output);
	/*
	 * We can't let one or the other thread go off and
	 * produce events/send ev_async callback messages
	 * until the peer thread has initialized the async
	 * and started it.
	 * Use a condition variable to make sure that two
	 * threads operate in sync.
	 */
	cbus_lock(bus);
	int pipe_idx = bus->pipe[0] != NULL;
	int peer_idx = !pipe_idx;
	bus->pipe[pipe_idx] = pipe;
	while (bus->pipe[peer_idx] == NULL)
		cbus_wait_signal(bus);

	cpipe_join(pipe, bus, bus->pipe[peer_idx]);
	cbus_signal(bus);
	/*
	 * At this point we've have both pipes initialized
	 * in bus->pipe array, and our pipe joined.
	 * But the other pipe may have not been joined
	 * yet, ensure it's fully initialized before return.
	 */
	while (bus->pipe[peer_idx]->producer == NULL) {
		/* Let the other side wakeup and perform the join. */
		cbus_wait_signal(bus);
	}
	cbus_unlock(bus);
	/*
	 * POSIX: pthread_cond_signal() function shall
	 * have no effect if there are no threads currently
	 * blocked on cond.
	 */
	cbus_signal(bus);
	return bus->pipe[peer_idx];
}

void
cbus_leave(struct cbus *bus)
{
	struct cpipe *pipe = bus->pipe[bus->pipe[0]->consumer != cord()->loop];

	assert(loop() == pipe->consumer);
	ev_async_stop(pipe->consumer, &pipe->fetch_output);
}

static void
cbus_flush_cb(ev_loop *loop, struct ev_async *watcher,
	      int events)
{
	(void) loop;
	(void) events;
	struct cpipe *pipe = (struct cpipe *) watcher->data;
	if (pipe->n_input == 0)
		return;
	struct cpipe *peer = pipe->peer;
	assert(pipe->producer == loop());
	assert(peer->consumer == loop());

	/* Trigger task processing when the queue becomes non-empty. */
	bool pipe_was_empty;
	bool peer_output_was_empty = stailq_empty(&peer->output);

	cbus_lock(pipe->bus);
	pipe_was_empty = !ev_async_pending(&pipe->fetch_output);
	/** Flush input */
	stailq_concat(&pipe->pipe, &pipe->input);
	/*
	 * While at it, pop output.
	 * The consumer of the output of the bound queue is the
	 * same as the producer of input, so we can safely access it.
	 * We can safely access queue because it's locked.
	 */
	stailq_concat(&peer->output, &peer->pipe);
	cbus_unlock(pipe->bus);


	pipe->n_input = 0;
	if (pipe_was_empty) {
		/* Count statistics */
		rmean_collect(pipe->bus->stats, CBUS_STAT_EVENTS, 1);

		ev_async_send(pipe->consumer, &pipe->fetch_output);
	}
	if (peer_output_was_empty && !stailq_empty(&peer->output))
		ev_feed_event(peer->consumer, &peer->fetch_output, EV_CUSTOM);
}

struct cmsg *
cpipe_peek_impl(struct cpipe *pipe)
{
	assert(stailq_empty(&pipe->output));

	struct cpipe *peer = pipe->peer;
	assert(pipe->consumer == loop());
	assert(peer->producer == loop());

	bool peer_pipe_was_empty = false;


	cbus_lock(pipe->bus);
	stailq_concat(&pipe->output, &pipe->pipe);
	if (! stailq_empty(&peer->input)) {
		peer_pipe_was_empty = !ev_async_pending(&peer->fetch_output);
		stailq_concat(&peer->pipe, &peer->input);
	}
	cbus_unlock(pipe->bus);
	peer->n_input = 0;

	if (peer_pipe_was_empty) {
		/* Count statistics */
		rmean_collect(pipe->bus->stats, CBUS_STAT_EVENTS, 1);

		ev_async_send(peer->consumer, &peer->fetch_output);
	}
	return stailq_first_entry(&pipe->output, struct cmsg, fifo);
}


static void
cmsg_notify_deliver(struct cmsg *msg)
{
	fiber_wakeup(((struct cmsg_notify *) msg)->fiber);
}

void
cmsg_notify_init(struct cmsg_notify *msg)
{
	static struct cmsg_hop route[] = { { cmsg_notify_deliver, NULL }, };

	cmsg_init(&msg->base, route);
	msg->fiber = fiber();
}


/** Return true if there are too many active workers in the pool. */
static inline bool
cpipe_fiber_pool_needs_throttling(struct cpipe_fiber_pool *pool)
{
	return pool->size > pool->max_size;
}

/**
 * Main function of the fiber invoked to handle all outstanding
 * tasks in a queue.
 */
static void
cpipe_fiber_pool_f(va_list ap)
{
	struct cpipe_fiber_pool *pool = va_arg(ap, struct cpipe_fiber_pool *);
	struct cpipe *pipe = pool->pipe;
	struct cmsg *msg;
	pool->size++;
restart:
	while ((msg = cpipe_pop_output(pipe)))
		cmsg_deliver(msg);

	if (pool->cache_size < 2 * pool->max_size) {
		/** Put the current fiber into a fiber cache. */
		rlist_add_entry(&pool->fiber_cache, fiber(), state);
		pool->size--;
		pool->cache_size++;
		bool timed_out = fiber_yield_timeout(pool->idle_timeout);
		pool->cache_size--;
		pool->size++;
		if (! timed_out)
			goto restart;
	}
	pool->size--;
}


/** Create fibers to handle all outstanding tasks. */
static void
cpipe_fiber_pool_cb(ev_loop *loop, struct ev_async *watcher,
		  int events)
{
	(void) loop;
	(void) events;
	struct cpipe_fiber_pool *pool =
		(struct cpipe_fiber_pool *) watcher->data;
	struct cpipe *pipe = pool->pipe;
	(void) cpipe_peek(pipe);
	while (! stailq_empty(&pipe->output)) {
		struct fiber *f;
		if (! rlist_empty(&pool->fiber_cache)) {
			f = rlist_shift_entry(&pool->fiber_cache,
					      struct fiber, state);
			fiber_call(f);
		} else if (! cpipe_fiber_pool_needs_throttling(pool)) {
			f = fiber_new(pool->name, cpipe_fiber_pool_f);
			if (f == NULL) {
				error_log(diag_last_error(&fiber()->diag));
				break;
			}
			fiber_start(f, pool);
		} else {
			/**
			 * No worries that this watcher may not
			 * get scheduled again - there are enough
			 * worker fibers already, so just leave.
			 */
			break;
		}
	}
}

void
cpipe_fiber_pool_create(struct cpipe_fiber_pool *pool,
			const char *name, struct cpipe *pipe,
			int max_pool_size, float idle_timeout)
{
	rlist_create(&pool->fiber_cache);
	pool->name = name;
	pool->pipe = pipe;
	pool->size = 0;
	pool->cache_size = 0;
	pool->max_size = max_pool_size;
	pool->idle_timeout = idle_timeout;
	cpipe_set_fetch_cb(pipe, cpipe_fiber_pool_cb, pool);
}
