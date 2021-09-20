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

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <small/rlist.h>
#include <tarantool_ev.h>

#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct vy_quota;

/** Rate limit state. */
struct vy_rate_limit {
	/** Max allowed rate, per second. */
	size_t rate;
	/** Current quota. */
	ssize_t value;
};

/** Initialize a rate limit state. */
static inline void
vy_rate_limit_create(struct vy_rate_limit *rl)
{
	rl->rate = SIZE_MAX;
	rl->value = SSIZE_MAX;
}

/** Set rate limit. */
static inline void
vy_rate_limit_set(struct vy_rate_limit *rl, size_t rate)
{
	rl->rate = rate;
}

/**
 * Return true if quota may be consumed without exceeding
 * the configured rate limit.
 */
static inline bool
vy_rate_limit_may_use(struct vy_rate_limit *rl)
{
	return rl->value > 0;
}

/** Consume the given amount of quota. */
static inline void
vy_rate_limit_use(struct vy_rate_limit *rl, size_t size)
{
	rl->value -= size;
}

/** Release the given amount of quota. */
static inline void
vy_rate_limit_unuse(struct vy_rate_limit *rl, size_t size)
{
	rl->value += size;
}

/**
 * Replenish quota by the amount accumulated for the given
 * time interval.
 */
static inline void
vy_rate_limit_refill(struct vy_rate_limit *rl, double time)
{
	double size = rl->rate * time;
	double value = rl->value + size;
	/* Allow bursts up to 2x rate. */
	value = MIN(value, size * 2);
	rl->value = MIN((ssize_t)value, SSIZE_MAX);
}

typedef void
(*vy_quota_exceeded_f)(struct vy_quota *quota);

/**
 * Apart from memory usage accounting and limiting, vy_quota is
 * responsible for consumption rate limiting (aka throttling).
 * There are multiple rate limits, each of which is associated
 * with a particular resource type. Different kinds of consumers
 * respect different limits. The following enumeration defines
 * the resource types for which vy_quota enables throttling.
 *
 * See also vy_quota_consumer_resource_map.
 */
enum vy_quota_resource_type {
	/**
	 * The goal of disk-based throttling is to keep LSM trees
	 * in a good shape so that read and space amplification
	 * stay within bounds. It is enabled when compaction does
	 * not keep up with dumps.
	 */
	VY_QUOTA_RESOURCE_DISK = 0,
	/**
	 * Memory-based throttling is needed to avoid long stalls
	 * caused by hitting the hard memory limit. It is set so
	 * that by the time the hard limit is hit, the last memory
	 * dump will have completed.
	 */
	VY_QUOTA_RESOURCE_MEMORY = 1,

	vy_quota_resource_type_MAX,
};

/**
 * Quota consumer type determines how a quota consumer will be
 * rate limited.
 *
 * See also vy_quota_consumer_resource_map.
 */
enum vy_quota_consumer_type {
	/** Transaction processor. */
	VY_QUOTA_CONSUMER_TX = 0,
	/** Compaction job. */
	VY_QUOTA_CONSUMER_COMPACTION = 1,
	/** Request to build a new index. */
	VY_QUOTA_CONSUMER_DDL = 2,

	vy_quota_consumer_type_MAX,
};

struct vy_quota_wait_node {
	/** Link in vy_quota::wait_queue. */
	struct rlist in_wait_queue;
	/** Fiber waiting for quota. */
	struct fiber *fiber;
	/** Amount of requested memory. */
	size_t size;
	/**
	 * Ticket assigned to this fiber when it was put to
	 * sleep, see vy_quota::wait_ticket for more details.
	 */
	int64_t ticket;
};

/**
 * Quota used for accounting and limiting memory consumption
 * in the vinyl engine. It is NOT multi-threading safe.
 */
struct vy_quota {
	/** Set if the quota was enabled. */
	bool is_enabled;
	/** Number of consumers waiting for quota. */
	int n_blocked;
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
	 * Called if the limit is hit when quota is consumed.
	 * It is supposed to trigger memory reclaim.
	 */
	vy_quota_exceeded_f quota_exceeded_cb;
	/**
	 * Monotonically growing counter assigned to consumers
	 * waiting for quota. It is used for balancing wakeups
	 * among wait queues: if two fibers from different wait
	 * queues may proceed, the one with the lowest ticket
	 * will be picked.
	 *
	 * See also vy_quota_wait_node::ticket.
	 */
	int64_t wait_ticket;
	/**
	 * Queue of consumers waiting for quota, one per each
	 * consumer type, linked by vy_quota_wait_node::state.
	 * Newcomers are added to the tail.
	 */
	struct rlist wait_queue[vy_quota_consumer_type_MAX];
	/** Rate limit state, one per each resource type. */
	struct vy_rate_limit rate_limit[vy_quota_resource_type_MAX];
	/**
	 * Periodic timer that is used for refilling the rate
	 * limit value.
	 */
	ev_timer timer;
};

/**
 * Initialize a quota object.
 *
 * Note, the limit won't be imposed until vy_quota_enable()
 * is called.
 */
void
vy_quota_create(struct vy_quota *q, size_t limit,
		vy_quota_exceeded_f quota_exceeded_cb);

/**
 * Enable the configured limit for a quota object.
 */
void
vy_quota_enable(struct vy_quota *q);

/**
 * Destroy a quota object.
 */
void
vy_quota_destroy(struct vy_quota *q);

/**
 * Set memory limit. If current memory usage exceeds
 * the new limit, invoke the callback.
 */
void
vy_quota_set_limit(struct vy_quota *q, size_t limit);

/**
 * Set the rate limit corresponding to the resource of the given
 * type. The rate limit is given in bytes per second.
 */
void
vy_quota_set_rate_limit(struct vy_quota *q, enum vy_quota_resource_type type,
			size_t rate);

/**
 * Return the rate limit applied to a consumer of the given type.
 */
size_t
vy_quota_get_rate_limit(struct vy_quota *q, enum vy_quota_consumer_type type);

/**
 * Consume @size bytes of memory. In contrast to vy_quota_use()
 * this function does not throttle the caller.
 */
void
vy_quota_force_use(struct vy_quota *q, enum vy_quota_consumer_type type,
		   size_t size);

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
vy_quota_use(struct vy_quota *q, enum vy_quota_consumer_type type,
	     size_t size, double timeout);

/**
 * Adjust quota after allocating memory.
 *
 * @reserved: size of quota reserved by vy_quota_use().
 * @used: size of memory actually allocated.
 *
 * See also vy_quota_use().
 */
void
vy_quota_adjust(struct vy_quota *q, enum vy_quota_consumer_type type,
		size_t reserved, size_t used);

/**
 * Block the caller until the quota is not exceeded.
 */
static inline void
vy_quota_wait(struct vy_quota *q, enum vy_quota_consumer_type type)
{
	vy_quota_use(q, type, 0, TIMEOUT_INFINITY);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_QUOTA_H */
