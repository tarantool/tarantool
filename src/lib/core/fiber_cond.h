#ifndef TARANTOOL_LIB_CORE_FIBER_COND_H_INCLUDED
#define TARANTOOL_LIB_CORE_FIBER_COND_H_INCLUDED 1
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <small/rlist.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

/**
 * Conditional variable for cooperative multitasking (fibers).
 *
 * A cond (short for "condition variable") is a synchronization primitive
 * that allow fibers to yield until some predicate is satisfied. Fiber
 * conditions have two basic operations - wait() and signal(). wait()
 * suspends execution of fiber (i.e. yields) until signal() is called.
 * Unlike pthread_cond, fiber_cond doesn't require mutex/latch wrapping.
 * 
 */
struct fiber_cond;

/** \endcond public */

struct fiber_cond {
	/** Waiting fibers */
	struct rlist waiters;
};

/**
 * Initialize the fiber condition variable.
 *
 * @param cond condition
 */
void
fiber_cond_create(struct fiber_cond *cond);

/**
 * Finalize the cond.
 * Behaviour is undefined if there are fiber waiting for the cond.
 * @param cond condition
 */
void
fiber_cond_destroy(struct fiber_cond *cond);

/** \cond public */

/**
 * Instantiate a new fiber cond object.
 */
struct fiber_cond *
fiber_cond_new(void);

/**
 * Delete the fiber cond object.
 * Behaviour is undefined if there are fiber waiting for the cond.
 */
void
fiber_cond_delete(struct fiber_cond *cond);

/**
 * Wake one fiber waiting for the cond.
 * Does nothing if no one is waiting.
 * @param cond condition
 */
void
fiber_cond_signal(struct fiber_cond *cond);

/**
 * Wake up all fibers waiting for the cond.
 * @param cond condition
 */
void
fiber_cond_broadcast(struct fiber_cond *cond);

/**
 * Suspend the execution of the current fiber (i.e. yield) until
 * fiber_cond_signal() is called. Like pthread_cond, fiber_cond can issue
 * spurious wake ups caused by explicit fiber_wakeup() or fiber_cancel()
 * calls. It is highly recommended to wrap calls to this function into a loop
 * and check an actual predicate and fiber_testcancel() on every iteration.
 *
 * @param cond condition
 * @param timeout timeout in seconds
 * @retval 0 on fiber_cond_signal() call or a spurious wake up
 * @retval -1 on timeout or fiber cancellation, diag is set
 */
int
fiber_cond_wait_timeout(struct fiber_cond *cond, double timeout);

/**
 * Shortcut for fiber_cond_wait_timeout().
 * @see fiber_cond_wait_timeout()
 */
int
fiber_cond_wait(struct fiber_cond *cond);

/** \endcond public */

/**
 * Wait until the given condition variable is signaled or the
 * deadline passed. The deadline is specified as absolute time
 * in seconds since system start (i.e. monotonic clock).
 * @see fiber_cond_wait_timeout()
 */
int
fiber_cond_wait_deadline(struct fiber_cond *cond, double deadline);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_FIBER_COND_H_INCLUDED */
