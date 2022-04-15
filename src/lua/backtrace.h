/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#include "core/backtrace.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
enum {
	/* Maximal length of a Lua function  name. */
	BACKTRACE_LUA_PROC_NAME_LEN_MAX = 64,
	/* Maximal length of a Lua source file name. */
	BACKTRACE_LUA_SRC_NAME_LEN_MAX = 64,
	/* Maximal number of Lua frames collected. */
	BACKTRACE_LUA_FRAME_COUNT_MAX = 64,
};

/*
 * Lua frame information, derived from `struct backtrace frame`.
 */
struct backtrace_lua_frame {
	union {
		/* Basic frame type. */
		struct backtrace_frame c;
		/* Derived frame type. */
		struct {
			/* Lua function name. */
			char proc_name[BACKTRACE_LUA_PROC_NAME_LEN_MAX];
			/* Lua source file name. */
			char src_name[BACKTRACE_LUA_SRC_NAME_LEN_MAX];
			/* Line number inside Lua function. */
			int line_no;
		} lua;
	};
	enum backtrace_lua_frame_type {
		BACKTRACE_LUA_FRAME_C,
		BACKTRACE_LUA_FRAME_LUA,
	} type;
};

/*
 * Collection of Lua frames.
 */
struct backtrace_lua {
	/* Number of Lua frames collected. */
	int frame_count;
	/* Array of Lua frames. */
	struct backtrace_lua_frame frames[BACKTRACE_LUA_FRAME_COUNT_MAX];
};

struct fiber;
struct lua_State;

/*
 * Initialize Lua backtrace: setup Lua stack entry boundaries.
 */
void
backtrace_lua_init(void);

/*
 * Collect call stack of `fiber` (both C/C++ and Lua frames) to `bt`.
 *
 * `skip_frames` determines the number of frames skipped, starting from the
 * frame of `backtrace_lua_collect`. It is expected to have a non-negative
 * value.
 */
void
backtrace_lua_collect(struct backtrace_lua *bt_lua, struct fiber *fiber,
		      int skip_frames);

/*
 * Push collected Lua backtrace onto Lua stack.
 */
void
backtrace_lua_stack_push(const struct backtrace_lua *bt, struct lua_State *L);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
#endif /* ENABLE_BACKTRACE */
