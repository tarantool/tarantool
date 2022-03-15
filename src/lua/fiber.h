#ifndef TARANTOOL_LUA_FIBER_H_INCLUDED
#define TARANTOOL_LUA_FIBER_H_INCLUDED
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
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#include "backtrace.h"

/*
 * This is the declaration of the Lua stack entry function: it does not exactly
 * `lj_BC_FUNCC`'s signature and its only aim is to provide the `lj_BC_FUNCC`
 * symbol's address to the backtrace subsystem for detecting Lua frames.
 */
void
lj_BC_FUNCC(void);
#endif /* ENABLE_BACKTRACE */

struct lua_State;
struct backtrace;

/**
* Initialize box.fiber system
*/
void
tarantool_lua_fiber_init(struct lua_State *L);

void
luaL_testcancel(struct lua_State *L);

#ifdef ENABLE_BACKTRACE
void
fiber_backtrace_collect_lua_frames_foreach(struct lua_State *L,
					   struct backtrace *bt);

void
fiber_backtrace_foreach_c_frame_cb(int frame_no, void *frame_ip,
				   const char *proc_name, size_t offs,
				   struct backtrace *bt);

void
fiber_backtrace_foreach_lua_frame_cb(const char *proc_name,
				     const char *src_name, int line_no,
				     struct backtrace *bt);
#endif /* ENABLE_BACKTRACE */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_FIBER_H_INCLUDED */
