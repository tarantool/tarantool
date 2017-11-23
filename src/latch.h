#ifndef TARANTOOL_LATCH_H_INCLUDED
#define TARANTOOL_LATCH_H_INCLUDED
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

/** Latch of cooperative multitasking environment. */

struct latch
{
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
 * Lock a latch. If the latch is already locked by another fiber,
 * waits for timeout.
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

	rlist_add_tail_entry(&l->queue, fiber(), state);
	bool was_cancellable = fiber_set_cancellable(false);
	ev_tstamp start = ev_monotonic_now(loop());
	int result = 0;
	while (true) {
		fiber_yield_timeout(timeout);
		if (l->owner == fiber()) {
			/* Current fiber was woken by previous latch owner. */
			break;
		}
		timeout -= ev_monotonic_now(loop()) - start;
		if (timeout <= 0) {
			result = 1;
			break;
		}
		rlist_add_entry(&l->queue, fiber(), state);
	}
	fiber_set_cancellable(was_cancellable);
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
		struct fiber *f = rlist_first_entry(&l->queue,
						    struct fiber, state);
		/*
		 * Set this fiber as latch owner because fiber_wakeup remove
		 * its from waiting queue and any other already scheduled
		 * fiber can intercept this latch.
		 */
		l->owner = f;
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
* the latch.
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

#endif /* TARANTOOL_LATCH_H_INCLUDED */
