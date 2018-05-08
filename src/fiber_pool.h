#ifndef TARANTOOL_FIBER_POOL_H_INCLUDED
#define TARANTOOL_FIBER_POOL_H_INCLUDED
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
#include "trivia/config.h"
#include "fiber.h"
#include "cbus.h"
#include "small/rlist.h"
#include "salad/stailq.h"
#include "tarantool_ev.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Period after which an idle fiber in the pool is shut down. */
enum { FIBER_POOL_IDLE_TIMEOUT = 1 };

/**
 * A pool of worker fibers to handle messages,
 * so that each message is handled in its own fiber.
 */
struct fiber_pool {
	struct {
		/** Cache of fibers which work on incoming messages. */
		alignas(CACHELINE_SIZE) struct rlist idle;
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
		/** Timer for idle workers */
		struct ev_timer idle_timer;
		/** Condition for worker exit signaling */
		struct fiber_cond worker_cond;
	};
	struct {
		/** The consumer thread loop. */
		alignas(CACHELINE_SIZE) struct ev_loop *consumer;
		/** cbus endpoint to fetch messages from */
		struct cbus_endpoint endpoint;
	};
};

/**
 * Initialize a fiber pool and connect it to a pipe. Currently
 * must be done before the pipe is actively used by a bus.
 */
void
fiber_pool_create(struct fiber_pool *pool, const char *name, int max_pool_size,
		  float idle_timeout);

/**
 * Set maximal fiber pool size.
 * @param pool Fiber pool to set size.
 * @param new_max_size New maximal size.
 */
void
fiber_pool_set_max_size(struct fiber_pool *pool, int new_max_size);

/**
 * Destroy a fiber pool
 */
void
fiber_pool_destroy(struct fiber_pool *pool);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_FIBER_POOL_H_INCLUDED */
