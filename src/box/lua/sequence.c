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

#include "diag.h"
#include "box/box.h"
#include "box/schema.h"
#include "box/sequence.h"
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

static void
lbox_sequence_new(struct lua_State *L, struct sequence *seq)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_getfield(L, -1, "sequence");
	lua_rawgeti(L, -1, seq->def->id);
	if (lua_isnil(L, -1)) {
		/*
		 * If the sequence already exists, modify it,
		 * rather than create a new one -- to not
		 * invalidate Lua variable references to old
		 * sequence outside the box.schema.sequence[].
		 */
		lua_pop(L, 1);
		lua_newtable(L);
		lua_rawseti(L, -2, seq->def->id);
		lua_rawgeti(L, -1, seq->def->id);
	} else {
		/* Clear the reference to old sequence by old name. */
		lua_getfield(L, -1, "name");
		lua_pushnil(L);
		lua_settable(L, -4);
	}
	int top = lua_gettop(L);
	lua_pushstring(L, "id");
	lua_pushnumber(L, seq->def->id);
	lua_settable(L, top);
	lua_pushstring(L, "uid");
	lua_pushnumber(L, seq->def->uid);
	lua_settable(L, top);
	lua_pushstring(L, "name");
	lua_pushstring(L, seq->def->name);
	lua_settable(L, top);
	lua_pushstring(L, "step");
	luaL_pushint64(L, seq->def->step);
	lua_settable(L, top);
	lua_pushstring(L, "min");
	luaL_pushint64(L, seq->def->min);
	lua_settable(L, top);
	lua_pushstring(L, "max");
	luaL_pushint64(L, seq->def->max);
	lua_settable(L, top);
	lua_pushstring(L, "start");
	luaL_pushint64(L, seq->def->start);
	lua_settable(L, top);
	lua_pushstring(L, "cache");
	luaL_pushint64(L, seq->def->cache);
	lua_settable(L, top);
	lua_pushstring(L, "cycle");
	lua_pushboolean(L, seq->def->cycle);
	lua_settable(L, top);

	/* Bless sequence object. */
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "schema");
	lua_gettable(L, -2);
	lua_pushstring(L, "sequence");
	lua_gettable(L, -2);
	lua_pushstring(L, "bless");
	lua_gettable(L, -2);

	lua_pushvalue(L, top);
	lua_call(L, 1, 0);
	lua_pop(L, 3);

	lua_setfield(L, -2, seq->def->name);

	lua_pop(L, 2);
}

static void
lbox_sequence_delete(struct lua_State *L, struct sequence *seq)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_getfield(L, -1, "sequence");
	lua_rawgeti(L, -1, seq->def->id);
	if (!lua_isnil(L, -1)) {
		lua_getfield(L, -1, "name");
		lua_pushnil(L);
		lua_rawset(L, -4);
		lua_pop(L, 1); /* pop sequence */
		lua_pushnil(L);
		lua_rawseti(L, -2, seq->def->id);
	} else {
		lua_pop(L, 1);
	}
	lua_pop(L, 2); /* box, sequence */
}

static int
lbox_sequence_new_or_delete(struct trigger *trigger, void *event)
{
	struct lua_State *L = trigger->data;
	struct sequence *seq = event;
	if (sequence_by_id(seq->def->id) != NULL)
		lbox_sequence_new(L, seq);
	else
		lbox_sequence_delete(L, seq);
	return 0;
}

void
box_lua_sequence_init(struct lua_State *L)
{
	static const struct luaL_Reg sequence_internal_lib[] = {
		{"next", lbox_sequence_next},
		{"set", lbox_sequence_set},
		{"reset", lbox_sequence_reset},
		{NULL, NULL}
	};
	luaL_register(L, "box.internal.sequence", sequence_internal_lib);
	lua_pop(L, 1);

	static struct trigger on_alter_sequence_in_lua;
	trigger_create(&on_alter_sequence_in_lua,
		       lbox_sequence_new_or_delete, L, NULL);
	trigger_add(&on_alter_sequence, &on_alter_sequence_in_lua);
}
