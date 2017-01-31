#include "fiber_pool.h"

#include <stdarg.h>

#include "fiber.h"
#include "cbus.h"

/* {{{ fiber_pool */

/**
 * Main function of the fiber invoked to handle all outstanding
 * tasks in a queue.
 */
static int
fiber_pool_f(va_list ap)
{
	struct fiber_pool *pool = va_arg(ap, struct fiber_pool *);
	struct cord *cord = cord();
	struct fiber *f = fiber();
	struct ev_loop *loop = pool->consumer;
	struct stailq *output = &pool->output;
	struct cmsg *msg;
	ev_tstamp last_active_at = ev_now(loop);
	pool->size++;
restart:
	msg = NULL;
	while (! stailq_empty(output)) {
		 msg = stailq_shift_entry(output, struct cmsg, fifo);

		if (f->caller == &cord->sched && ! stailq_empty(output) &&
		    ! rlist_empty(&pool->idle)) {
			/*
			 * Activate a "backup" fiber for the next
			 * message in the queue.
			 */
			f->caller = rlist_shift_entry(&pool->idle,
						      struct fiber,
						      state);
			assert(f->caller->caller == &cord->sched);
		}
		cmsg_deliver(msg);
	}
	/** Put the current fiber into a fiber cache. */
	if (msg != NULL || ev_now(loop) - last_active_at < pool->idle_timeout) {
		if (msg != NULL)
			last_active_at = ev_now(loop);
		/*
		 * Add the fiber to the front of the list, so that
		 * it is most likely to get scheduled again.
		 */
		rlist_add_entry(&pool->idle, fiber(), state);
		fiber_yield();
		goto restart;
	}
	pool->size--;
	return 0;
}

static void
fiber_pool_idle_cb(ev_loop *loop, struct ev_timer *watcher, int events)
{
	(void) events;
	struct fiber_pool *pool = (struct fiber_pool *) watcher->data;
	if (! rlist_empty(&pool->idle)) {
		struct fiber *f;
		/*
		 * Schedule the fiber at the tail of the list,
		 * it's the one most likely to have not been
		 * scheduled lately.
		 */
		f = rlist_shift_tail_entry(&pool->idle, struct fiber, state);
		fiber_call(f);
	}
	ev_timer_again(loop, watcher);
}

/** Create fibers to handle all outstanding tasks. */
static void
fiber_pool_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct fiber_pool *pool = (struct fiber_pool *) watcher->data;
	/** Fetch messages */
	cbus_endpoint_fetch(&pool->endpoint, &pool->output);

	struct stailq *output = &pool->output;
	while (! stailq_empty(output)) {
		struct fiber *f;
		if (! rlist_empty(&pool->idle)) {
			f = rlist_shift_entry(&pool->idle, struct fiber, state);
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
fiber_pool_create(struct fiber_pool *pool, const char *name, int max_pool_size,
		  float idle_timeout)
{
	pool->consumer = loop();
	pool->idle_timeout = idle_timeout;
	rlist_create(&pool->idle);
	ev_timer_init(&pool->idle_timer, fiber_pool_idle_cb, 0,
		      pool->idle_timeout);
	pool->idle_timer.data = pool;
	ev_timer_again(loop(), &pool->idle_timer);
	pool->size = 0;
	pool->max_size = max_pool_size;
	stailq_create(&pool->output);
	/* Join fiber pool to cbus */
	cbus_join(&pool->endpoint, name, fiber_pool_cb, pool);
}

void
fiber_pool_destroy(struct fiber_pool *pool)
{
	(void) pool;
	/*
	 * Do not destroy async or idle timers, or fibers:
	 * events are destroyed along with the event loop,
	 * and fibers are freed at once when thread runtime
	 * pool is destroyed.
         */
	/*
	 * cbus leaving is not implemented yet
	 */
}

/** }}} fiber_pool */

