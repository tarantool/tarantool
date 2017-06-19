#ifndef INCLUDES_TARANTOOL_BOX_VY_QUOTA_H
#define INCLUDES_TARANTOOL_BOX_VY_QUOTA_H
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

#include <stddef.h>

#include <tarantool_ev.h> /* ev_tstamp */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_quota;

/**
 * Called when quota is consumed if used >= watermark.
 * It is supposed to instigate memory reclaim.
 */
typedef void
(*vy_quota_exceeded_f)(struct vy_quota *quota);

/**
 * Called when quota is consumed if used >= limit.
 * It is supposed to put the current fiber to sleep
 * until enough memory is freed. @timeout sepcifies
 * the maximal time to wait. The function should
 * return the time left or 0 on timeout.
 */
typedef ev_tstamp
(*vy_quota_throttled_f)(struct vy_quota *quota, ev_tstamp timeout);

/**
 * Called when quota is released if used < limit.
 * It is supposed to wake up all throttled fibers.
 */
typedef void
(*vy_quota_released_f)(struct vy_quota *quota);

/**
 * Quota used for accounting and limiting memory consumption
 * in the vinyl engine. It is NOT multi-threading safe.
 */
struct vy_quota {
	/**
	 * Memory limit. Once hit, new transactions are
	 * throttled until memory is reclaimed.
	 */
	size_t limit;
	/**
	 * Memory watermark. Exceeding it does not result in
	 * throttling new transactions, but it does trigger
	 * background memory reclaim.
	 */
	size_t watermark;
	/** Current memory consumption. */
	size_t used;
	/** Used-defined callbacks. */
	vy_quota_exceeded_f quota_exceeded_cb;
	vy_quota_throttled_f quota_throttled_cb;
	vy_quota_released_f quota_released_cb;
};

static inline void
vy_quota_init(struct vy_quota *q,
	      vy_quota_exceeded_f quota_exceeded_cb,
	      vy_quota_throttled_f quota_throttled_cb,
	      vy_quota_released_f quota_released_cb)
{
	q->limit = SIZE_MAX;
	q->watermark = SIZE_MAX;
	q->used = 0;
	q->quota_exceeded_cb = quota_exceeded_cb;
	q->quota_throttled_cb = quota_throttled_cb;
	q->quota_released_cb = quota_released_cb;
}

/**
 * Return true if memory reclaim should be triggered.
 */
static inline bool
vy_quota_is_exceeded(struct vy_quota *q)
{
	return q->used > q->watermark;
}

/**
 * Set memory limit. If current memory usage exceeds
 * the new limit, invoke the callback.
 */
static inline void
vy_quota_set_limit(struct vy_quota *q, size_t limit)
{
	q->limit = q->watermark = limit;
	if (q->used >= limit)
		q->quota_exceeded_cb(q);
}

/**
 * Set memory watermark. If current memory usage exceeds
 * the new watermark, invoke the callback.
 */
static inline void
vy_quota_set_watermark(struct vy_quota *q, size_t watermark)
{
	q->watermark = watermark;
	if (q->used >= watermark)
		q->quota_exceeded_cb(q);
}

/**
 * Consume @size bytes of memory. In contrast to vy_quota_use()
 * this function does not throttle the caller.
 */
static inline void
vy_quota_force_use(struct vy_quota *q, size_t size)
{
	q->used += size;
	if (q->used >= q->watermark)
		q->quota_exceeded_cb(q);
}

/**
 * Release @size bytes of memory.
 */
static inline void
vy_quota_release(struct vy_quota *q, size_t size)
{
	assert(q->used >= size);
	q->used -= size;
	if (q->used < q->limit)
		q->quota_released_cb(q);
}

/**
 * Try to consume @size bytes of memory, throttle the caller
 * if the limit is exceeded. @timeout specifies the maximal
 * time to wait. Return 0 on success, -1 on timeout.
 */
static inline int
vy_quota_use(struct vy_quota *q, size_t size, ev_tstamp timeout)
{
	vy_quota_force_use(q, size);
	while (q->used >= q->limit && timeout > 0)
		timeout = q->quota_throttled_cb(q, timeout);
	if (q->used > q->limit) {
		vy_quota_release(q, size);
		return -1;
	}
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_QUOTA_H */
