/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#ifndef TARANTOOL_LUA_DECIMAL_H_INCLUDED
#define TARANTOOL_LUA_DECIMAL_H_INCLUDED

#include "lib/core/decimal.h"
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern __thread uint32_t CTID_DECIMAL;

struct lua_State;

/*
 * Alias box_decimal_t to decimal_t to use luaT_*decimal()
 * functions within tarantool source code without type
 * casting.
 *
 * The module API has its own box_decimal_t definition.
 */
typedef decNumber box_decimal_t;

/** \cond public */

/**
 * Allocate a new decimal on the Lua stack and return
 * a pointer to it.
 */
API_EXPORT box_decimal_t *
luaT_newdecimal(struct lua_State *L);

/**
 * Allocate a new decimal on the Lua stack with copy of given
 * decimal and return a pointer to it.
 */
API_EXPORT box_decimal_t *
luaT_pushdecimal(struct lua_State *L, const box_decimal_t *dec);

/**
 * Check whether a value on the Lua stack is a decimal.
 *
 * Returns a pointer to the decimal on a successful check,
 * NULL otherwise.
 */
API_EXPORT box_decimal_t *
luaT_isdecimal(struct lua_State *L, int index);

/** \endcond public */

void
tarantool_lua_decimal_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_DECIMAL_H_INCLUDED */
