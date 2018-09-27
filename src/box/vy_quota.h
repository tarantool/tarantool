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
#include <tarantool_ev.h>
#include "fiber_cond.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_quota;

typedef void
(*vy_quota_exceeded_f)(struct vy_quota *quota);

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
	/** Current memory consumption. */
	size_t used;
	/**
	 * If vy_quota_use() takes longer than the given
	 * value, warn about it in the log.
	 */
	double too_long_threshold;
	/**
	 * Condition variable used for throttling consumers when
	 * there is no quota left.
	 */
	struct fiber_cond cond;
	/**
	 * Called if the limit is hit when quota is consumed.
	 * It is supposed to trigger memory reclaim.
	 */
	vy_quota_exceeded_f quota_exceeded_cb;
};

void
vy_quota_create(struct vy_quota *q, vy_quota_exceeded_f quota_exceeded_cb);

void
vy_quota_destroy(struct vy_quota *q);

/**
 * Set memory limit. If current memory usage exceeds
 * the new limit, invoke the callback.
 */
void
vy_quota_set_limit(struct vy_quota *q, size_t limit);

/**
 * Consume @size bytes of memory. In contrast to vy_quota_use()
 * this function does not throttle the caller.
 */
void
vy_quota_force_use(struct vy_quota *q, size_t size);

/**
 * Release @size bytes of memory.
 */
void
vy_quota_release(struct vy_quota *q, size_t size);

/**
 * Try to consume @size bytes of memory, throttle the caller
 * if the limit is exceeded. @timeout specifies the maximal
 * time to wait. Return 0 on success, -1 on timeout.
 *
 * Usage pattern:
 *
 *   size_t reserved = <estimate>;
 *   if (vy_quota_use(q, reserved, timeout) != 0)
 *           return -1;
 *   <allocate memory>
 *   size_t used = <actually allocated>;
 *   vy_quota_adjust(q, reserved, used);
 *
 * We use two-step quota allocation strategy (reserve-consume),
 * because we may not yield after we start inserting statements
 * into a space so we estimate the allocation size and wait for
 * quota before committing statements. At the same time, we
 * cannot precisely estimate the size of memory we are going to
 * consume so we adjust the quota after the allocation.
 *
 * The size of memory allocated while committing a transaction
 * may be greater than an estimate, because insertion of a
 * statement into an in-memory index can trigger allocation
 * of a new index extent. This should not normally result in a
 * noticeable breach in the memory limit, because most memory
 * is occupied by statements, but we need to adjust the quota
 * accordingly after the allocation in this case.
 *
 * The actual memory allocation size may also be less than an
 * estimate if the space has multiple indexes, because statements
 * are stored in the common memory level, which isn't taken into
 * account while estimating the size of a memory allocation.
 */
int
vy_quota_use(struct vy_quota *q, size_t size, double timeout);

/**
 * Adjust quota after allocating memory.
 *
 * @reserved: size of quota reserved by vy_quota_use().
 * @used: size of memory actually allocated.
 *
 * See also vy_quota_use().
 */
void
vy_quota_adjust(struct vy_quota *q, size_t reserved, size_t used);

/**
 * Block the caller until the quota is not exceeded.
 */
static inline void
vy_quota_wait(struct vy_quota *q)
{
	vy_quota_use(q, 0, TIMEOUT_INFINITY);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_QUOTA_H */
