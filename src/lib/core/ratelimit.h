#ifndef TARANTOOL_LIB_CORE_RATELIMIT_H_INCLUDED
#define TARANTOOL_LIB_CORE_RATELIMIT_H_INCLUDED
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
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Rate limit state.
 */
struct ratelimit {
	/** Time interval used for rate limiting, in seconds. */
	double interval;
	/** Max number of events per interval. */
	int burst;
	/** Number of events emitted in the current interval. */
	int emitted;
	/** Number of events suppressed in the current interval. */
	int suppressed;
	/** Start time of the current interval. */
	double start;
};

/**
 * Rate limit state initializer.
 */
#define RATELIMIT_INITIALIZER(interval_init, burst_init) \
	{ (interval_init), (burst_init), 0, 0, 0 }

/**
 * Initialize a rate limit state.
 */
static inline void
ratelimit_create(struct ratelimit *rl, double interval, int burst)
{
	rl->interval = interval;
	rl->burst = burst;
	rl->emitted = 0;
	rl->suppressed = 0;
	rl->start = 0;
}

/**
 * Check if an event may be emitted. Returns true on success.
 * @now is the current time.
 *
 * If the current interval is over, the total number of events
 * suppressed in it is added to @suppressed.
 */
static inline bool
ratelimit_check(struct ratelimit *rl, double now, int *suppressed)
{
	if (now > rl->start + rl->interval) {
		/* Current interval is over, reset counters. */
		*suppressed += rl->suppressed;
		rl->emitted = 0;
		rl->suppressed = 0;
		rl->start = now;
	}
	if (rl->emitted < rl->burst) {
		rl->emitted++;
		return true;
	} else {
		rl->suppressed++;
		return false;
	}
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_LIB_CORE_RATELIMIT_H_INCLUDED */
