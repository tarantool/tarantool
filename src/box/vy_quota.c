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
#include <stdbool.h>
#include <stddef.h>
#include <tarantool_ev.h>

#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "errinj.h"
#include "fiber.h"
#include "say.h"
#include "trivia/util.h"

/**
 * Quota timer period, in seconds.
 *
 * The timer is used for replenishing the rate limit value so
 * its period defines how long throttled transactions will wait.
 * Therefore use a relatively small period.
 */
static const double VY_QUOTA_TIMER_PERIOD = 0.1;

/**
 * Bit mask of resources used by a particular consumer type.
 */
static unsigned
vy_quota_consumer_resource_map[] = {
	/**
	 * Transaction throttling pursues two goals. First, it is
	 * capping memory consumption rate so that the hard memory
	 * limit will not be hit before memory dump has completed
	 * (memory-based throttling). Second, we must make sure
	 * that compaction jobs keep up with dumps to keep read and
	 * space amplification within bounds (disk-based throttling).
	 * Transactions ought to respect them both.
	 */
	[VY_QUOTA_CONSUMER_TX] = (1 << VY_QUOTA_RESOURCE_DISK) |
				 (1 << VY_QUOTA_RESOURCE_MEMORY),
	/**
	 * Compaction jobs may need some quota too, because they
	 * may generate deferred DELETEs for secondary indexes.
	 * Apparently, we must not impose the rate limit that is
	 * supposed to speed up compaction on them (disk-based),
	 * however they still have to respect memory-based throttling
	 * to avoid long stalls.
	 */
	[VY_QUOTA_CONSUMER_COMPACTION] = (1 << VY_QUOTA_RESOURCE_MEMORY),
	/**
	 * Since DDL is triggered by the admin, it can be deliberately
	 * initiated when the workload is known to be low. Throttling
	 * it along with DML requests would only cause exasperation in
	 * this case. So we don't apply disk-based rate limit to DDL.
	 * This should be fine, because the disk-based limit is set
	 * rather strictly to let the workload some space to grow, see
	 * vy_regulator_update_rate_limit(), and in contrast to the
	 * memory-based limit, exceeding the disk-based limit doesn't
	 * result in abrupt stalls - it may only lead to a gradual
	 * accumulation of disk space usage and read latency.
	 */
	[VY_QUOTA_CONSUMER_DDL] = (1 << VY_QUOTA_RESOURCE_MEMORY),
};

/**
 * Check if the rate limit corresponding to resource @resource_type
 * should be applied to a consumer of type @consumer_type.
 */
static inline bool
vy_rate_limit_is_applicable(enum vy_quota_consumer_type consumer_type,
			    enum vy_quota_resource_type resource_type)
{
	return (vy_quota_consumer_resource_map[consumer_type] &
					(1 << resource_type)) != 0;
}

/**
 * Return true if the requested amount of memory may be consumed
 * right now, false if consumers have to wait.
 *
 * If the requested amount of memory cannot be consumed due to
 * the configured limit, invoke the registered callback so that
 * it can start memory reclaim immediately.
 */
static inline bool
vy_quota_may_use(struct vy_quota *q, enum vy_quota_consumer_type type,
		 size_t size)
{
	if (!q->is_enabled)
		return true;
	if (q->used + size > q->limit) {
		q->quota_exceeded_cb(q);
		return false;
	}
	for (int i = 0; i < vy_quota_resource_type_MAX; i++) {
		struct vy_rate_limit *rl = &q->rate_limit[i];
		if (vy_rate_limit_is_applicable(type, i) &&
		    !vy_rate_limit_may_use(rl))
			return false;
	}
	return true;
}

/**
 * Consume the given amount of memory without checking the limit.
 */
static inline void
vy_quota_do_use(struct vy_quota *q, enum vy_quota_consumer_type type,
		size_t size)
{
	q->used += size;
	for (int i = 0; i < vy_quota_resource_type_MAX; i++) {
		struct vy_rate_limit *rl = &q->rate_limit[i];
		if (vy_rate_limit_is_applicable(type, i))
			vy_rate_limit_use(rl, size);
	}
}

/**
 * Return the given amount of memory without waking blocked fibers.
 * This function is an exact opposite of vy_quota_do_use().
 */
static inline void
vy_quota_do_unuse(struct vy_quota *q, enum vy_quota_consumer_type type,
		  size_t size)
{
	assert(q->used >= size);
	q->used -= size;
	for (int i = 0; i < vy_quota_resource_type_MAX; i++) {
		struct vy_rate_limit *rl = &q->rate_limit[i];
		if (vy_rate_limit_is_applicable(type, i))
			vy_rate_limit_unuse(rl, size);
	}
}

/**
 * Invoke the registered callback in case memory usage exceeds
 * the configured limit.
 */
static inline void
vy_quota_check_limit(struct vy_quota *q)
{
	if (q->is_enabled && q->used > q->limit)
		q->quota_exceeded_cb(q);
}

/**
 * Wake up the first consumer in the line waiting for quota.
 */
static void
vy_quota_signal(struct vy_quota *q)
{
	/*
	 * To prevent starvation, wake up a consumer that has
	 * waited most irrespective of its type.
	 */
	struct vy_quota_wait_node *oldest = NULL;
	for (int i = 0; i < vy_quota_consumer_type_MAX; i++) {
		struct rlist *wq = &q->wait_queue[i];
		if (rlist_empty(wq))
			continue;

		struct vy_quota_wait_node *n;
		n = rlist_first_entry(wq, struct vy_quota_wait_node,
				      in_wait_queue);
		/*
		 * No need in waking up a consumer if it will have
		 * to go back to sleep immediately.
		 */
		if (!vy_quota_may_use(q, i, n->size))
			continue;

		if (oldest == NULL || oldest->ticket > n->ticket)
			oldest = n;
	}
	if (oldest != NULL)
		fiber_wakeup(oldest->fiber);
}

