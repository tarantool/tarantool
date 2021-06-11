#ifndef TARANTOOL_LUA_MSGPACK_H_INCLUDED
#define TARANTOOL_LUA_MSGPACK_H_INCLUDED
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
#include <stdint.h>
#include <stdbool.h>

#include "utils.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <lua.h>

struct luaL_field;
struct luaL_serializer;
struct mpstream;
struct serializer_opts;

/**
 * Default instance of msgpack serializer (msgpack = require('msgpack')).
 * This instance is used by all box's Lua/C API bindings (e.g. space:replace()).
 * All changes made by msgpack.cfg{} function are also affect box's bindings
 * (this is a feature).
 */
extern struct luaL_serializer *luaL_msgpack_default;

/**
 * luaL_error()
 */
void
luamp_error(void *);

enum { LUAMP_ALLOC_FACTOR = 256 };

/* low-level function needed for execute_lua_call() */
enum mp_type
luamp_encode_r(struct lua_State *L, struct luaL_serializer *cfg,
	       const struct serializer_opts *opts, struct mpstream *stream,
	       struct luaL_field *field, int level);

enum mp_type
luamp_encode(struct lua_State *L, struct luaL_serializer *cfg,
	     const struct serializer_opts *opts, struct mpstream *stream,
	     int index);

void
luamp_decode(struct lua_State *L, struct luaL_serializer *cfg,
	     const char **data);

typedef enum mp_type
(*luamp_encode_extension_f)(struct lua_State *, int, struct mpstream *);

/**
 * @brief Set a callback that executed by encoder on unsupported Lua type
 * @param handler callback
 */
void
luamp_set_encode_extension(luamp_encode_extension_f handler);

typedef void
(*luamp_decode_extension_f)(struct lua_State *L, const char **data);

/**
 * @brief Set a callback that executed by decode on unsupported extension
 * @param handler callback
 */
void
luamp_set_decode_extension(luamp_decode_extension_f handler);

/**
 * @brief Lua/C API exports
 * @param L Lua stack
 * @return 1
 */
LUALIB_API int
luaopen_msgpack(lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_MSGPACK_H_INCLUDED */
