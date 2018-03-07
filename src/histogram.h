#ifndef TARANTOOL_HISTOGRAM_H_INCLUDED
#define TARANTOOL_HISTOGRAM_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#if defined(__cplusplus)
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

struct histogram_bucket {
	int64_t max;
	size_t count;
};

struct histogram {
	int64_t max;
	size_t total;
	size_t n_buckets;
	struct histogram_bucket buckets[0];
};

/**
 * Create a new histogram given an array of bucket boundaries.
 * buckets[i] defines the upper bound for bucket i.
 */
struct histogram *
histogram_new(const int64_t *buckets, size_t n_buckets);

/**
 * Destroy a histogram.
 */
void
histogram_delete(struct histogram *hist);

/**
 * Reset a histogram.
 */
void
histogram_reset(struct histogram *hist);

/**
 * Update a histogram with a new observation.
 */
void
histogram_collect(struct histogram *hist, int64_t val);

/**
 * Remove a previously collected observation from a historam.
 */
void
histogram_discard(struct histogram *hist, int64_t val);

/**
 * Calculate a percentile, i.e. the value below which a given
 * percentage of observations fall.
 */
int64_t
histogram_percentile(struct histogram *hist, int pct);

/**
 * Print string representation of a histogram.
 */
int
histogram_snprint(char *buf, int size, struct histogram *hist);


#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_HISTOGRAM_H_INCLUDED */
