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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
typedef struct tuple box_tuple_t;
struct lua_State;
struct mpstream;
struct luaL_serializer;

/** \cond public */

/**
 * Push a tuple onto the stack.
 * @param L Lua State
 * @sa luaT_istuple
 * @throws on OOM
 */
void
luaT_pushtuple(struct lua_State *L, box_tuple_t *tuple);

/**
 * Checks whether argument idx is a tuple
 *
 * @param L Lua State
 * @param idx the stack index
 * @retval non-NULL argument is tuple
 * @retval NULL argument is not tuple
 */
box_tuple_t *
luaT_istuple(struct lua_State *L, int idx);

/** \endcond public */

int
lbox_tuple_new(struct lua_State *L);

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