static void
vy_quota_timer_cb(ev_loop *loop, ev_timer *timer, int events)
{
	(void)loop;
	(void)events;

	struct vy_quota *q = timer->data;

	for (int i = 0; i < vy_quota_resource_type_MAX; i++)
		vy_rate_limit_refill(&q->rate_limit[i], VY_QUOTA_TIMER_PERIOD);
	vy_quota_signal(q);
}

void
vy_quota_create(struct vy_quota *q, size_t limit,
		vy_quota_exceeded_f quota_exceeded_cb)
{
	q->is_enabled = false;
	q->n_blocked = 0;
	q->limit = limit;
	q->used = 0;
	q->too_long_threshold = TIMEOUT_INFINITY;
	q->quota_exceeded_cb = quota_exceeded_cb;
	q->wait_ticket = 0;
	for (int i = 0; i < vy_quota_consumer_type_MAX; i++)
		rlist_create(&q->wait_queue[i]);
	for (int i = 0; i < vy_quota_resource_type_MAX; i++)
		vy_rate_limit_create(&q->rate_limit[i]);
	ev_timer_init(&q->timer, vy_quota_timer_cb, 0, VY_QUOTA_TIMER_PERIOD);
	q->timer.data = q;
}

void
vy_quota_enable(struct vy_quota *q)
{
	assert(!q->is_enabled);
	q->is_enabled = true;
	ev_timer_start(loop(), &q->timer);
	vy_quota_check_limit(q);
}

void
vy_quota_destroy(struct vy_quota *q)
{
	ev_timer_stop(loop(), &q->timer);
}

void
vy_quota_set_limit(struct vy_quota *q, size_t limit)
{
	q->limit = limit;
	vy_quota_check_limit(q);
	vy_quota_signal(q);
}

void
vy_quota_set_rate_limit(struct vy_quota *q, enum vy_quota_resource_type type,
			size_t rate)
{
	vy_rate_limit_set(&q->rate_limit[type], rate);
}

size_t
vy_quota_get_rate_limit(struct vy_quota *q, enum vy_quota_consumer_type type)
{
	size_t rate = SIZE_MAX;
	for (int i = 0; i < vy_quota_resource_type_MAX; i++) {
		struct vy_rate_limit *rl = &q->rate_limit[i];
		if (vy_rate_limit_is_applicable(type, i))
			rate = MIN(rate, rl->rate);
	}
	return rate;
}

void
vy_quota_force_use(struct vy_quota *q, enum vy_quota_consumer_type type,
		   size_t size)
{
	vy_quota_do_use(q, type, size);
	vy_quota_check_limit(q);
}

void
vy_quota_release(struct vy_quota *q, size_t size)
{
	/*
	 * Don't use vy_quota_do_unuse(), because it affects
	 * the rate limit state.
	 */
	assert(q->used >= size);
	q->used -= size;
	vy_quota_signal(q);
}

int
vy_quota_use(struct vy_quota *q, enum vy_quota_consumer_type type,
	     size_t size, double timeout)
{
	/*
	 * Fail early if the configured memory limit never allows
	 * us to commit the transaction.
	 */
	if (size > q->limit) {
		diag_set(OutOfMemory, size, "lsregion", "vinyl transaction");
		return -1;
	}

	q->n_blocked++;
	ERROR_INJECT_YIELD(ERRINJ_VY_QUOTA_DELAY);
	q->n_blocked--;

	/*
	 * Proceed only if there is enough quota available *and*
	 * the wait queue is empty. The latter is necessary to ensure
	 * fairness and avoid starvation among fibers queued earlier.
	 */
	if (rlist_empty(&q->wait_queue[type]) &&
	    vy_quota_may_use(q, type, size)) {
		vy_quota_do_use(q, type, size);
		return 0;
	}

	/* Wait for quota. */
	double wait_start = ev_monotonic_now(loop());
	struct vy_quota_wait_node wait_node = {
		.fiber = fiber(),
		.size = size,
		.ticket = ++q->wait_ticket,
	};
	rlist_add_tail_entry(&q->wait_queue[type], &wait_node, in_wait_queue);
	q->n_blocked++;
	bool timed_out = fiber_yield_timeout(timeout);
	q->n_blocked--;
	rlist_del_entry(&wait_node, in_wait_queue);

	if (timed_out) {
		diag_set(ClientError, ER_VY_QUOTA_TIMEOUT);
		return -1;
	}

	double wait_time = ev_monotonic_now(loop()) - wait_start;
	if (wait_time > q->too_long_threshold) {
		say_warn_ratelimited("waited for %zu bytes of vinyl memory "
				     "quota for too long: %.3f sec", size,
				     wait_time);
	}

	vy_quota_do_use(q, type, size);
	/*
	 * Blocked consumers are awaken one by one to preserve
	 * the order they were put to sleep. It's a responsibility
	 * of a consumer that managed to acquire the requested
	 * amount of quota to wake up the next one in the line.
	 */
	vy_quota_signal(q);
	return 0;
}

void
vy_quota_adjust(struct vy_quota *q, enum vy_quota_consumer_type type,
		size_t reserved, size_t used)
{
	if (reserved > used) {
		vy_quota_do_unuse(q, type, reserved - used);
		vy_quota_signal(q);
	}
	if (reserved < used) {
		vy_quota_do_use(q, type, used - reserved);
		vy_quota_check_limit(q);
	}
}
