/*
 * Copyright 2010-2016 Tarantool AUTHORS: please see AUTHORS file.
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
#include "clock.h"

#include "trivia/util.h"
double
clock_realtime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (double) ts.tv_sec + ts.tv_nsec / 1e9;

}
double
clock_monotonic(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec + ts.tv_nsec / 1e9;
}
double
clock_process(void)
{
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	struct timespec ts;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return (double) ts.tv_sec + ts.tv_nsec / 1e9;
#else
	return (double) clock() / CLOCKS_PER_SEC;
#endif
}

double
clock_thread(void)
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double) ts.tv_sec + ts.tv_nsec / 1e9;
#else
	return (double) clock() / CLOCKS_PER_SEC;
#endif
}

uint64_t
clock_realtime64(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((uint64_t)ts.tv_sec) * 1000000000 + ts.tv_nsec;

}
uint64_t
clock_monotonic64(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec) * 1000000000 + ts.tv_nsec;
}
uint64_t
clock_process64(void)
{
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	struct timespec ts;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return ((uint64_t)ts.tv_sec) * 1000000000 + ts.tv_nsec;
#else
	return (uint64_t) clock() * 1000000000 / CLOCKS_PER_SEC;
#endif
}

uint64_t
clock_thread64(void)
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ((uint64_t)ts.tv_sec) * 1000000000 + ts.tv_nsec;
#else
	return (uint64_t) clock() * 1000000000 / CLOCKS_PER_SEC;
#endif
}
