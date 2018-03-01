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

#include "trivia/util.h"
#include "memory.h"
#include "histogram.h"

#include <assert.h>

struct histogram *
histogram_new(const int64_t *buckets, size_t n_buckets)
{
	struct histogram *hist;

	assert(n_buckets > 0);
	for (size_t i = 1; i < n_buckets; i++)
		assert(buckets[i - 1] < buckets[i]);

	hist = malloc(sizeof(*hist) + n_buckets * sizeof(*hist->buckets));
	if (hist == NULL)
		return NULL;

	hist->max = buckets[n_buckets - 1];
	hist->total = 0;
	hist->n_buckets = n_buckets;

	for (size_t i = 0; i < n_buckets; i++) {
		hist->buckets[i].count = 0;
		hist->buckets[i].max = buckets[i];
	}
	return hist;
}

void
histogram_delete(struct histogram *hist)
{
	free(hist);
}

void
histogram_reset(struct histogram *hist)
{
	hist->total = 0;
	for (size_t i = 0; i < hist->n_buckets; i++)
		hist->buckets[i].count = 0;
	hist->max = hist->buckets[hist->n_buckets - 1].max;
}

static struct histogram_bucket *
histogram_lookup_bucket(struct histogram *hist, int64_t val)
{
	size_t begin, end, mid;
	struct histogram_bucket *bucket;

	begin = 0;
	end = hist->n_buckets - 1;
	while (1) {
		if (begin == end) {
			bucket = &hist->buckets[begin];
			if (val > bucket->max)
				bucket = &hist->buckets[end];
			break;
		} else {
			mid = begin / 2 + end / 2;
			bucket = &hist->buckets[mid];
		}

		if (val > hist->buckets[mid].max)
			begin = mid + 1;
		else
			end = mid;
	};

	if (val <= bucket->max)
		return bucket;
	return NULL;
}

void
histogram_collect(struct histogram *hist, int64_t val)
{
	struct histogram_bucket *bucket;

	bucket = histogram_lookup_bucket(hist, val);
	if (bucket != NULL)
		bucket->count++;
	if (hist->max < val)
		hist->max = val;
	hist->total++;
}

void
histogram_discard(struct histogram *hist, int64_t val)
{
	struct histogram_bucket *bucket;

	bucket = histogram_lookup_bucket(hist, val);
	if (bucket != NULL) {
		assert(bucket->count > 0);
		bucket->count--;
	}
	assert(hist->total > 0);
	hist->total--;
}

int64_t
histogram_percentile(struct histogram *hist, int pct)
{
	size_t count = 0;

	for (size_t i = 0; i < hist->n_buckets; i++) {
		struct histogram_bucket *bucket = &hist->buckets[i];
		count += bucket->count;
		if (count * 100 > hist->total * pct)
			return bucket->max;
	}
	return hist->max;
}

int
histogram_snprint(char *buf, int size, struct histogram *hist)
{
	int total = 0;
	bool first = true;

	for (size_t i = 0; i < hist->n_buckets; i++) {
		int64_t count = hist->buckets[i].count;
		if (count == 0)
			continue;

		int64_t min = (i > 0) ? hist->buckets[i - 1].max + 1 : 0;
		int64_t max = hist->buckets[i].max;

		if (!first)
			SNPRINT(total, snprintf, buf, size, " ");

		SNPRINT(total, snprintf, buf, size, "[%"PRIi64, min);
		if (max != min)
			SNPRINT(total, snprintf, buf, size, "-%"PRIi64, max);
		SNPRINT(total, snprintf, buf, size, "]:%"PRIi64, count);

		first = false;
	}
	return total;
}
