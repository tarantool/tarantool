#ifndef TARANTOOL_MUTEX_H_INCLUDED
#define TARNATOOL_MUTEX_H_INCLUDED
/*
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

#include <assert.h>
#include "fiber.h"
#include "salad/rlist.h"

/** Mutex of cooperative multitasking environment. */

struct mutex
{
	/**
	 * The queue of fibers waiting on a mutex.
	 * The first fiber owns the mutex.
	 */
	struct rlist queue;
};

/**
 * Initialize the given mutex.
 *
 * @param m   mutex to be initialized.
 */
static inline void
mutex_create(struct mutex *m)
{
	rlist_create(&m->queue);
}

static inline void
mutex_destroy(struct mutex *m)
{
	while (!rlist_empty(&m->queue)) {
		struct fiber *f = rlist_first_entry(&m->queue,
						    struct fiber, state);
		rlist_del_entry(f, state);
	}
}

/**
 * Lock a mutex. If the mutex is already locked by another fiber,
 * waits for timeout.
 *
 * @param m mutex to be locked.
 *
 * @retval false  success
 * @retval true   timeout
 */
static inline bool
mutex_lock_timeout(struct mutex *m, ev_tstamp timeout)
{
	rlist_add_tail_entry(&m->queue, fiber_self(), state);
	ev_tstamp start = timeout;
	while (timeout > 0) {
		struct fiber *f = rlist_first_entry(&m->queue,
						    struct fiber, state);
		if (f == fiber_self())
			break;

		fiber_yield_timeout(timeout);
		timeout -= ev_now() - start;
		if (timeout <= 0) {
			rlist_del_entry(fiber_self(), state);
			errno = ETIMEDOUT;
			return true;
		}
	}
	return false;
}

/**
 * Lock a mutex (no timeout). Waits indefinitely until
 * the current fiber can gain access to the mutex.
 */
static inline void
mutex_lock(struct mutex *m)
{
	(void) mutex_lock_timeout(m, TIMEOUT_INFINITY);
}

/**
 * Try to lock a mutex. Return immediately if the mutex is locked.
 * @retval false  success
 * @retval true   the mutex is locked.
 */
static inline bool
mutex_trylock(struct mutex *m)
{
	if (rlist_empty(&m->queue)) {
		mutex_lock(m);
		return false;
	}
	return true;
}

/**
 * Unlock a mutex. The fiber calling this function must
 * own the mutex.
 */
static inline void
mutex_unlock(struct mutex *m)
{
	struct fiber *f;
	f = rlist_first_entry(&m->queue, struct fiber, state);
	assert(f == fiber_self());
	rlist_del_entry(f, state);
	if (!rlist_empty(&m->queue)) {
		f = rlist_first_entry(&m->queue, struct fiber, state);
		fiber_wakeup(f);
	}
}

#endif /* TARANTOOL_MUTEX_H_INCLUDED */
