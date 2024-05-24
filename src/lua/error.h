#ifndef TARANTOOL_LUA_ERROR_H
#define TARANTOOL_LUA_ERROR_H
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include <lua.h>
#include <lauxlib.h> /* luaL_error */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern uint32_t CTID_CONST_STRUCT_ERROR_REF;

/** \cond public */
struct error;

/**
 * Re-throws the last Tarantool error as a Lua object. Set trace frame
 * of to the caller of Lua C API.
 *
 * \sa lua_error()
 * \sa box_error_last()
 */
LUA_API int
luaT_error(lua_State *L);

/**
 * Same as luaT_error but set error trace frame according to given level.
 * luaT_error same as this function with level equal to 1. If level is 0
 * then error trace is unchanged.
 */
int
luaT_error_at(lua_State *L, int level);

/**
 * Return nil as the first return value and an error as the
 * second. The error is received using box_error_last().
 *
 * @param L Lua stack.
 */
LUA_API int
luaT_push_nil_and_error(lua_State *L);

/**
 * Push error to a Lua stack.
 *
 * @param L Lua stack.
 * @param e error.
 */
void
luaT_pusherror(struct lua_State *L, struct error *e);
/** \endcond public */

struct error *
luaL_iserror(struct lua_State *L, int narg);

/**
 * Return argument on stack converted to struct error if it is box error.
 * Otherwise raise error.
 *
 * @param narg argument position on stack
 * @return argument converted
 */
struct error *
luaT_checkerror(struct lua_State *L, int narg);

void
tarantool_lua_error_init(struct lua_State *L);

/**
 * Set the error location information (file, line) to Lua stack frame at
 * the given level. Level 1 is the Lua function that called this function.
 * If level is <= 0 or the function failed to get the location information,
 * the location is cleared.
 */
void
luaT_error_set_trace(lua_State *L, int level, struct error *error);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_ERROR_H */
