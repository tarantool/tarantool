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
#include <rlist.h>

struct mutex {
	struct rlist q;
};

static inline void
mutex_init(struct mutex *m) {
	rlist_create(&m->q);
}

static inline void
mutex_destroy(struct mutex *m) {
	struct fiber *f;
	while (!rlist_empty(&m->q)) {
		f = rlist_first_entry(&m->q, struct fiber, state);
		rlist_del_entry(f, state);
	}
	rlist_create(&m->q);
}

static inline bool
mutex_lock_timeout(struct mutex *m, ev_tstamp timeout) {
	rlist_add_tail_entry(&m->q, fiber, state);
	ev_tstamp start = timeout;
	while (timeout > 0) {
		struct fiber *f = rlist_first_entry(&m->q, struct fiber, state);
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

static inline bool
mutex_lock(struct mutex *m) {
	return mutex_lock_timeout(m, TIMEOUT_INFINITY);
}

static inline void
mutex_unlock(struct mutex *m) {
	struct fiber *f;
	f = rlist_first_entry(&m->q, struct fiber, state);
	assert(f == fiber);
	rlist_del_entry(f, state);
	if (!rlist_empty(&m->q)) {
		f = rlist_first_entry(&m->q, struct fiber, state);
		fiber_wakeup(f);
	}
}

#endif
