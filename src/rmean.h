#ifndef TARANTOOL_RMEAN_H_INCLUDED
#define TARANTOOL_RMEAN_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stddef.h>
#include <stdint.h>

#include "trivia/util.h"
#include "third_party/tarantool_ev.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Rolling mean time window, in seconds. */
enum { RMEAN_WINDOW = 5 };

struct stats {
	const char *name;
	int64_t value[RMEAN_WINDOW + 1];
	int64_t total;
};

/**
 * Rolling average.
 */
struct rmean {
	ev_timer timer;
	int stats_n;
	double prev_ts;
	struct stats stats[0];
};

void
rmean_roll(int64_t *value, double dt);

int64_t
rmean_mean(int64_t *value);

struct rmean *
rmean_new(const char **name, size_t n);

void
rmean_delete(struct rmean *rmean);

void
rmean_cleanup(struct rmean *rmean);

void
rmean_timer_tick(struct rmean *rmean);

void
rmean_collect(struct rmean *rmean, size_t name, int64_t value);

typedef int (*rmean_cb)(const char *name, int rps, int64_t total, void *cb_ctx);

int
rmean_foreach(struct rmean *rmean, rmean_cb cb, void *cb_ctx);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_RMEAN_H_INCLUDED */
