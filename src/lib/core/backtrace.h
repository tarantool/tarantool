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
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifdef ENABLE_BACKTRACE
#include <coro.h>

enum {
	/* Maximal length of a Lua function or source file name. */
	BACKTRACE_LUA_LEN_MAX = 64,
	/* Maximal number of frames in 'struct backtrace'. */
	BACKTRACE_FRAMES_CNT_MAX = 64,
};

/*
 * Each language used in our runtime can have its own specific frame
 * information.
 */
enum {
	BACKTRACE_FRAME_TYPE_C,
	BACKTRACE_FRAME_TYPE_LUA,
};

/*
 * C/C++ and Lua frame information sufficient for further resolving of function
 * names and locations.
 */
struct backtrace_frame {
	/* The language the frame came from. */
	int type;
	union {
		/** C/C++ frame IP register value */
		void *ip;
		/** Lua frame */
		struct {
			/* Current line inside Lua function. */
			int line;
			/* Lua function name. */
			char proc_name[BACKTRACE_LUA_LEN_MAX];
			/* Lua source file name. */
			char src_name[BACKTRACE_LUA_LEN_MAX];
		};
	};
};

/*
 * Context for storing an array of 'struct backtrace_frame's along with context
 * for pushing them to Lua.
 */
struct backtrace {
	/* Number of frames collected. */
	int frames_cnt;
	/* Array of frame data. */
	struct backtrace_frame frames[BACKTRACE_FRAMES_CNT_MAX];
	/* Lua stack to push values. */
	struct lua_State *L;
	/* Count of processed frames (both C/C++ and Lua). */
	int total_frame_cnt;
};

struct fiber;
struct backtrace;

char *
backtrace(char *start, size_t size);

void print_backtrace(void);

/*
 * Initialize backtrace subsystem: set callback for Lua frame collection.
 */
void
backtrace_init(void);

/*
 * Collect current stack snapshot of a current context of 'fiber' to 'bt'.
 *
 * Nota bene: requires its own stack frame — hence, NOINLINE.
 *
 * Returns the number of frames collected.
 */
void
backtrace_collect(struct backtrace *bt, struct fiber *fiber);

/*
 * Run 'c_cb' and 'lua_cb' on each C/C++ and Lua frame of 'bt' correspondingly.
 *
 * Returns the number of frames traversed.
 */
void
backtrace_foreach(struct backtrace *bt);

/*
 * Append C/C++ frame to the end of 'bt'.
 */
void
backtrace_append_c_frame(struct backtrace *bt, void *rip);

/*
 * Append Lua frame to the end of 'bt'.
 */
void
backtrace_append_lua_frame(struct backtrace *bt, const char *proc_name,
			   const char *src_name, int line_no);

#endif /* ENABLE_BACKTRACE */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_BACKTRACE_H_INCLUDED */
