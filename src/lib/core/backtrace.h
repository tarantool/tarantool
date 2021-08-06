#ifndef TARANTOOL_LIB_CORE_BACKTRACE_H_INCLUDED
#define TARANTOOL_LIB_CORE_BACKTRACE_H_INCLUDED
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
#include "trivia/config.h"
#include "trivia/util.h"
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifdef ENABLE_BACKTRACE
#include <coro.h>

enum {
	/** Maximal size of  of Lua frame's name and source name. */
	BACKTRACE_LUA_MAX_NAME = 64,
	/** Maximal number of frames in @a backtrace structure. */
	BACKTRACE_MAX_FRAMES = 32,
};

/**
 * Frame information common for C and Lua
 * sufficient for further resolving of names and offsets.
 */
struct backtrace_frame {
	/** True iff frame is for lua code. */
	bool is_lua;
	union {
		/** C frame RIP register value */
		void *rip;
		/** Lua frame */
		struct {
			/** Current line inside lua function */
			int line;
			/** Lua function name */
			char name[BACKTRACE_LUA_MAX_NAME];
			/** Lua frame source */
			char source[BACKTRACE_LUA_MAX_NAME];
		};
	};
};

/**
 * Backtrace context storing a 'snapshot' of a
 * fixed number of last stack frames.
 */
struct backtrace {
    /** Number of frames. */
    int frames_count;
    /** Buffer of Lua frame data. */
    struct backtrace_frame frames[BACKTRACE_MAX_FRAMES];
};

/**
 * Forward declaration only to use pointer to fiber.
 */
struct fiber;

char *
backtrace(char *start, size_t size);

void print_backtrace(void);

/**
 * Callback to be called on each C/C++ frame of the backtrace.
 * Note that @a frameno counter in @a backtrace_foreach and
 * @a backtrace_foreach_current is common for both C/C++ and Lua callbacks
 * @sa backtrace_foreach_current
 * @sa backtrace_foreach
 */
typedef int (backtrace_cxx_cb)(int frameno, void *frameret, const char *func,
			       size_t offset, void *cb_ctx);

/**
 * Callback to be called on each Lua frame of the backtrace.
 * Note that @a frameno counter in @a backtrace_foreach and
 * @a backtrace_foreach_current is common for both C/C++ and Lua callbacks
 * @sa backtrace_foreach_current
 * @sa backtrace_foreach
 */
typedef int (backtrace_lua_cb)(int frameno, const char *func,
			       const char *source, int line, void *cb_ctx);

/**
 * Run @a cxx_cb and @a lua_cb on each C/C++ and Lua frame of a current
 * context of @a fiber correspondingly.
 * @a lua_cb may be NULL when tracing only C frames.
 * @returns number of frames traversed
 */
void NOINLINE
backtrace_foreach_current(backtrace_cxx_cb cxx_cb, backtrace_lua_cb lua_cb,
			  struct fiber *fiber, void *cb_ctx);

void
backtrace_proc_cache_clear(void);

/**
 * Callback used to collect lua frames.
 */
typedef int (*backtrace_collect_lua_cb)(struct backtrace *, struct fiber *);

/**
 * Default callback to collect lua frames in backtrace.
 */
int
backtrace_collect_lua_default(struct backtrace *bt, struct fiber *fiber);

/**
 * Initialize helpers to track Lua stack starting
 * from @a lua_stack_entry_point procedure's frame.
 * Do not track lua stack if @a lua_stack_entry_point is NULL.
 */
int
backtrace_init(void *lua_stack_entry_point,
	       backtrace_collect_lua_cb collect_lua_cb);

/**
 * Collect current stack snapshot of a current context of @a fiber to @a bt.
 * Do not collect its own (backtrace_collect()'s) frame.
 * @returns number of frames collected
 */
int NOINLINE
backtrace_collect(struct backtrace *bt, struct fiber *fiber);

/**
 * Run @a cxx_cb and @a lua_cb on each C/C++ and Lua frame of @a bt
 * correspondingly.
 * @returns number of frames traversed
 */
int
backtrace_foreach(const struct backtrace *bt, backtrace_cxx_cb cxx_cb,
		  backtrace_lua_cb lua_cb, void *cb_ctx);

/**
 * Append frames from @a add to the bottom of @a to.
 */
int
backtrace_concat(struct backtrace *to, const struct backtrace *add);

/**
 * Append C/C++ frame bt the botbtm of @a bt.
 */
int
backtrace_append_cxx(struct backtrace *bt, void *rip);

/**
 * Append Lua frame to the bottom of @a bt.
 */
int
backtrace_append_lua(struct backtrace *bt, const char *name, const char *source,
		     int line);

#endif /* ENABLE_BACKTRACE */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_BACKTRACE_H_INCLUDED */
