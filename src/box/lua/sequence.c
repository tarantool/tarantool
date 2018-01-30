/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/sequence.h"
#include "box/lua/tuple.h"
#include "lua/utils.h"
#include "lua/trigger.h"

#include "diag.h"
#include "box/box.h"
#include "box/schema.h"
#include "box/txn.h"

static int
lbox_sequence_next(struct lua_State *L)
{
	uint32_t seq_id = luaL_checkinteger(L, 1);
	int64_t result;
	if (box_sequence_next(seq_id, &result) != 0)
		luaT_error(L);
	luaL_pushint64(L, result);
	return 1;
}

static int
lbox_sequence_set(struct lua_State *L)
{
	uint32_t seq_id = luaL_checkinteger(L, 1);
	int64_t value = luaL_checkint64(L, 2);
	if (box_sequence_set(seq_id, value) != 0)
		luaT_error(L);
	return 0;
}

static int
lbox_sequence_reset(struct lua_State *L)
{
	uint32_t seq_id = luaL_checkinteger(L, 1);
	if (box_sequence_reset(seq_id) != 0)
		luaT_error(L);
	return 0;
}

static int
lbox_sequence_push_on_alter_event(struct lua_State *L, void *event)
{
	struct txn_stmt *stmt = (struct txn_stmt *) event;
	if (stmt->old_tuple) {
		luaT_pushtuple(L, stmt->old_tuple);
	} else {
		lua_pushnil(L);
	}
	if (stmt->new_tuple) {
		luaT_pushtuple(L, stmt->new_tuple);
	} else {
		lua_pushnil(L);
	}
	return 2;
}

static int
lbox_sequence_on_alter(struct lua_State *L)
{
	return lbox_trigger_reset(L, 2, &on_alter_sequence,
				  lbox_sequence_push_on_alter_event, NULL);
}

void
box_lua_sequence_init(struct lua_State *L)
{
	static const struct luaL_Reg sequence_internal_lib[] = {
		{"next", lbox_sequence_next},
		{"set", lbox_sequence_set},
		{"reset", lbox_sequence_reset},
		{"on_alter", lbox_sequence_on_alter},
		{NULL, NULL}
	};
	luaL_register(L, "box.internal.sequence", sequence_internal_lib);
	lua_pop(L, 1);
}
