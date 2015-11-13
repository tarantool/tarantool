#ifndef TARANTOOL_LATCH_H_INCLUDED
#define TARANTOOL_LATCH_H_INCLUDED
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
#include <assert.h>
#include "small/rlist.h"
#include "fiber.h"

/** Latch of cooperative multitasking environment. */

struct latch
{
	/**
	 * State of latch. 0 - not locked, 1 - locked.
	 */
	int locked;
	/**
	 * The queue of fibers waiting on a latch.
	 */
	struct rlist queue;
};

/**
 * latch initializer
 */
#define LATCH_INITIALIZER(name) { 0, RLIST_HEAD_INITIALIZER(name.queue) }

/**
 * Initialize the given latch.
 *
 * @param m - latch to be initialized.
 */
static inline void
latch_create(struct latch *l)
{
	l->locked = 0;
	rlist_create(&l->queue);
}

/**
 * Destroy the given latch.
 *
 * @param m - latch to be destroyed.
 */
static inline void
latch_destroy(struct latch *l)
{
	assert(rlist_empty(&l->queue));
	(void) l;
}

/**
 * Lock a latch. If the latch is already locked by another fiber,
 * waits for timeout.
 *
 * @param m - latch to be locked.
 * @param timeout - maximal time to wait
 *
 * @retval 0 - success
 * @retval 1 - timeout
 */
static inline int
latch_lock_timeout(struct latch *l, ev_tstamp timeout)
{
	if (l->locked == 0 && rlist_empty(&l->queue)) {
		l->locked = 1;
		return 0;
	}
	if (timeout <= 0)
		return 1;

	rlist_add_tail_entry(&l->queue, fiber(), state);
	bool was_cancellable = fiber_set_cancellable(false);
	ev_tstamp start = timeout;
	int result = 0;
	while (true) {
		fiber_yield_timeout(timeout);
		if (l->locked == 0) {
			l->locked = 1;
			break;
		}
		timeout -= ev_now(loop()) - start;
		if (timeout <= 0) {
			result = 1;
			break;
		}
	}
	fiber_set_cancellable(was_cancellable);
	rlist_del_entry(fiber(), state);
	return result;
}

/**
 * \copydoc box_latch_lock
 */
static inline void
latch_lock(struct latch *l)
{
	(void) latch_lock_timeout(l, TIMEOUT_INFINITY);
}

/**
 * \copydoc box_latch_trylock
 */
static inline int
latch_trylock(struct latch *l)
{
	return latch_lock_timeout(l, 0);
}

/**
 * \copydoc box_latch_unlock
 */
static inline void
latch_unlock(struct latch *l)
{
	assert(l->locked);
	l->locked = 0;
	if (!rlist_empty(&l->queue)) {
		struct fiber *f = rlist_first_entry(&l->queue,
						    struct fiber, state);
		fiber_wakeup(f);
	}
}

/** \cond public */

/**
 * A lock for cooperative multitasking environment
 */
typedef struct box_latch box_latch_t;

/**
 * Allocate and initialize the new latch.
 * \returns latch
 */
API_EXPORT box_latch_t*
box_latch_new(void);

/**
 * Destroy and free the latch.
 * \param latch latch
 */
API_EXPORT void
box_latch_delete(box_latch_t *latch);

/**
* Lock a latch. Waits indefinitely until the current fiber can gain access to
* the latch.
*
* \param latch a latch
*/
API_EXPORT void
box_latch_lock(box_latch_t *latch);

/**
 * Try to lock a latch. Return immediately if the latch is locked.
 * \param latch a latch
 * \retval 0 - success
 * \retval 1 - the latch is locked.
 */
API_EXPORT int
box_latch_trylock(box_latch_t *latch);

/**
 * Unlock a latch. The fiber calling this function must
 * own the latch.
 *
 * \param latch a ltach
 */
API_EXPORT void
box_latch_unlock(box_latch_t *latch);

/** \endcond public */

#endif /* TARANTOOL_LATCH_H_INCLUDED */
