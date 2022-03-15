/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#include "core/backtrace.h"
#include "core/say.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
enum {
	/* +2 for `backtrace_collect` and `backtrace_collect_current_stack` */
	/* Maximal length of a Lua function  name. */
	LUA_BACKTRACE_LUA_PROC_NAME_LEN_MAX = 64,
	/* Maximal length of a Lua source file name. */
	LUA_BACKTRACE_LUA_SRC_NAME_LEN_MAX = 64,
	/* Maximal number of Lua frames collected. */
	LUA_BACKTRACE_FRAME_COUNT_MAX = 8,
};

/*
 * Lua frame information.
 */
struct lua_frame {
	/* Ordinal number of frame. */
	int no;
	/* Lua function name. */
	char proc_name[LUA_BACKTRACE_LUA_PROC_NAME_LEN_MAX];
	/* Lua source file name. */
	char src_name[LUA_BACKTRACE_LUA_SRC_NAME_LEN_MAX];
	/* Line number inside Lua function. */
	int line_no;
};

/*
 * Context for collection of both C/C++ and Lua frames.
 */
struct lua_backtrace {
	/* Core backtrace information. */
	struct core_backtrace core_bt;
	/* Ordinal number of first Lua frame in backtrace. */
	int first_lua_frame_no;
	/* Number of Lua frames collected. */
	int frame_count;
	/* Array of Lua frame data. */
	struct lua_frame frames[LUA_BACKTRACE_FRAME_COUNT_MAX];
};

struct fiber;
struct lua_State;

/*
 * Callback for processing Lua frames.
 *
 * Returns status:
 * 0 - continue processing.
 * -1 - stop processing
 */
typedef int (lua_frame_cb)(const struct lua_frame *,
			    void *ctx);

#ifndef __APPLE__
/*
 * Initialize Lua backtrace: setup Lua stack entry boundaries.
 */
void
lua_backtrace_init(void);
#endif /* __APPLE__ */

/*
 * Collect call stack of `fiber` (both C/C++ and Lua frames) to `bt`.
 */
void
lua_backtrace_collect_frames(struct lua_backtrace *lua_bt, struct fiber *fiber);

/*
 * Process collected Lua frames.
 */
void
lua_backtrace_foreach(const struct lua_backtrace *lua_bt,
		      core_resolved_frame_cb core_resolved_frame_cb,
		      lua_frame_cb lua_frame_cb,
		      void *ctx);

/*
 * Push collected Lua backtrace onto Lua stack.
 */
void
lua_backtrace_push_frames(const struct lua_backtrace *bt, struct lua_State *L);

/*
 * Print collected frames via `say` API.
 */
void
lua_backtrace_print_frames(const struct lua_backtrace *bt,
			   enum say_level say_level);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
#endif /* ENABLE_BACKTRACE */
