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


/** \cond public */
struct error;

/**
 * Re-throws the last Tarantool error as a Lua object.
 * \sa lua_error()
 * \sa box_error_last()
 */
LUA_API int
luaT_error(lua_State *L);

/**
 * Return nil as the first return value and an error as the
 * second. The error is received using box_error_last().
 *
 * @param L Lua stack.
 */
LUA_API int
luaT_push_nil_and_error(lua_State *L);

void
luaT_pusherror(struct lua_State *L, struct error *e);
/** \endcond public */

struct error *
luaL_iserror(struct lua_State *L, int narg);

struct error *
luaL_checkerror(struct lua_State *L, int narg);

void
tarantool_lua_error_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_ERROR_H */
