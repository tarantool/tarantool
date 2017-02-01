#ifndef TARANTOOL_FIBER_POOL_H_INCLUDED
#define TARANTOOL_FIBER_POOL_H_INCLUDED

#include "salad/stailq.h"
#include "small/rlist.h"
#include "tarantool_ev.h"

enum { FIBER_POOL_SIZE = 4096, FIBER_POOL_IDLE_TIMEOUT = 1 };

#define CACHELINE_SIZE 64
/**
 * A pool of worker fibers to handle messages,
 * so that each message is handled in its own fiber.
 */
struct fiber_pool {
	struct {
		/** Cache of fibers which work on incoming messages. */
		/*alignas(CACHELINE_SIZE)*/ struct rlist idle;
		/** The number of fibers in the pool. */
		int size;
		/** The limit on the number of fibers working on tasks. */
		int max_size;
		/**
		 * Fibers in leave the pool if they have nothing to do
		 * for longer than this.
		 */
		float idle_timeout;
		/** Staged messages (for fibers to work on) */
		struct stailq output;
		struct ev_timer idle_timer;
	};
	struct {
		/** The consumer thread loop. */
		/*alignas(CACHELINE_SIZE)*/ struct ev_loop *consumer;
		/**
		 * Used to trigger task processing when
		 * the pipe becomes non-empty.
		 */
		struct ev_async fetch_output;
		/** The lock around the pipe. */
		pthread_mutex_t mutex;
		/** The pipe with incoming messages. */
		struct stailq pipe;
	};
};
#undef CACHELINE_SIZE

/**
 * Initialize a fiber pool and connect it to a pipe. Currently
 * must be done before the pipe is actively used by a bus.
 */
void
fiber_pool_create(struct fiber_pool *pool, int max_pool_size,
		  float idle_timeout);

/**
 * Destroy a fiber pool
 */
void
fiber_pool_destroy(struct fiber_pool *pool);

#endif
