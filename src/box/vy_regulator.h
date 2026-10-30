#ifndef INCLUDES_TARANTOOL_BOX_VY_REGULATOR_H
#define INCLUDES_TARANTOOL_BOX_VY_REGULATOR_H
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

#include <stdbool.h>
#include <stddef.h>
#include <tarantool_ev.h>

#include "vy_stat.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct histogram;
struct vy_quota;
struct vy_regulator;

typedef int
(*vy_trigger_dump_f)(struct vy_regulator *regulator);

/**
 * The regulator is supposed to keep track of vinyl memory usage
 * and dump/compaction progress and adjust transaction write rate
 * accordingly.
 */
struct vy_regulator {
	/**
	 * Pointer to a quota object that is used to control
	 * memory usage.
	 */
	struct vy_quota *quota;
	/**
	 * Called when the regulator detects that memory usage
	 * exceeds the computed watermark. Supposed to trigger
	 * memory dump and return 0 on success, -1 on failure.
	 */
	vy_trigger_dump_f trigger_dump_cb;
	/**
	 * Periodic timer that updates the memory watermark
	 * basing on accumulated statistics.
	 */
	ev_timer timer;
	/**
	 * Average rate at which transactions are writing to
	 * the database, in bytes per second.
	 */
	size_t write_rate;
	/**
	 * Max write rate observed since the last time when
	 * memory dump was triggered, in bytes per second.
	 */
	size_t write_rate_max;
	/**
	 * Amount of memory that was used when the timer was
	 * executed last time. Needed to update @write_rate.
	 */
	size_t quota_used_last;
	/**
	 * Current dump bandwidth estimate, in bytes per second.
	 * See @dump_bandwidth_hist for more details.
	 */
	size_t dump_bandwidth;
	/**
	 * Dump bandwidth is needed for calculating the watermark.
	 * The higher the bandwidth, the later we can start dumping
	 * w/o suffering from transaction throttling. So we want to
	 * be very conservative about estimating the bandwidth.
	 *
	 * To make sure we don't overestimate it, we maintain a
	 * histogram of all observed measurements and assume the
	 * bandwidth to be equal to the 10th percentile, i.e. the
	 * best result among 10% worst measurements.
	 */
	struct histogram *dump_bandwidth_hist;
	/**
	 * Memory watermark. Exceeding it does not result in
	 * throttling new transactions, but it does trigger
	 * background memory reclaim.
	 */
	size_t dump_watermark;
	/**
	 * Set if the last triggered memory dump hasn't completed
	 * yet, i.e. trigger_dump_cb() was successfully invoked,
	 * but vy_regulator_dump_complete() hasn't been called yet.
	 */
	bool dump_in_progress;
	/**
	 * Snapshot of scheduler statistics taken at the time of
	 * the last rate limit update.
	 */
	struct vy_scheduler_stat sched_stat_last;
	/**
	 * Scheduler statistics for the most recent few dumps.
	 * Used for calculating the rate limit.
	 */
	struct vy_scheduler_stat sched_stat_recent;
};

void
vy_regulator_create(struct vy_regulator *regulator, struct vy_quota *quota,
		    vy_trigger_dump_f trigger_dump_cb);

void
vy_regulator_start(struct vy_regulator *regulator);

void
vy_regulator_destroy(struct vy_regulator *regulator);

/**
 * Check if memory usage is above the watermark and trigger
 * memory dump if so.
 */
void
vy_regulator_check_dump_watermark(struct vy_regulator *regulator);

/**
 * Called when the memory limit is hit by a quota consumer.
 */
void
vy_regulator_quota_exceeded(struct vy_regulator *regulator);

/**
 * Notify the regulator about memory dump completion.
 */
void
vy_regulator_dump_complete(struct vy_regulator *regulator,
			   size_t mem_dumped, double dump_duration);

/**
 * Set memory limit and update the dump watermark accordingly.
 */
void
vy_regulator_set_memory_limit(struct vy_regulator *regulator, size_t limit);

/**
 * Reset dump bandwidth histogram and update initial estimate.
 * Called when box.cfg.snap_io_rate_limit is updated.
 */
void
vy_regulator_reset_dump_bandwidth(struct vy_regulator *regulator, size_t max);

/**
 * Called when global statistics are reset by box.stat.reset().
 */
void
vy_regulator_reset_stat(struct vy_regulator *regulator);

/**
 * Set transaction rate limit so as to ensure that compaction
 * will keep up with dumps.
 */
void
vy_regulator_update_rate_limit(struct vy_regulator *regulator,
			       const struct vy_scheduler_stat *stat,
			       int compaction_threads);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_REGULATOR_H */
