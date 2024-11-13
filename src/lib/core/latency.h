#ifndef TARANTOOL_LATENCY_H_INCLUDED
#define TARANTOOL_LATENCY_H_INCLUDED
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

struct histogram;

/**
 * Latency counter.
 */
struct latency {
	/**
	 * Histogram of all latency observations,
	 * in microseconds.
	 */
	struct histogram *histogram;
};

/**
 * Initialize a latency counter.
 * Return 0 on success, -1 on OOM.
 */
int
latency_create(struct latency *latency);

/**
 * Destroy a latency counter.
 */
void
latency_destroy(struct latency *latency);

/**
 * Reset a latency counter.
 */
void
latency_reset(struct latency *latency);

/**
 * Update a latency counter with a new observation.
 * @value is the observed latency value, in seconds.
 */
void
latency_collect(struct latency *latency, double value);

/**
 * Get accumulated latency value, in seconds.
 * Returns @pct-th percentile of all observations.
 */
double
latency_get(struct latency *latency, int pct);

#endif /* TARANTOOL_LATENCY_H_INCLUDED */
