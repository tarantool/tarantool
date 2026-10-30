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
#include "latency.h"

#include <stdint.h>

#include "histogram.h"
#include "trivia/util.h"

enum {
	USEC_PER_MSEC		= 1000,
	USEC_PER_SEC		= 1000000,
};

int
latency_create(struct latency *latency)
{
	enum { US = 1, MS = USEC_PER_MSEC, S = USEC_PER_SEC };
	static int64_t buckets[] = {
		  1 * US,   2 * US,   3 * US,   4 * US,   5 * US,   6 * US,
		  7 * US,   8 * US,   9 * US,
		 10 * US,  20 * US,  30 * US,  40 * US,  50 * US,  60 * US,
		 70 * US,  80 * US,  90 * US,
		100 * US, 200 * US, 300 * US, 400 * US, 500 * US, 600 * US,
		700 * US, 800 * US, 900 * US,
		  1 * MS,   2 * MS,   3 * MS,   4 * MS,   5 * MS,   6 * MS,
		  7 * MS,   8 * MS,   9 * MS,
		 10 * MS,  20 * MS,  30 * MS,  40 * MS,  50 * MS,  60 * MS,
		 70 * MS,  80 * MS,  90 * MS,
		100 * MS, 200 * MS, 300 * MS, 400 * MS, 500 * MS, 600 * MS,
		700 * MS, 800 * MS, 900 * MS,
		  1 * S,    2 * S,    3 * S,    4 * S,    5 * S,    6 * S,
		  7 * S,    8 * S,    9 * S,   10 * S,
	};

	latency->histogram = histogram_new(buckets, lengthof(buckets));
	if (latency->histogram == NULL)
		return -1;

	histogram_collect(latency->histogram, 0);
	return 0;
}

void
latency_destroy(struct latency *latency)
{
	histogram_delete(latency->histogram);
}

void
latency_reset(struct latency *latency)
{
	histogram_reset(latency->histogram);
	histogram_collect(latency->histogram, 0);
}

void
latency_collect(struct latency *latency, double value)
{
	int64_t value_usec = value * USEC_PER_SEC;
	histogram_collect(latency->histogram, value_usec);
}

double
latency_get(struct latency *latency, int pct)
{
	int64_t value_usec = histogram_percentile(latency->histogram, pct);
	return (double)value_usec / USEC_PER_SEC;
}
