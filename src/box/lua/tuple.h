#ifndef INCLUDES_TARANTOOL_MOD_BOX_LUA_TUPLE_H
#define INCLUDES_TARANTOOL_MOD_BOX_LUA_TUPLE_H
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
#include <stddef.h>
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct tuple_format;
typedef struct tuple box_tuple_t;
struct lua_State;
struct mpstream;
struct luaL_serializer;
struct tuple_format;
typedef struct tuple_format box_tuple_format_t;

/** \cond public */

/**
 * Checks whether the argument idx is a tuple and
 * returns it.
 *
 * @param L Lua State
 * @param idx the stack index
 * @retval non-NULL argument is tuple
 * @throws error if the argument is not a tuple.
 */
API_EXPORT box_tuple_t *
luaT_checktuple(struct lua_State *L, int idx);

/**
 * Push a tuple onto the stack.
 * @param L Lua State
 * @sa luaT_istuple
 * @throws on OOM
 */
API_EXPORT void
luaT_pushtuple(struct lua_State *L, box_tuple_t *tuple);

/**
 * Checks whether argument idx is a tuple.
 *
 * @param L Lua State
 * @param idx the stack index
 * @retval non-NULL argument is tuple
 * @retval NULL argument is not tuple
 */
API_EXPORT box_tuple_t *
luaT_istuple(struct lua_State *L, int idx);

/**
 * Encode a table or a tuple on the Lua stack as an MsgPack array.
 *
 * @param L              Lua state.
 * @param idx            Acceptable index on the Lua stack.
 * @param tuple_len_ptr  Where to store tuple data size in bytes
 *                       (or NULL).
 *
 * The storage for data is allocated on the box region. A caller
 * should call <box_region_truncate>() to release the data.
 *
 * In case of an error set a diag and return NULL.
 *
 * @sa luaT_tuple_new()
 */
API_EXPORT char *
luaT_tuple_encode(struct lua_State *L, int idx, size_t *tuple_len_ptr);

/**
 * Create a new tuple with specific format from a Lua table or a
 * tuple.
 *
 * The new tuple is referenced in the same way as one created by
 * <box_tuple_new>(). There are two possible usage scenarious:
 *
 * 1. A short living tuple may not be referenced explicitly and
 *    will be collected automatically at the next module API call
 *    that yields or returns a tuple.
 * 2. A long living tuple must be referenced using
 *    <box_tuple_ref>() and unreferenced then with
 *    <box_tuple_unref>().
 *
 * @sa box_tuple_ref()
 *
 * In case of an error set a diag and return NULL.
 */
API_EXPORT box_tuple_t *
luaT_tuple_new(struct lua_State *L, int idx, box_tuple_format_t *format);

/** \endcond public */

static inline int
luaT_pushtupleornil(struct lua_State *L, struct tuple *tuple)
{
	if (tuple == NULL)
		return 0;
	luaT_pushtuple(L, tuple);
	return 1;
}

void
luamp_convert_key(struct lua_State *L, struct luaL_serializer *cfg,
		  struct mpstream *stream, int index);

void
luamp_encode_tuple(struct lua_State *L, struct luaL_serializer *cfg,
		   struct mpstream *stream, int index);

void
tuple_to_mpstream(struct tuple *tuple, struct mpstream *stream);

void
box_lua_tuple_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_MOD_BOX_LUA_TUPLE_H */
