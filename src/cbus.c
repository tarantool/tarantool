/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include <limits.h>
#include "fiber.h"

/**
 * Cord interconnect.
 */
struct cbus {
	/** cbus statistics */
	struct rmean *stats;
	/** A mutex to protect bus join. */
	pthread_mutex_t mutex;
	/** Condition for synchronized start of the bus. */
	pthread_cond_t cond;
	/** Connected endpoints */
	struct rlist endpoints;
};

/** A singleton for all cords. */
static struct cbus cbus;

const char *cbus_stat_strings[CBUS_STAT_LAST] = {
	"EVENTS",
	"LOCKS",
};

/**
 * Find a joined cbus endpoint by name.
 * This is an internal helper method which should be called
 * under cbus::mutex.
 *
 * @return endpoint or NULL if not found
 */
static struct cbus_endpoint *
cbus_find_endpoint(struct cbus *bus, const char *name)
{
	struct cbus_endpoint *endpoint;
	rlist_foreach_entry(endpoint, &bus->endpoints, in_cbus) {
		if (strcmp(endpoint->name, name) == 0)
			return endpoint;
	}
	return NULL;
}

static void
cpipe_flush_cb(ev_loop * /* loop */, struct ev_async *watcher,
	       int /* events */);

void
cpipe_create(struct cpipe *pipe, const char *consumer)
{
	stailq_create(&pipe->input);

	pipe->n_input = 0;
	pipe->max_input = INT_MAX;

	ev_async_init(&pipe->flush_input, cpipe_flush_cb);
	pipe->flush_input.data = pipe;

	tt_pthread_mutex_lock(&cbus.mutex);
	struct cbus_endpoint *endpoint = cbus_find_endpoint(&cbus, consumer);
	while (endpoint == NULL) {
		tt_pthread_cond_wait(&cbus.cond, &cbus.mutex);
		endpoint = cbus_find_endpoint(&cbus, consumer);
	}
	pipe->producer = cord()->loop;
	pipe->endpoint = endpoint;
	tt_pthread_mutex_unlock(&cbus.mutex);
}

static void
cbus_create(struct cbus *bus)
{
	bus->stats = rmean_new(cbus_stat_strings, CBUS_STAT_LAST);
	if (bus->stats == NULL)
		panic_syserror("cbus_create");

	/* Initialize queue lock mutex. */
	(void) tt_pthread_mutex_init(&bus->mutex, NULL);

	(void) tt_pthread_cond_init(&bus->cond, NULL);

	rlist_create(&bus->endpoints);
}

static void
cbus_destroy(struct cbus *bus)
{
	(void) tt_pthread_mutex_destroy(&bus->mutex);
	(void) tt_pthread_cond_destroy(&bus->cond);
	rmean_delete(bus->stats);
}

/**
 * Join a new endpoint (message consumer) to the bus. The endpoint
 * must have a unique name. Wakes up all producers (@sa cpipe_create())
 * who are blocked waiting for this endpoint to become available.
 */
void
cbus_join(struct cbus_endpoint *endpoint, const char *name,
	  void (*fetch_cb)(ev_loop *, struct ev_watcher *, int), void *fetch_data)
{
	tt_pthread_mutex_lock(&cbus.mutex);
	if (cbus_find_endpoint(&cbus, name) != NULL)
		panic("cbus endpoint %s joined twice", name);

	snprintf(endpoint->name, sizeof(endpoint->name), "%s", name);
	endpoint->consumer = loop();
	tt_pthread_mutex_init(&endpoint->mutex, NULL);
	stailq_create(&endpoint->pipe);
	ev_async_init(&endpoint->async,
		      (void (*)(ev_loop *, struct ev_async *, int)) fetch_cb);
	endpoint->async.data = fetch_data;
	ev_async_start(endpoint->consumer, &endpoint->async);

	rlist_add_tail(&cbus.endpoints, &endpoint->in_cbus);
	tt_pthread_mutex_unlock(&cbus.mutex);
	/*
	 * Alert all waiting producers.
	 *
	 * POSIX: pthread_cond_broadcast() function shall
	 * have no effect if there are no threads currently
	 * blocked on cond.
	 */
	tt_pthread_cond_broadcast(&cbus.cond);
}

