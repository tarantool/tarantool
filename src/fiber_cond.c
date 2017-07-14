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

#include "fiber_cond.h"

#include <tarantool_ev.h>

#include "fiber.h"

void
fiber_cond_create(struct fiber_cond *c)
{
	rlist_create(&c->waiters);
}

void
fiber_cond_destroy(struct fiber_cond *c)
{
	(void)c;
	assert(rlist_empty(&c->waiters));
}

void
fiber_cond_signal(struct fiber_cond *e)
{
	if (! rlist_empty(&e->waiters)) {
		struct fiber *f;
		f = rlist_shift_entry(&e->waiters, struct fiber, state);
		fiber_wakeup(f);
	}
}

void
fiber_cond_broadcast(struct fiber_cond *e)
{
	while (! rlist_empty(&e->waiters)) {
		struct fiber *f;
		f = rlist_shift_entry(&e->waiters, struct fiber, state);
		fiber_wakeup(f);
	}
}

int
fiber_cond_wait_timeout(struct fiber_cond *c, double timeout)
{
	struct fiber *f = fiber();
	rlist_add_tail_entry(&c->waiters, f, state);
	if (fiber_yield_timeout(timeout)) {
		diag_set(TimedOut);
		return -1;
	}
	return 0;
}

int
fiber_cond_wait(struct fiber_cond *c)
{
	return fiber_cond_wait_timeout(c, TIMEOUT_INFINITY);
}
