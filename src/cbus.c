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

static inline void
cmsg_deliver(struct cmsg *msg);

static __thread struct fiber_pool fiber_pool;

/* {{{ fiber_pool */

enum { FIBER_POOL_SIZE = 4096, FIBER_POOL_IDLE_TIMEOUT = 3 };

static void
fiber_pool_fetch_output(struct fiber_pool *pool)
{
	tt_pthread_mutex_lock(&pool->mutex);
	stailq_concat(&pool->output, &pool->pipe);
	tt_pthread_mutex_unlock(&pool->mutex);
}

/**
 * Main function of the fiber invoked to handle all outstanding
 * tasks in a queue.
 */
static int
fiber_pool_f(va_list ap)
{
	struct fiber_pool *pool = va_arg(ap, struct fiber_pool *);
	struct stailq *output = &pool->output;
	struct ev_loop *loop = pool->consumer;
	struct ev_async *watcher = &pool->fetch_output;
	pool->size++;
restart:
	while (! stailq_empty(output))
		cmsg_deliver(stailq_shift_entry(output, struct cmsg, fifo));
	/* fiber_yield_timeout() is expensive, avoid under load. */
	if (ev_is_pending(watcher)) {
		(void) fiber_pool_fetch_output(pool);
		ev_clear_pending(loop, watcher);
		goto restart;
	}

	/** Put the current fiber into a fiber cache. */
	rlist_add_entry(&pool->idle, fiber(), state);
	bool timed_out = fiber_yield_timeout(pool->idle_timeout);
	if (! timed_out)
		goto restart;

	pool->size--;
	return 0;
}

