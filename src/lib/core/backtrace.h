/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/config.h"
#include "trivia/util.h"

#include "libunwind.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#ifdef ENABLE_BACKTRACE
/*
 * Format for printing Core frames.
 */
extern const char *const core_frame_str_fmt;

enum {
	/* Maximal number of Core frames collected. */
	CORE_BACKTRACE_FRAME_COUNT_MAX = 64,
};

/*
 * Core frame information sufficient for further resolving of function name
 * and offset.
 */
struct core_frame {
	/* Value of IP register. */
	void *ip;
};

/*
 * Core frame information sufficient for further processing.
 */
struct core_resolved_frame {
	/* Core frame information. */
	struct core_frame core_frame;
	/* Ordinal number of frame. */
	int no;
	/* Core function name. */
	const char *proc_name;
	/* Core function offset. */
	size_t offset;
};

/*
 * Collection of Core frames.
 */
struct core_backtrace {
	/* Number of Core frames collected. */
	int frame_count;
	/* Array of frames to be resolved. */
	struct core_frame frames[CORE_BACKTRACE_FRAME_COUNT_MAX];
};

struct fiber;

/*
 * Callback for processing Core frames.
 *
 * Returns status:
 * 0 - continue processing.
 * -1 - stop processing
 */
typedef int (core_resolved_frame_cb)(const struct core_resolved_frame *,
				      void *);

/*
 * Collect call stack of `fiber` (only C/C++ frames) to `bt`.
 *
 * Nota bene: requires its own stack frame — hence, NOINLINE.
 */
NOINLINE void
core_backtrace_collect_frames(struct core_backtrace *bt,
			      const struct fiber *fiber,
			      int skip_frames /* = 2 by default. */);

/*
 * Resolve function name and offset based on `ip`.
 *
 * An additional buffer is needed for demangling C++ names, see `output_buf`
 * and `length` parameters of `abi::__cxa_demangle`: https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html.
 *
 * The caller is responsible for freeing the pointer stored in `demangle_buf`.
 * `demangle_buf` and `demangle_buf_len` can be reused for multiple calls.
 *
 * Returns pointer to a temporary buffer storing the resolved function name:
 * the caller is responsible for making a copy of it. The returned pointer may
 * coincide with the one stored in `demangle_buf`: hence, `demangle_buf` must be
 * freed only after making a copy.
 */
const char *
core_backtrace_resolve_frame(unw_word_t ip, unw_word_t *offset,
			     char **demangle_buf, size_t *demangle_buf_len);

/*
 * Process collected Core frames.
 */
void
core_backtrace_foreach(const struct core_backtrace *bt, int begin, int end,
		       core_resolved_frame_cb core_resolved_frame_cb,
		       void *ctx);

/*
 * Dump collected frames to `buf`.
 */
void
core_backtrace_dump_frames(const struct core_backtrace *bt, char *buf,
			   size_t buf_len);
#endif /* ENABLE_BACKTRACE */
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
