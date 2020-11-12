#ifndef INCLUDES_TARANTOOL_TEST_PERF_H
#define INCLUDES_TARANTOOL_TEST_PERF_H
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdio.h>
#include <time.h>

#include "tt_static.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct cur_time {
	struct timespec wall_time;
	struct timespec cpu_time;
};

struct perf_time {
	double wall_time;
	double cpu_time;
};

static inline void
perf_print(struct perf_time time)
{
	printf("wall_time: %lf; cpu_time: %lf\n", time.wall_time,
						  time.cpu_time);
}

static inline struct perf_time
perf_init(void)
{
	struct perf_time perf_time;
	perf_time.wall_time = 0;
	perf_time.cpu_time = 0;
	return perf_time;
}

static inline void
perf_add_time(struct perf_time *sum, struct perf_time time)
{
	sum->wall_time += time.wall_time;
	sum->cpu_time += time.cpu_time;
}

static inline struct cur_time
perf_get_time(struct timespec *cpu_time)
{
	struct cur_time cur;
	clock_gettime(CLOCK_REALTIME, &cur.wall_time);
	if (cpu_time != NULL)
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, cpu_time);
	else
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cur.cpu_time);
	return cur;
}

static inline struct perf_time
perf_count(struct cur_time time)
{
	struct cur_time cur = perf_get_time(&time.cpu_time);
	struct perf_time diff;
	diff.wall_time = (cur.wall_time.tv_sec - time.wall_time.tv_sec) +
			 (cur.wall_time.tv_nsec - time.wall_time.tv_nsec) * 1e-9;
	diff.cpu_time = time.cpu_time.tv_sec + time.cpu_time.tv_nsec * 1e-9;
	return diff;
}

static inline char *
perf_json_result(const char *meta, const char *meas_val, float res)
{
	tt_sprintf("{\"meta\" : \"%s\", \"measurementValue\" : \"%s\","
		   "\"result\" : %f}", meta, meas_val, res);
}

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_TEST_PERF_H */