/** Create fibers to handle all outstanding tasks. */
static void
fiber_pool_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct fiber_pool *pool = (struct fiber_pool *) watcher->data;
	fiber_pool_fetch_output(pool);

	struct stailq *output = &pool->output;
	while (! stailq_empty(output)) {
		struct fiber *f;
		if (! rlist_empty(&pool->idle)) {
			f = rlist_shift_entry(&pool->idle,
					      struct fiber, state);
			fiber_call(f);
		} else if (pool->size < pool->max_size) {
			f = fiber_new(cord_name(cord()), fiber_pool_f);
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
fiber_pool_create(struct fiber_pool *pool, int max_pool_size,
		  float idle_timeout)
{
	pool->consumer = loop();
	rlist_create(&pool->idle);
	pool->size = 0;
	pool->max_size = max_pool_size;
	pool->idle_timeout = idle_timeout;
	stailq_create(&pool->output);
	stailq_create(&pool->pipe);
	ev_async_init(&pool->fetch_output, fiber_pool_cb);
	pool->fetch_output.data = pool;
	ev_async_start(pool->consumer, &pool->fetch_output);

	pthread_mutexattr_t errorcheck;
	(void) tt_pthread_mutexattr_init(&errorcheck);
#ifndef NDEBUG
	(void) tt_pthread_mutexattr_settype(&errorcheck,
					    PTHREAD_MUTEX_ERRORCHECK);
#endif
	(void) tt_pthread_mutex_init(&pool->mutex, &errorcheck);
	(void) tt_pthread_mutexattr_destroy(&errorcheck);
}

void
fiber_pool_destroy(struct fiber_pool *pool)
{
	ev_async_stop(pool->consumer, &pool->fetch_output);
	(void) tt_pthread_mutex_destroy(&pool->mutex);
}

/** }}} fiber_pool */

static void
cpipe_flush_cb(ev_loop * /* loop */, struct ev_async *watcher,
	       int /* events */);

void
cpipe_create(struct cpipe *pipe)
{
	stailq_create(&pipe->input);

	pipe->n_input = 0;
	pipe->max_input = INT_MAX;

	ev_async_init(&pipe->flush_input, cpipe_flush_cb);
	pipe->flush_input.data = pipe;

	/* Set in join() under a mutex. */
	pipe->producer = NULL;
	pipe->bus = NULL;
	pipe->pool = NULL;
	cpipe_set_max_input(pipe, 2 * FIBER_POOL_SIZE);
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
	pipe->pool = &fiber_pool;
	if (pipe->pool->max_size == 0) {
		fiber_pool_create(pipe->pool, FIBER_POOL_SIZE,
				  FIBER_POOL_IDLE_TIMEOUT);
	}
	/*
	 * We can't let one or the other thread go off and
	 * produce events/send ev_async callback messages
	 * until the peer thread has initialized the async
	 * and started it.
	 * Use a condition variable to make sure that two
	 * threads operate in sync.
	 */
	tt_pthread_mutex_lock(&bus->mutex);
	int pipe_idx = bus->pipe[0] != NULL;
	int peer_idx = !pipe_idx;
	bus->pipe[pipe_idx] = pipe;
	while (bus->pipe[peer_idx] == NULL)
		tt_pthread_cond_wait(&bus->cond, &bus->mutex);

	pipe->bus = bus;
	pipe->producer = bus->pipe[peer_idx]->pool->consumer;
	tt_pthread_cond_signal(&bus->cond);
	/*
	 * At this point we've have both pipes initialized
	 * in bus->pipe array, and our pipe joined.
	 * But the other pipe may have not been joined
	 * yet, ensure it's fully initialized before return.
	 */
	while (bus->pipe[peer_idx]->bus == NULL) {
		/* Let the other side wakeup and perform the join. */
		tt_pthread_cond_wait(&bus->cond, &bus->mutex);
	}
	tt_pthread_mutex_unlock(&bus->mutex);
	/*
	 * POSIX: pthread_cond_signal() function shall
	 * have no effect if there are no threads currently
	 * blocked on cond.
	 */
	tt_pthread_cond_signal(&bus->cond);
	return bus->pipe[peer_idx];
}

static void
cpipe_flush_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct cpipe *pipe = (struct cpipe *) watcher->data;
	struct fiber_pool *pool = pipe->pool;
	if (pipe->n_input == 0)
		return;

	/* Trigger task processing when the queue becomes non-empty. */
	bool pipe_was_empty;

	tt_pthread_mutex_lock(&pool->mutex);
	pipe_was_empty = !ev_async_pending(&pool->fetch_output);
	/** Flush input */
	stailq_concat(&pool->pipe, &pipe->input);
	tt_pthread_mutex_unlock(&pool->mutex);

	pipe->n_input = 0;
	if (pipe_was_empty) {
		/* Count statistics */
		rmean_collect(pipe->bus->stats, CBUS_STAT_EVENTS, 1);

		ev_async_send(pool->consumer, &pool->fetch_output);
	}
}

/* {{{ cmsg */

/**
 * Dispatch the message to the next hop.
 */
static inline void
cmsg_dispatch(struct cpipe *pipe, struct cmsg *msg)
{
	/**
	 * 'pipe' pointer saved in class constructor works as
	 * a guard that the message is alive. If a message route
	 * has the next pipe, then the message mustn't have been
	 * destroyed on this hop. Otherwise msg->hop->pipe could
	 * be already pointing to garbage.
	 */
	if (pipe) {
		/*
		 * Once we pushed the message to the bus,
		 * we relinquished all write access to it,
		 * so we must increase the current hop *before*
		 * push.
		 */
		msg->hop++;
		cpipe_push(pipe, msg);
	}
}

/**
 * Deliver the message and dispatch it to the next hop.
 */
static inline void
cmsg_deliver(struct cmsg *msg)
{
	/*
	 * Save the pointer to the last pipe,
	 * the memory where it is stored may be destroyed
	 * on the last hop.
	 */
	struct cpipe *pipe = msg->hop->pipe;
	msg->hop->f(msg);
	cmsg_dispatch(pipe, msg);
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

/* }}} cmsg */
