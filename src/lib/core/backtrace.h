/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#include "trivia/util.h"

#include "libunwind.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/*
 * Format for printing C/C++ frames.
 */
#define C_FRAME_STR_FMT "#%-2d %p in %s+%zu"

enum {
	/* Maximal number of frames collected. */
	BACKTRACE_FRAME_COUNT_MAX = 64,
};

/*
 * C/C++ frame information sufficient for further resolving of function name
 * and offset.
 */
struct backtrace_frame {
	/* Value of IP register. */
	void *ip;
};

/*
 * Collection of C/C++ frames.
 */
struct backtrace {
	/* Number of frames collected. */
	int frame_count;
	/* Array of frames to be further resolved. */
	struct backtrace_frame frames[BACKTRACE_FRAME_COUNT_MAX];
};

struct fiber;

/*
 * Collect call stack of `fiber` (only C/C++ frames) to `bt`.
 *
 * It is guaranteed that if NULL passed as fiber argument, fiber
 * module will not be used.
 * `skip_frames` determines the number of frames skipped, starting from the
 * frame of `backtrace_collect`. It is expected to have a non-negative value.
 * For example, if skip_frames is 0, then the backtrace will contain
 * `backtrace_collect` and every function on the call stack up to the limit.
 * If it is 1, then it will skip `backtrace_collect`. If it is 2, then it will
 * skip `backtrace_collect` and the function the first function on the call
 * stack. Etc.
 *
 * Nota bene: requires its own stack frame — hence, NOINLINE.
 */
NOINLINE void
backtrace_collect(struct backtrace *bt, const struct fiber *fiber,
		  int skip_frames);

/*
 * Resolve C/C++ function name and `offset` from `frame`.
 *
 * Returns pointer to a temporary buffer storing the demangled function name:
 * the caller is responsible for making a copy of it.
 */
const char *
backtrace_frame_resolve(const struct backtrace_frame *frame,
			unw_word_t *offset);

/*
 * Dump collected C/C++ frames to `buf`, is `SNPRINT`-compatible
 * (see src/trivia/util.h for details).
 *
 * Returns the number of characters the backtrace string takes, or a negative
 * value in case of failure.
 */
int
backtrace_snprint(char *buf, int buf_len, const struct backtrace *bt);

/*
 * Print collected C/C++ frames to `fd`.
 */
void
backtrace_print(const struct backtrace *bt, int fd);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
#endif /* ENABLE_BACKTRACE */
