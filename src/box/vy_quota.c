/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "vy_quota.h"

#include <assert.h>
#include <stddef.h>
#include <tarantool_ev.h>

#include "fiber.h"
#include "fiber_cond.h"
#include "say.h"

void
vy_quota_create(struct vy_quota *q, vy_quota_exceeded_f quota_exceeded_cb)
{
	q->limit = SIZE_MAX;
	q->watermark = SIZE_MAX;
	q->used = 0;
	q->too_long_threshold = TIMEOUT_INFINITY;
	q->quota_exceeded_cb = quota_exceeded_cb;
	fiber_cond_create(&q->cond);
}

void
vy_quota_destroy(struct vy_quota *q)
{
	fiber_cond_broadcast(&q->cond);
	fiber_cond_destroy(&q->cond);
}

void
vy_quota_set_limit(struct vy_quota *q, size_t limit)
{
	q->limit = q->watermark = limit;
	if (q->used >= limit)
		q->quota_exceeded_cb(q);
	fiber_cond_broadcast(&q->cond);
}

void
vy_quota_set_watermark(struct vy_quota *q, size_t watermark)
{
	q->watermark = watermark;
	if (q->used >= watermark)
		q->quota_exceeded_cb(q);
}

void
vy_quota_force_use(struct vy_quota *q, size_t size)
{
	q->used += size;
	if (q->used >= q->watermark)
		q->quota_exceeded_cb(q);
}

void
vy_quota_release(struct vy_quota *q, size_t size)
{
	assert(q->used >= size);
	q->used -= size;
	fiber_cond_broadcast(&q->cond);
}

int
vy_quota_use(struct vy_quota *q, size_t size, double timeout)
{
	double start_time = ev_monotonic_now(loop());
	double deadline = start_time + timeout;
	while (q->used + size > q->limit && timeout > 0) {
		q->quota_exceeded_cb(q);
		if (fiber_cond_wait_deadline(&q->cond, deadline) != 0)
			break; /* timed out */
	}
	double wait_time = ev_monotonic_now(loop()) - start_time;
	if (wait_time > q->too_long_threshold) {
		say_warn("waited for %zu bytes of vinyl memory quota "
			 "for too long: %.3f sec", size, wait_time);
	}
	if (q->used + size > q->limit)
		return -1;
	q->used += size;
	if (q->used >= q->watermark)
		q->quota_exceeded_cb(q);
	return 0;
}

void
vy_quota_adjust(struct vy_quota *q, size_t reserved, size_t used)
{
	if (reserved > used) {
		size_t excess = reserved - used;
		assert(q->used >= excess);
		q->used -= excess;
		fiber_cond_broadcast(&q->cond);
	}
	if (reserved < used)
		vy_quota_force_use(q, used - reserved);
}

void
vy_quota_wait(struct vy_quota *q)
{
	while (q->used > q->limit)
		fiber_cond_wait(&q->cond);
}
