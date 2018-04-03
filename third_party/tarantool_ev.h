#ifndef TARANTOOL_EV_H_INCLUDED
#define TARANTOOL_EV_H_INCLUDED
/*
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
#include "trivia/config.h"
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define EV_MULTIPLICITY 1
#define EV_COMPAT3 0

#if defined(ENABLE_BUNDLED_LIBEV)
#define EV_STANDALONE 1
#define EV_USE_SELECT 1
#define EV_USE_POLL 1
#define EV_USE_NANOSLEEP 1
#define EV_PERIODIC_ENABLE 1
#define EV_IDLE_ENABLE 1
#define EV_STAT_ENABLE 1
#define EV_FORK_ENABLE 1
#define EV_CONFIG_H 0
#define EV_USE_FLOOR 1
#ifdef HAVE_CLOCK_GETTIME_DECL
# define EV_USE_REALTIME 1
# define EV_USE_MONOTONIC 1
#endif
#include "third_party/libev/ev.h"
#else /* !defined(ENABLE_BUNDLED_LIBEV) */
#include <ev.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern const ev_tstamp TIMEOUT_INFINITY;

typedef void (*ev_io_cb)(ev_loop *, ev_io *, int);
typedef void (*ev_async_cb)(ev_loop *, ev_async *, int);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_EV_H_INCLUDED */
