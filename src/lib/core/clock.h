#ifndef TARANTOOL_LIB_CORE_CLOCK_H_INCLUDED
#define TARANTOOL_LIB_CORE_CLOCK_H_INCLUDED
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


#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

/**
 * A settable system-wide clock that measures real (i.e.,
 * wall-clock) time.
 *
 * \sa clock_gettime(2), CLOCK_REALTIME.
 */
double
clock_realtime(void);

/**
 * A nonsettable system-wide clock that represents monotonic time.
 *
 * \sa clock_gettime(2), CLOCK_MONOTONIC.
 */
double
clock_monotonic(void);

/**
 * A clock that measures CPU time consumed by this process (by all
 * threads in the process).
 *
 * \sa clock_gettime(2), CLOCK_PROCESS_CPUTIME_ID.
 */
double
clock_process(void);

/**
 * A clock that measures CPU time consumed by this thread.
 *
 * \sa clock_gettime(2), CLOCK_THREAD_CPUTIME_ID.
 */
double
clock_thread(void);

/**
 * Same as clock_realtime(), but returns the time as 64 bit
 * signed integer.
 */
int64_t
clock_realtime64(void);

/**
 * Same as clock_monotonic(), but returns the time as 64 bit
 * signed integer.
 */
int64_t
clock_monotonic64(void);

/**
 * Same as clock_process(), but returns the time as 64 bit
 * signed integer.
 */
int64_t
clock_process64(void);

/**
 * Same as clock_thread(), but returns the time as 64 bit
 * signed integer.
 */
int64_t
clock_thread64(void);

/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_CLOCK_H_INCLUDED */
