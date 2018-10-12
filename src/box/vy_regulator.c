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
#include "vy_regulator.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tarantool_ev.h>

#include "fiber.h"
#include "histogram.h"
#include "say.h"
#include "trivia/util.h"

#include "vy_quota.h"

/**
 * Regulator timer period, in seconds.
 */
static const double VY_REGULATOR_TIMER_PERIOD = 1;

/**
 * Time window over which the write rate is averaged,
 * in seconds.
 */
static const double VY_WRITE_RATE_AVG_WIN = 5;

/**
 * Histogram percentile used for estimating dump bandwidth.
 * For details see the comment to vy_regulator::dump_bandwidth_hist.
 */
static const int VY_DUMP_BANDWIDTH_PCT = 10;

/*
 * Until we dump anything, assume bandwidth to be 10 MB/s,
 * which should be fine for initial guess.
 */
static const size_t VY_DUMP_BANDWIDTH_DEFAULT = 10 * 1024 * 1024;

/**
 * Do not take into account small dumps when estimating dump
 * bandwidth, because they have too high overhead associated
 * with file creation.
 */
static const size_t VY_DUMP_SIZE_ACCT_MIN = 1024 * 1024;

static void
vy_regulator_trigger_dump(struct vy_regulator *regulator)
{
	if (regulator->dump_in_progress)
		return;

	if (regulator->trigger_dump_cb(regulator) != 0)
		return;

	regulator->dump_in_progress = true;
}

static void
vy_regulator_update_write_rate(struct vy_regulator *regulator)
{
	size_t used_curr = regulator->quota->used;
	size_t used_last = regulator->quota_used_last;

	/*
	 * Memory can be dumped between two subsequent timer
	 * callback invocations, in which case memory usage
	 * will decrease. Ignore such observations - it's not
	 * a big deal, because dump is a rare event.
	 */
	if (used_curr < used_last) {
		regulator->quota_used_last = used_curr;
		return;
	}

	size_t rate_avg = regulator->write_rate;
	size_t rate_curr = (used_curr - used_last) / VY_REGULATOR_TIMER_PERIOD;

	double weight = 1 - exp(-VY_REGULATOR_TIMER_PERIOD /
				VY_WRITE_RATE_AVG_WIN);
	rate_avg = (1 - weight) * rate_avg + weight * rate_curr;

	regulator->write_rate = rate_avg;
	regulator->quota_used_last = used_curr;
}

static void
vy_regulator_update_dump_watermark(struct vy_regulator *regulator)
{
	struct vy_quota *quota = regulator->quota;

	/*
	 * Due to log structured nature of the lsregion allocator,
	 * which is used for allocating statements, we cannot free
	 * memory in chunks, only all at once. Therefore we should
	 * configure the watermark so that by the time we hit the
	 * limit, all memory have been dumped, i.e.
	 *
	 *   limit - watermark      watermark
	 *   ----------------- = --------------
	 *       write_rate      dump_bandwidth
	 */
	regulator->dump_watermark =
			(double)quota->limit * regulator->dump_bandwidth /
			(regulator->dump_bandwidth + regulator->write_rate + 1);
}

static void
vy_regulator_timer_cb(ev_loop *loop, ev_timer *timer, int events)
{
	(void)loop;
	(void)events;

	struct vy_regulator *regulator = timer->data;

	vy_regulator_update_write_rate(regulator);
	vy_regulator_update_dump_watermark(regulator);
	vy_regulator_check_dump_watermark(regulator);
}

void
vy_regulator_create(struct vy_regulator *regulator, struct vy_quota *quota,
		    vy_trigger_dump_f trigger_dump_cb)
{
	enum { KB = 1024, MB = KB * KB };
	static int64_t dump_bandwidth_buckets[] = {
		100 * KB, 200 * KB, 300 * KB, 400 * KB, 500 * KB, 600 * KB,
		700 * KB, 800 * KB, 900 * KB,   1 * MB,   2 * MB,   3 * MB,
		  4 * MB,   5 * MB,   6 * MB,   7 * MB,   8 * MB,   9 * MB,
		 10 * MB,  15 * MB,  20 * MB,  25 * MB,  30 * MB,  35 * MB,
		 40 * MB,  45 * MB,  50 * MB,  55 * MB,  60 * MB,  65 * MB,
		 70 * MB,  75 * MB,  80 * MB,  85 * MB,  90 * MB,  95 * MB,
		100 * MB, 200 * MB, 300 * MB, 400 * MB, 500 * MB, 600 * MB,
		700 * MB, 800 * MB, 900 * MB,
	};
	regulator->dump_bandwidth_hist = histogram_new(dump_bandwidth_buckets,
					lengthof(dump_bandwidth_buckets));
	if (regulator->dump_bandwidth_hist == NULL)
		panic("failed to allocate dump bandwidth histogram");

	regulator->quota = quota;
	regulator->trigger_dump_cb = trigger_dump_cb;
	ev_timer_init(&regulator->timer, vy_regulator_timer_cb, 0,
		      VY_REGULATOR_TIMER_PERIOD);
	regulator->timer.data = regulator;
	regulator->write_rate = 0;
	regulator->quota_used_last = 0;
	regulator->dump_bandwidth = VY_DUMP_BANDWIDTH_DEFAULT;
	regulator->dump_watermark = SIZE_MAX;
	regulator->dump_in_progress = false;
}

void
vy_regulator_start(struct vy_regulator *regulator)
{
	regulator->quota_used_last = regulator->quota->used;
	ev_timer_start(loop(), &regulator->timer);
}

void
vy_regulator_destroy(struct vy_regulator *regulator)
{
	ev_timer_stop(loop(), &regulator->timer);
	histogram_delete(regulator->dump_bandwidth_hist);
}

void
vy_regulator_quota_exceeded(struct vy_regulator *regulator)
{
	vy_regulator_trigger_dump(regulator);
}

void
vy_regulator_check_dump_watermark(struct vy_regulator *regulator)
{
	if (regulator->quota->used >= regulator->dump_watermark)
		vy_regulator_trigger_dump(regulator);
}

void
vy_regulator_dump_complete(struct vy_regulator *regulator,
			   size_t mem_dumped, double dump_duration)
{
	regulator->dump_in_progress = false;

	if (mem_dumped >= VY_DUMP_SIZE_ACCT_MIN && dump_duration > 0) {
		histogram_collect(regulator->dump_bandwidth_hist,
				  mem_dumped / dump_duration);
		/*
		 * To avoid unpredictably long stalls caused by
		 * mispredicting dump time duration, we need to
		 * know the worst (smallest) dump bandwidth so
		 * use a lower-bound percentile estimate.
		 */
		regulator->dump_bandwidth = histogram_percentile_lower(
			regulator->dump_bandwidth_hist, VY_DUMP_BANDWIDTH_PCT);
	}
}

void
vy_regulator_reset_dump_bandwidth(struct vy_regulator *regulator, size_t max)
{
	histogram_reset(regulator->dump_bandwidth_hist);
	regulator->dump_bandwidth = VY_DUMP_BANDWIDTH_DEFAULT;
	if (max > 0 && regulator->dump_bandwidth > max)
		regulator->dump_bandwidth = max;
}
