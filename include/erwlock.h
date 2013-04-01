#ifndef TARANTOOL_ERWLOCK_H_INCLUDED
#define TARNATOOL_ERWLOCK_H_INCLUDED

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
#include <rlist.h>

/* exclusive readers-writers locks */

struct erwlock {
	struct rlist readers, writers;
};

static inline void
erwlock_init(struct erwlock *l) {
	rlist_create(&l->readers);
	rlist_create(&l->writers);
}

static inline void
erwlock_destroy(struct erwlock *l) {
	struct fiber *f;
	while (!rlist_empty(&l->readers)) {
		f = rlist_first_entry(&l->readers, struct fiber, state);
		rlist_del_entry(f, state);
	}
	while (!rlist_empty(&l->writers)) {
		f = rlist_first_entry(&l->writers, struct fiber, state);
		rlist_del_entry(f, state);
	}
}

static inline bool
erwlock_lockq_timeout(struct rlist *q, ev_tstamp timeout) {
	rlist_add_tail_entry(q, fiber, state);
	ev_tstamp start = timeout;
	while (timeout > 0) {
		struct fiber *f = rlist_first_entry(q, struct fiber, state);
		if (f == fiber)
			break;
		fiber_yield_timeout(timeout);
		timeout -= ev_now() - start;
		if (timeout <= 0) {
			rlist_del_entry(fiber, state);
			errno = ETIMEDOUT;
			return true;
		}
	}
	return false;
}

static inline void
erwlock_unlockq(struct rlist *q) {
	struct fiber *f;
	f = rlist_first_entry(q, struct fiber, state);
	assert(f == fiber);
	rlist_del_entry(f, state);
	if (!rlist_empty(q)) {
		f = rlist_first_entry(q, struct fiber, state);
		fiber_wakeup(f);
	}
}

static inline bool
erwlock_lockedq(struct rlist *q) {
	return rlist_empty(q);
}

static inline bool
erwlock_lockread_timeout(struct erwlock *l, ev_tstamp timeout) {
	return erwlock_lockq_timeout(&l->readers, timeout);
}

static inline bool
erwlock_lockread(struct erwlock *l) {
	return erwlock_lockread_timeout(l, TIMEOUT_INFINITY);
}

static inline void
erwlock_unlockread(struct erwlock *l) {
	erwlock_unlockq(&l->readers);
}

static inline bool
erwlock_lockedread(struct erwlock *l) {
	return erwlock_lockedq(&l->readers);
}

static inline bool
erwlock_lockwrite_timeout(struct erwlock *l, ev_tstamp timeout) {
	return erwlock_lockq_timeout(&l->writers, timeout);
}

static inline bool
erwlock_lockwrite(struct erwlock *l) {
	return erwlock_lockwrite_timeout(l, TIMEOUT_INFINITY);
}

static inline void
erwlock_unlockwrite(struct erwlock *l) {
	erwlock_unlockq(&l->writers);
}

static inline bool
erwlock_lockedwrite(struct erwlock *l) {
	return erwlock_lockedq(&l->writers);
}

#endif
