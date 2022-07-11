#ifndef TARANTOOL_LIB_CORE_LATCH_H_INCLUDED
#define TARANTOOL_LIB_CORE_LATCH_H_INCLUDED
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
#include <assert.h>
#include "small/rlist.h"
#include "fiber.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Latch of cooperative multitasking environment, which preserves strict order
 * of fibers waiting for the latch.
 */
struct latch {
	/**
	 * The fiber that locked the latch, or NULL
	 * if the latch is unlocked.
	 */
	struct fiber *owner;
	/**
	 * The queue of fibers waiting on the latch.
	 */
	struct rlist queue;
};

/**
 * A structure for adding to the latch->queue list. fiber->link cannot be used
 * for that purpose, because it may break the order of waiters.
 */
struct latch_waiter {
	/**
	 * The fiber that is waiting for the latch.
	 */
	struct fiber *fiber;
	/**
	 * Link in the latch->queue list.
	 */
	struct rlist link;
};

/**
 * latch initializer
 */
#define LATCH_INITIALIZER(name) { 0, RLIST_HEAD_INITIALIZER(name.queue) }

/**
 * Initialize the given latch.
 *
 * @param l - latch to be initialized.
 */
static inline void
latch_create(struct latch *l)
{
	l->owner = NULL;
	rlist_create(&l->queue);
}

/**
 * Destroy the given latch.
 *
 * @param l - latch to be destroyed.
 */
static inline void
latch_destroy(struct latch *l)
{
	assert(l->owner == NULL);
	assert(rlist_empty(&l->queue));
	(void) l;
}

/**
 * Return the fiber that locked the given latch, or NULL
 * if the latch is unlocked.
 *
 * @param l - latch to be checked.
 */
static inline struct fiber *
latch_owner(struct latch *l)
{
	return l->owner;
}

/**
 * Return true if the latch is locked.
 *
 * @param l - latch to be tested.
 */
static inline bool
latch_is_locked(const struct latch *l)
{
	return l->owner != NULL;
}

/**
 * Lock a latch. If the latch is already locked by another fiber,
 * waits for timeout.
 * Locks are acquired in the strict order as they were requested.
 *
 * @param l - latch to be locked.
 * @param timeout - maximal time to wait
 *
 * @retval 0 - success
 * @retval 1 - timeout
 */
static inline int
latch_lock_timeout(struct latch *l, ev_tstamp timeout)
{
	assert(l->owner != fiber());
	if (l->owner == NULL && rlist_empty(&l->queue)) {
		l->owner = fiber();
		return 0;
	}
	if (timeout <= 0)
		return 1;

	int result = 0;
	struct latch_waiter waiter;
	waiter.fiber = fiber();
	rlist_add_tail_entry(&l->queue, &waiter, link);
	ev_tstamp deadline = ev_monotonic_now(loop()) + timeout;

	while (true) {
		bool exceeded = fiber_yield_deadline(deadline);
		if (l->owner == fiber()) {
			/* Current fiber was woken by previous latch owner. */
			break;
		}
		if (exceeded) {
			result = 1;
			break;
		}
	}
	rlist_del_entry(&waiter, link);
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
	assert(l->owner == fiber());
	l->owner = NULL;
	if (!rlist_empty(&l->queue)) {
		/*
		 * Set the first waiter as latch owner because otherwise any
		 * other waiter can intercept this latch in arbitrary order.
		 */
		struct latch_waiter *waiter;
		waiter = rlist_first_entry(&l->queue,
					   struct latch_waiter,
					   link);
		l->owner = waiter->fiber;
		fiber_wakeup(waiter->fiber);
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
box_latch_t*
box_latch_new(void);

/**
 * Destroy and free the latch.
 * \param latch latch
 */
void
box_latch_delete(box_latch_t *latch);

/**
* Lock a latch. Waits indefinitely until the current fiber can gain access to
* the latch. Locks are acquired in the strict order as they were requested.
*
* \param latch a latch
*/
void
box_latch_lock(box_latch_t *latch);

/**
 * Try to lock a latch. Return immediately if the latch is locked.
 * \param latch a latch
 * \retval 0 - success
 * \retval 1 - the latch is locked.
 */
int
box_latch_trylock(box_latch_t *latch);

/**
 * Unlock a latch. The fiber calling this function must
 * own the latch.
 *
 * \param latch a latch
 */
void
box_latch_unlock(box_latch_t *latch);

/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_LATCH_H_INCLUDED */