static void
cpipe_flush_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct cpipe *pipe = (struct cpipe *) watcher->data;
	struct cbus_endpoint *endpoint = pipe->endpoint;
	if (pipe->n_input == 0)
		return;

	/* Trigger task processing when the queue becomes non-empty. */
	bool pipe_was_empty;

	tt_pthread_mutex_lock(&endpoint->mutex);
	pipe_was_empty = stailq_empty(&endpoint->pipe);
	/** Flush input */
	stailq_concat(&endpoint->pipe, &pipe->input);
	tt_pthread_mutex_unlock(&endpoint->mutex);

	pipe->n_input = 0;
	if (pipe_was_empty) {
		/* Count statistics */
		rmean_collect(cbus.stats, CBUS_STAT_EVENTS, 1);

		ev_async_send(endpoint->consumer, &endpoint->async);
	}
}

void
cbus_init()
{
	cbus_create(&cbus);
}

void
cbus_free()
{
	cbus_destroy(&cbus);
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
void
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

/* }}} cmsg */

/**
 * Call the target function and store the results (diag, rc) in
 * struct cbus_call_msg.
 */
void
cbus_call_perform(struct cmsg *m)
{
	struct cbus_call_msg *msg = (struct cbus_call_msg *)m;
	msg->rc = msg->func(msg);
	if (msg->rc)
		diag_move(&fiber()->diag, &msg->diag);
}

/**
 * Wake up the caller fiber to reap call results.
 * If the fiber is gone, e.g. in case of call timeout
 * or cancellation, invoke free_cb to free message state.
 */
void
cbus_call_done(struct cmsg *m)
{
	struct cbus_call_msg *msg = (struct cbus_call_msg *)m;
	if (msg->caller == NULL) {
		if (msg->free_cb)
			msg->free_cb(msg);
		return;
	}
	msg->complete = true;
	fiber_wakeup(msg->caller);
}

/**
 * Execute a synchronous call over cbus.
 */
int
cbus_call(struct cpipe *callee, struct cpipe *caller, struct cbus_call_msg *msg,
	cbus_call_f func, cbus_call_f free_cb, double timeout)
{
	int rc;

	diag_create(&msg->diag);
	msg->caller = fiber();
	msg->complete = false;
	msg->route[0].f = cbus_call_perform;
	msg->route[0].pipe = caller;
	msg->route[1].f = cbus_call_done;
	msg->route[1].pipe = NULL;
	cmsg_init(cmsg(msg), msg->route);

	msg->func = func;
	msg->free_cb = free_cb;
	msg->rc = 0;

	cpipe_push(callee, cmsg(msg));

	fiber_yield_timeout(timeout);
	if (msg->complete == false) {           /* timed out or cancelled */
		msg->caller = NULL;
		if (fiber_is_cancelled())
			diag_set(FiberIsCancelled);
		else
			diag_set(TimedOut);
		return -1;
	}
	if ((rc = msg->rc))
		diag_move(&msg->diag, &fiber()->diag);
	return rc;
}

void
cbus_process(struct cbus_endpoint *endpoint)
{
	struct stailq output;
	stailq_create(&output);
	cbus_endpoint_fetch(endpoint, &output);
	struct cmsg *msg, *msg_next;
	stailq_foreach_entry_safe(msg, msg_next, &output, fifo)
		cmsg_deliver(msg);
}

void
cbus_loop(struct cbus_endpoint *endpoint)
{
	while (true) {
		cbus_process(endpoint);
		if (fiber_is_cancelled())
			break;
		fiber_yield();
	}
}

static void
cbus_stop_loop_f(struct cmsg *msg)
{
	fiber_cancel(fiber());
	free(msg);
}

void
cbus_stop_loop(struct cpipe *pipe)
{
	/*
	 * Hack: static message only works because cmsg_deliver()
	 * is a no-op on the second hop.
	 */
	static const struct cmsg_hop route[1] = {
		{cbus_stop_loop_f, NULL}
	};
	struct cmsg *cancel = malloc(sizeof(struct cmsg));

	cmsg_init(cancel, route);

	cpipe_push(pipe, cancel);
	ev_invoke(pipe->producer, &pipe->flush_input, EV_CUSTOM);
}

