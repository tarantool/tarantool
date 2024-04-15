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
#include "box/lua/func_adapter.h"
#include "box/lua/trigger.h"
#include "box/lua/space.h"
#include "box/lua/tuple.h"
#include "box/lua/key_def.h"
#include "box/sql/sqlLimit.h"
#include "lua/utils.h"
#include "lua/trigger.h"
#include "box/box.h"

extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
} /* extern "C" */

#include "box/func.h"
#include "box/func_def.h"
#include "box/space.h"
#include "box/memtx_space.h"
#include "box/schema.h"
#include "box/user_def.h"
#include "box/tuple.h"
#include "box/tuple_constraint.h"
#include "box/txn.h"
#include "box/sequence.h"
#include "box/coll_id_cache.h"
#include "box/replication.h" /* GROUP_LOCAL */
#include "box/iproto_constants.h" /* iproto_type_name */
#include "vclock/vclock.h"

/**
 * Set/Reset/Get a temporary trigger to an event associated with space by id.
 * Argument event_fmt is a format string for the event name - it must expect
 * one uint32_t value as an argument.
 */
static int
lbox_space_reset_trigger(struct lua_State *L, uint32_t space_id,
			 const char *event_fmt)
{
	const char *event_name = tt_sprintf(event_fmt, space_id);
	struct event *event = event_get(event_name, true);
	assert(event != NULL);
	return luaT_event_reset_trigger_with_flags(
		L, 2, event, EVENT_TRIGGER_IS_TEMPORARY);
}

/**
 * Set/Reset/Get space on_replace trigger. If space runs recovery triggers,
 * associated recovery trigger is set as well but it does not affect returned
 * value. The new trigger is bound by id.
 */
static int
lbox_space_on_replace(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 1 || !lua_istable(L, 1)) {
		luaL_error(L, "usage: space:on_replace(function | nil, "
			   "[function | nil], [string])");
	}
	lua_getfield(L, 1, "id"); /* Get space id. */
	uint32_t id = lua_tonumber(L, lua_gettop(L));
	struct space *space = space_cache_find_xc(id);
	lua_pop(L, 1);

	if (space->run_recovery_triggers) {
		const char *fmt = "box.space[%u].on_recovery_replace";
		lbox_space_reset_trigger(L, id, fmt);
		lua_settop(L, top);
	}
	const char *fmt = "box.space[%u].on_replace";
	return lbox_space_reset_trigger(L, id, fmt);
}

/**
 * Set/Reset/Get space before_replace trigger. If space runs recovery triggers,
 * associated recovery trigger is set as well but it does not affect returned
 * value. The new trigger is bound by id.
 */
static int
lbox_space_before_replace(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 1 || !lua_istable(L, 1)) {
		luaL_error(L, "usage: space:before_replace(function | nil, "
			   "[function | nil], [string])");
	}
	lua_getfield(L, 1, "id"); /* Get space id. */
	uint32_t id = lua_tonumber(L, lua_gettop(L));
	struct space *space = space_cache_find_xc(id);
	lua_pop(L, 1);

	if (space->run_recovery_triggers) {
		const char *fmt = "box.space[%u].before_recovery_replace";
		lbox_space_reset_trigger(L, id, fmt);
		lua_settop(L, top);
	}
	const char *fmt = "box.space[%u].before_replace";
	return lbox_space_reset_trigger(L, id, fmt);
}

/**
 * Create constraint field in lua space object, given by index i in lua stack.
 * If the space has no constraints, there will be no constraint field.
 */
static void
lbox_push_space_constraint(struct lua_State *L, struct space *space, int i)
{
	assert(i >= 0);
	struct tuple_format *fmt = space->format;
	uint32_t constraint_count = 0;
	for (size_t k = 0; k < fmt->constraint_count; k++) {
		struct tuple_constraint *c = &fmt->constraint[k];
		if (c->def.type == CONSTR_FUNC)
			constraint_count++;
	}
	if (constraint_count == 0) {
		/* No constraints - no field. */
		lua_pushnil(L);
		lua_setfield(L, i, "constraint");
		return;
	}

	lua_newtable(L);
	for (size_t k = 0; k < fmt->constraint_count; k++) {
		if (fmt->constraint[k].def.type != CONSTR_FUNC)
			continue;
		lua_pushnumber(L, fmt->constraint[k].def.func.id);
		lua_setfield(L, -2, fmt->constraint[k].def.name);
	}
	lua_setfield(L, i, "constraint");
}

/**
 * Helper function of lbox_push_space_foreign_key.
 * Push a value @a def to the top of lua stack @a L.
 * ID-defined fields are converted to one-based index.
 */
static void
lbox_push_field_id(struct lua_State *L,
		   struct tuple_constraint_field_id *def)
{
	if (def->name_len == 0)
		lua_pushnumber(L, def->id + 1);
	else
		lua_pushstring(L, def->name);
}

/**
 * Create foreign_key field in lua space object, given by index i in lua stack.
 * If the space has no foreign keys, there will be no foreign_key field.
 */
static void
lbox_push_space_foreign_key(struct lua_State *L, struct space *space, int i)
{
	assert(i >= 0);
	struct tuple_format *fmt = space->format;
	uint32_t foreign_key_count = 0;
	for (size_t k = 0; k < fmt->constraint_count; k++) {
		struct tuple_constraint *c = &fmt->constraint[k];
		if (c->def.type == CONSTR_FKEY)
			foreign_key_count++;
	}
	if (foreign_key_count == 0) {
		/* No foreign keys - no field. */
		lua_pushnil(L);
		lua_setfield(L, i, "foreign_key");
		return;
	}

	lua_newtable(L);
	for (size_t k = 0; k < fmt->constraint_count; k++) {
		struct tuple_constraint *c = &fmt->constraint[k];
		if (c->def.type != CONSTR_FKEY)
			continue;

		lua_newtable(L);
		if (c->def.fkey.space_id == 0) {
			/* No space id - no field. */
			lua_pushnil(L);
		} else {
			lua_pushnumber(L, c->def.fkey.space_id);
		}
		lua_setfield(L, -2, "space");
		lua_newtable(L);
		for (uint32_t j = 0; j < c->def.fkey.field_mapping_size; j++) {
			struct tuple_constraint_fkey_field_mapping *m =
				&c->def.fkey.field_mapping[j];
			lbox_push_field_id(L, &m->local_field);
			lbox_push_field_id(L, &m->foreign_field);
			lua_settable(L, -3);
		}
		lua_setfield(L, -2, "field");
		lua_setfield(L, -2, fmt->constraint[k].def.name);
	}
	lua_setfield(L, i, "foreign_key");
}

/**
 * index.parts:extract_key(tuple)
 * Stack: [1] unused; [2] tuple.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_extract_key(struct lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: index.parts:extract_key(tuple)");
	return luaT_key_def_extract_key(L, lua_upvalueindex(1));
}

/**
 * index.parts:validate_key(key)
 * Stack: [1] unused; [2] key.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_validate_key(struct lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: index.parts:validate_key(key)");
	return luaT_key_def_validate_key(L, lua_upvalueindex(1));
}

/**
 * index.parts:validate_full_key(key)
 * Stack: [1] unused; [2] key.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_validate_full_key(struct lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: index.parts:validate_full_key("
				     "key)");
	return luaT_key_def_validate_full_key(L, lua_upvalueindex(1));
}

/**
 * index.parts:validate_tuple(key)
 * Stack: [1] unused; [2] tuple.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_validate_tuple(struct lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Usage: index.parts:validate_tuple("
				     "tuple)");
	return luaT_key_def_validate_tuple(L, lua_upvalueindex(1));
}

/**
 * index.parts:compare(tuple_a, tuple_b)
 * Stack: [1] unused; [2] tuple_a; [3] tuple_b.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_compare(struct lua_State *L)
{
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "Usage: index.parts:compare("
				     "tuple_a, tuple_b)");
	}
	return luaT_key_def_compare(L, lua_upvalueindex(1));
}

/**
 * index.parts:compare_with_key(tuple, key)
 * Stack: [1] unused; [2] tuple; [3] key.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_compare_with_key(struct lua_State *L)
{
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "Usage: index.parts:compare_with_key("
				     "tuple, key)");
	}
	return luaT_key_def_compare_with_key(L, lua_upvalueindex(1));
}

/**
 * index.parts:compare_keys(key_a, key_b)
 * Stack: [1] unused; [2] key_a; [3] key_b.
 * key_def is passed in the upvalue.
 */
static int
lbox_index_parts_compare_keys(struct lua_State *L)
{
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "Usage: index.parts:compare_keys("
				     "key_a, key_b)");
	}
	return luaT_key_def_compare_keys(L, lua_upvalueindex(1));
}

/**
 * index.parts:merge(second_index_parts)
 * Stack: [1] unused; [2] second_index_parts.
 * First key_def is passed in the upvalue.
 */
static int
lbox_index_parts_merge(struct lua_State *L)
{
	int idx_b;
	struct key_def *key_def_b;
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "Usage: index.parts:merge("
				     "second_index_parts)");
	}
	lua_pushcfunction(L, lbox_key_def_new);
	lua_replace(L, 1);
	/*
	 * Stack:
	 * [1] lbox_key_def_new
	 * [2] second_index_parts (first argument for lbox_key_def_new)
	 */
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto key_def_b_error;
	/*
	 * Stack:
	 * [1] key_def_b
	 */
	idx_b = 1;
	key_def_b = luaT_is_key_def(L, idx_b);
	if (key_def_b == NULL)
		goto key_def_b_error;
	return luaT_key_def_merge(L, lua_upvalueindex(1), idx_b);

key_def_b_error:
	return luaL_error(L,
			  "Can't create key_def from the second index.parts");
}

/**
 * Populate __index metamethod of index_object.parts table with the methods,
 * which work like require('key_def').new(index_object.parts) methods.
 * Each method is implemented as a C closure, associated with `struct key_def'.
 */
static void
luaT_add_index_parts_methods(struct lua_State *L, const struct key_def *key_def)
{
	/* Metatable. */
	lua_newtable(L);
	/* __index */
	lua_newtable(L);
	int idx_index = lua_gettop(L);

	luaT_push_key_def(L, key_def);
	int idx_key_def = lua_gettop(L);

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_extract_key, 1);
	lua_setfield(L, idx_index, "extract_key");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_validate_key, 1);
	lua_setfield(L, idx_index, "validate_key");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_validate_full_key, 1);
	lua_setfield(L, idx_index, "validate_full_key");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_validate_tuple, 1);
	lua_setfield(L, idx_index, "validate_tuple");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_compare, 1);
	lua_setfield(L, idx_index, "compare");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_compare_with_key, 1);
	lua_setfield(L, idx_index, "compare_with_key");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_compare_keys, 1);
	lua_setfield(L, idx_index, "compare_keys");

	lua_pushvalue(L, idx_key_def);
	lua_pushcclosure(L, &lbox_index_parts_merge, 1);
	lua_setfield(L, idx_index, "merge");

	lua_pop(L, 1); /* key_def */
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
}

/**
 * Make a single space available in Lua,
 * via box.space[] array.
 *
 * @return A new table representing a space on top of the Lua
 * stack.
 */
static void
lbox_fillspace(struct lua_State *L, struct space *space, int i)
{
	/* space.arity */
	lua_pushstring(L, "field_count");
	lua_pushnumber(L, space->def->exact_field_count);
	lua_settable(L, i);

	/* space.n */
	lua_pushstring(L, "id");
	lua_pushnumber(L, space_id(space));
	lua_settable(L, i);

	/* space.group_id */
	lua_pushstring(L, "is_local");
	lua_pushboolean(L, space_is_local(space));
	lua_settable(L, i);

	/* space.temporary */
	lua_pushstring(L, "temporary");
	lua_pushboolean(L, space_is_data_temporary(space));
	lua_settable(L, i);

	/* space.type */
	lua_pushstring(L, "type");
	lua_pushstring(L, space_type_name(space->def->opts.type));
	lua_settable(L, i);

	/* space.name */
	lua_pushstring(L, "name");
	lua_pushstring(L, space_name(space));
	lua_settable(L, i);

	/* space.engine */
	lua_pushstring(L, "engine");
	lua_pushstring(L, space->def->engine_name);
	lua_settable(L, i);

	/* space.is_sync */
	lua_pushstring(L, "is_sync");
	lua_pushboolean(L, space_is_sync(space));
	lua_settable(L, i);

	lua_pushstring(L, "enabled");
	lua_pushboolean(L, space_index(space, 0) != 0);
	lua_settable(L, i);

	/* space:on_replace */
	lua_pushstring(L, "on_replace");
	lua_pushcfunction(L, lbox_space_on_replace);
	lua_settable(L, i);

	/* space:before_replace */
	lua_pushstring(L, "before_replace");
	lua_pushcfunction(L, lbox_space_before_replace);
	lua_settable(L, i);

	if (space_is_vinyl(space)) {
		lua_pushstring(L, "defer_deletes");
		lua_pushboolean(L, space->def->opts.defer_deletes);
		lua_settable(L, i);
	}

	lua_getfield(L, i, "index");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		/* space.index */
		lua_pushstring(L, "index");
		lua_newtable(L);
		lua_settable(L, i);	/* push space.index */
		lua_getfield(L, i, "index");
	} else {
		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			if (lua_isnumber(L, -2)) {
				uint32_t iid = (uint32_t) lua_tonumber(L, -2);
				/*
				 * Remove index only if it was deleted.
				 * If an existing index was
				 * altered, update the existing
				 * lua table to keep local
				 * references to this index
				 * intact.
				 */
				if (space_index(space, iid) == NULL) {
					lua_pushnumber(L, iid);
					lua_pushnil(L);
					lua_settable(L, -5);
				}
				lua_pop(L, 1);
			} else {
				/*
				 * Remove all named references
				 * to an existing index, since
				 * an existing index may have been
				 * renamed. The references will be
				 * re-instated below.
				 */
				assert(lua_isstring(L, -2));
				lua_pushvalue(L, -2);
				lua_pushnil(L);
				lua_settable(L, -5);
				lua_pop(L, 2);
				lua_pushnil(L);
			}
		}
	}
	/*
	 * Fill space.index table with
	 * all defined indexes.
	 */
	for (unsigned k = 0; k <= space->index_id_max; k++) {
		struct index *index = space_index(space, k);
		if (index == NULL)
			continue;
		struct index_def *index_def = index->def;
		struct index_opts *index_opts = &index_def->opts;
		lua_rawgeti(L, -1, index_def->iid);
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_pushnumber(L, index_def->iid);
			lua_newtable(L);
			lua_settable(L, -3);
			lua_rawgeti(L, -1, index_def->iid);
			assert(! lua_isnil(L, -1));
		}

		if (index_def->type == HASH || index_def->type == TREE) {
			lua_pushboolean(L, index_opts->is_unique);
			lua_setfield(L, -2, "unique");
		} else if (index_def->type == RTREE) {
			lua_pushnumber(L, index_opts->dimension);
			lua_setfield(L, -2, "dimension");
		}
		if (space_is_memtx(space) && index_def->type == TREE) {
			lua_pushboolean(L, index_opts->hint == INDEX_HINT_ON);
			lua_setfield(L, -2, "hint");
		} else {
			lua_pushnil(L);
			lua_setfield(L, -2, "hint");
		}

		if (index_opts->func_id > 0) {
			lua_pushstring(L, "func");
			lua_newtable(L);

			lua_pushnumber(L, index_opts->func_id);
			lua_setfield(L, -2, "fid");

			struct func *func = func_by_id(index_opts->func_id);
			if (func != NULL) {
				lua_pushstring(L, func->def->name);
				lua_setfield(L, -2, "name");
			}

			lua_settable(L, -3);
		}

		lua_pushstring(L, index_type_strs[index_def->type]);
		lua_setfield(L, -2, "type");

		lua_pushnumber(L, index_def->iid);
		lua_setfield(L, -2, "id");

		lua_pushnumber(L, space->def->id);
		lua_setfield(L, -2, "space_id");

		lua_pushstring(L, index_def->name);
		lua_setfield(L, -2, "name");

		lua_pushstring(L, "parts");
		luaT_push_key_def_parts(L, index_def->key_def);
		luaT_add_index_parts_methods(L, index_def->key_def);
		lua_settable(L, -3); /* space.index[k].parts */

		lua_pushstring(L, "sequence_id");
		if (k == 0 && space->sequence != NULL) {
			lua_pushnumber(L, space->sequence->def->id);
		} else {
			/*
			 * This removes field 'sequence_id' from
			 * the table if it is set. If it is not
			 * set, this is a no-op.
			 */
			lua_pushnil(L);
		}
		/*
		 * Optional attributes must be set via
		 * 'raw' API to avoid invocation of
		 * __newindex metamethod.
		 */
		lua_rawset(L, -3);

		lua_pushstring(L, "sequence_fieldno");
		if (k == 0 && space->sequence != NULL)
			lua_pushnumber(L, space->sequence_fieldno +
				       TUPLE_INDEX_BASE);
		else
			lua_pushnil(L);
		lua_rawset(L, -3);

		lua_pushstring(L, "sequence_path");
		if (k == 0 && space->sequence_path != NULL)
			lua_pushstring(L, space->sequence_path);
		else
			lua_pushnil(L);
		lua_rawset(L, -3);

		if (space_is_vinyl(space)) {
			lua_pushstring(L, "options");
			lua_newtable(L);

			if (index_opts->range_size > 0) {
				lua_pushnumber(L, index_opts->range_size);
				lua_setfield(L, -2, "range_size");
			}

			lua_pushnumber(L, index_opts->page_size);
			lua_setfield(L, -2, "page_size");

			lua_pushnumber(L, index_opts->run_count_per_level);
			lua_setfield(L, -2, "run_count_per_level");

			lua_pushnumber(L, index_opts->run_size_ratio);
			lua_setfield(L, -2, "run_size_ratio");

			lua_pushnumber(L, index_opts->bloom_fpr);
			lua_setfield(L, -2, "bloom_fpr");

			lua_settable(L, -3);
		}
		lua_setfield(L, -2, index_def->name);
	}

	lua_pop(L, 1); /* pop the index field */

	lbox_push_space_constraint(L, space, i);
	lbox_push_space_foreign_key(L, space, i);

	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "schema");
	lua_gettable(L, -2);
	lua_pushstring(L, "space");
	lua_gettable(L, -2);
	lua_pushstring(L, "bless");
	lua_gettable(L, -2);

	lua_pushvalue(L, i);	/* space */
	lua_call(L, 1, 0);
	lua_pop(L, 3);	/* cleanup stack - box, schema, space */
}

/** Export a space to Lua */
static void
box_lua_space_new(struct lua_State *L, struct space *space)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_getfield(L, -1, "space");

	if (!lua_istable(L, -1)) {
		lua_pop(L, 1); /* pop nil */
		lua_newtable(L);
		lua_setfield(L, -2, "space");
		lua_getfield(L, -1, "space");
	}
	/*
	 * We can have the following conditions here:
	 * a) The space is totally new (e. g. on new space creation).
	 * b) The space is replaced (e. g. on index update).
	 * c) The space is updated (e. g. on sequence update).
	 * d) The space is reverted (e. g. on space drop rollback).
	 *
	 * - In case a) we need to create new box.space.<id|name> entries.
	 * - In cases b) and c) we need to update the existing box.space.<id>
	 *   entry, drop the old box.space.<name> and create a new one.
	 * - In case d) we need to restore the original box.space.<id|name>
	 *   entries, but let's only restore the box.space.<id> entry and
	 *   perform the same actions as for b) and c) - it won't change the
	 *   visible behavior but will make the code simpler.
	 */
	if (space->lua_ref != LUA_NOREF) {
		/* We have either case c) or d). */
		lua_rawgeti(L, LUA_REGISTRYINDEX, space->lua_ref);
		lua_rawseti(L, -2, space_id(space));
	}
	lua_rawgeti(L, -1, space_id(space));
	if (lua_isnil(L, -1)) {
		/*
		 * If the space already exists, modify it, rather
		 * than create a new one -- to not invalidate
		 * Lua variable references to old space outside
		 * the box.space[].
		 */
		lua_pop(L, 1);
		lua_newtable(L);
		lua_rawseti(L, -2, space_id(space));
		lua_rawgeti(L, -1, space_id(space));
	} else {
		/* Clear the reference to old space by old name. */
		lua_getfield(L, -1, "name");
		lua_pushnil(L);
		lua_settable(L, -4);
	}
	lbox_fillspace(L, space, lua_gettop(L));
	lua_setfield(L, -2, space_name(space));

	if (space->lua_ref == LUA_NOREF) {
		/*
		 * Save the reference to the box.space[id] to restore
		 * the exactly same object on the space drop rollback.
		 * It prevents the situations when old references to
		 * the space go out of sync with the space which had
		 * been rolled back. For more information see #9120.
		 */
		lua_rawgeti(L, -1, space_id(space));
		space->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	lua_pop(L, 2); /* box, space */
}

/** Delete a given space in Lua */
static void
box_lua_space_delete(struct lua_State *L, uint32_t id)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_getfield(L, -1, "space");
	lua_rawgeti(L, -1, id);
	lua_getfield(L, -1, "name");
	lua_pushnil(L);
	lua_rawset(L, -4);
	lua_pop(L, 1); /* pop space */

	lua_pushnil(L);
	lua_rawseti(L, -2, id);
	lua_pop(L, 2); /* box, space */
}

static int
box_lua_space_new_or_delete(struct trigger *trigger, void *event)
{
	struct lua_State *L = (struct lua_State *) trigger->data;
	struct space *space = (struct space *) event;

	if (space_by_id(space->def->id) != NULL) {
		box_lua_space_new(L, space);
	} else {
		box_lua_space_delete(L, space->def->id);
	}
	return 0;
}

static TRIGGER(on_alter_space_in_lua, box_lua_space_new_or_delete);

/**
 * Make a tuple or a table Lua object by map.
 * @param Lua space object.
 * @param Lua map table object.
 * @param Lua opts table object (optional).
 * @retval not nil A tuple or a table conforming to a space
 *         format.
 * @retval nil, err Can not built a tuple. A reason is returned in
 *         the second value.
 */
static int
lbox_space_frommap(struct lua_State *L)
{
	struct tuple_dictionary *dict = NULL;
	uint32_t id = 0;
	struct space *space = NULL;
	struct tuple *tuple = NULL;
	int argc = lua_gettop(L);
	bool table = false;
	if (argc < 2 || argc > 3 || !lua_istable(L, 2))
		goto usage_error;
	if (argc == 3) {
		if (!lua_istable(L, 3))
			goto usage_error;
		lua_getfield(L, 3, "table");
		if (!lua_isboolean(L, -1) && !lua_isnil(L, -1))
			goto usage_error;
		table = lua_toboolean(L, -1);
	}

	lua_getfield(L, 1, "id");
	id = (int)lua_tointeger(L, -1);
	space = space_by_id(id);
	if (space == NULL) {
		lua_pushnil(L);
		lua_pushfstring(L, "Space with id '%d' doesn't exist", id);
		return 2;
	}
	assert(space->format != NULL);

	dict = space->format->dict;
	lua_createtable(L, space->def->field_count, 0);

	lua_pushnil(L);
	while (lua_next(L, 2) != 0) {
		uint32_t fieldno;
		size_t key_len;
		const char *key = lua_tolstring(L, -2, &key_len);
		uint32_t key_hash = lua_hashstring(L, -2);
		if (tuple_fieldno_by_name(dict, key, key_len, key_hash,
					  &fieldno)) {
			lua_pushnil(L);
			lua_pushfstring(L, "Unknown field '%s'", key);
			return 2;
		}
		lua_rawseti(L, -3, fieldno+1);
	}

	lua_replace(L, 1);
	lua_settop(L, 1);
	tuple = luaT_tuple_new(L, -1, space->format);
	if (tuple == NULL) {
		struct error *e = diag_last_error(&fiber()->diag);
		lua_pushnil(L);
		lua_pushstring(L, e->errmsg);
		return 2;
	}
	if (table)
		return 1;
	luaT_pushtuple(L, tuple);
	return 1;
usage_error:
	return luaL_error(L, "Usage: space:frommap(map, opts)");
}

/**
 * Push to Lua stack a table with the statistics on the memory usage by tuples
 * of the space.
 */
static int
lbox_space_stat(struct lua_State *L)
{
	int argc = lua_gettop(L);
	if (argc != 1 || !lua_istable(L, 1))
		luaL_error(L, "Usage: space:stat()");

	lua_getfield(L, 1, "id");
	uint32_t id = (int)lua_tointeger(L, -1);
	struct space *space = space_by_id(id);
	if (space == NULL) {
		lua_pushnil(L);
		lua_pushfstring(L, "Space with id '%d' doesn't exist", id);
		return 2;
	}

	if (!space_is_memtx(space)) {
		lua_newtable(L);
		return 1;
	}
	struct memtx_space *memtx_space = (struct memtx_space *)space;

	lua_newtable(L); /* Result table. */
	lua_newtable(L); /* result.tuple */

	for (int i = TUPLE_ARENA_MEMTX; i <= TUPLE_ARENA_MALLOC; i++) {
		enum tuple_arena_type type = (enum tuple_arena_type)i;
		struct tuple_info *stat = &memtx_space->tuple_stat[type];
		lua_newtable(L);

		lua_pushnumber(L, stat->data_size);
		lua_setfield(L, -2, "data_size");

		lua_pushnumber(L, stat->header_size);
		lua_setfield(L, -2, "header_size");

		lua_pushnumber(L, stat->field_map_size);
		lua_setfield(L, -2, "field_map_size");

		lua_pushnumber(L, stat->waste_size);
		lua_setfield(L, -2, "waste_size");

		lua_setfield(L, -2, tuple_arena_type_strs[i]);
	}
	lua_setfield(L, -2, "tuple");

	return 1;
}

void
box_lua_space_init(struct lua_State *L)
{
	/* Register the trigger that will push space data to Lua. */
	on_alter_space_in_lua.data = L;
	trigger_add(&on_alter_space, &on_alter_space_in_lua);

	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_newtable(L);
	lua_setfield(L, -2, "schema");
	lua_getfield(L, -1, "schema");
	lua_pushnumber(L, BOX_VINYL_DEFERRED_DELETE_ID);
	lua_setfield(L, -2, "VINYL_DEFERRED_DELETE_ID");
	lua_pushnumber(L, BOX_SCHEMA_ID);
	lua_setfield(L, -2, "SCHEMA_ID");
	lua_pushnumber(L, BOX_SPACE_ID);
	lua_setfield(L, -2, "SPACE_ID");
	lua_pushnumber(L, BOX_VSPACE_ID);
	lua_setfield(L, -2, "VSPACE_ID");
	lua_pushnumber(L, BOX_INDEX_ID);
	lua_setfield(L, -2, "INDEX_ID");
	lua_pushnumber(L, BOX_VINDEX_ID);
	lua_setfield(L, -2, "VINDEX_ID");
	lua_pushnumber(L, BOX_USER_ID);
	lua_setfield(L, -2, "USER_ID");
	lua_pushnumber(L, BOX_VUSER_ID);
	lua_setfield(L, -2, "VUSER_ID");
	lua_pushnumber(L, BOX_FUNC_ID);
	lua_setfield(L, -2, "FUNC_ID");
	lua_pushnumber(L, BOX_COLLATION_ID);
	lua_setfield(L, -2, "COLLATION_ID");
	lua_pushnumber(L, BOX_VCOLLATION_ID);
	lua_setfield(L, -2, "VCOLLATION_ID");
	lua_pushnumber(L, BOX_VFUNC_ID);
	lua_setfield(L, -2, "VFUNC_ID");
	lua_pushnumber(L, BOX_PRIV_ID);
	lua_setfield(L, -2, "PRIV_ID");
	lua_pushnumber(L, BOX_VPRIV_ID);
	lua_setfield(L, -2, "VPRIV_ID");
	lua_pushnumber(L, BOX_CLUSTER_ID);
	lua_setfield(L, -2, "CLUSTER_ID");
	lua_pushnumber(L, BOX_TRIGGER_ID);
	lua_setfield(L, -2, "TRIGGER_ID");
	lua_pushnumber(L, BOX_FK_CONSTRAINT_ID);
	lua_setfield(L, -2, "FK_CONSTRAINT_ID");
	lua_pushnumber(L, BOX_CK_CONSTRAINT_ID);
	lua_setfield(L, -2, "CK_CONSTRAINT_ID");
	lua_pushnumber(L, BOX_TRUNCATE_ID);
	lua_setfield(L, -2, "TRUNCATE_ID");
	lua_pushnumber(L, BOX_SEQUENCE_ID);
	lua_setfield(L, -2, "SEQUENCE_ID");
	lua_pushnumber(L, BOX_SEQUENCE_DATA_ID);
	lua_setfield(L, -2, "SEQUENCE_DATA_ID");
	lua_pushnumber(L, BOX_VSEQUENCE_ID);
	lua_setfield(L, -2, "VSEQUENCE_ID");
	lua_pushnumber(L, BOX_SPACE_SEQUENCE_ID);
	lua_setfield(L, -2, "SPACE_SEQUENCE_ID");
	lua_pushnumber(L, BOX_VSPACE_SEQUENCE_ID);
	lua_setfield(L, -2, "VSPACE_SEQUENCE_ID");
	lua_pushnumber(L, BOX_FUNC_INDEX_ID);
	lua_setfield(L, -2, "FUNC_INDEX_ID");
	lua_pushnumber(L, BOX_SESSION_SETTINGS_ID);
	lua_setfield(L, -2, "SESSION_SETTINGS_ID");
	lua_pushnumber(L, BOX_GC_CONSUMERS_ID);
	lua_setfield(L, -2, "GC_CONSUMERS_ID");
	lua_pushnumber(L, BOX_SYSTEM_ID_MIN);
	lua_setfield(L, -2, "SYSTEM_ID_MIN");
	lua_pushnumber(L, BOX_SYSTEM_ID_MAX);
	lua_setfield(L, -2, "SYSTEM_ID_MAX");
	lua_pushnumber(L, BOX_SYSTEM_USER_ID_MIN);
	lua_setfield(L, -2, "SYSTEM_USER_ID_MIN");
	lua_pushnumber(L, BOX_SYSTEM_USER_ID_MAX);
	lua_setfield(L, -2, "SYSTEM_USER_ID_MAX");
	lua_pushnumber(L, ADMIN);
	lua_setfield(L, -2, "ADMIN_ID");
	lua_pushnumber(L, GUEST);
	lua_setfield(L, -2, "GUEST_ID");
	lua_pushnumber(L, PUBLIC);
	lua_setfield(L, -2, "PUBLIC_ROLE_ID");
	lua_pushnumber(L, SUPER);
	lua_setfield(L, -2, "SUPER_ROLE_ID");
	lua_pushnumber(L, BOX_INDEX_MAX);
	lua_setfield(L, -2, "INDEX_MAX");
	lua_pushnumber(L, BOX_SPACE_MAX);
	lua_setfield(L, -2, "SPACE_MAX");
	lua_pushnumber(L, BOX_FIELD_MAX);
	lua_setfield(L, -2, "FIELD_MAX");
	lua_pushnumber(L, BOX_INDEX_FIELD_MAX);
	lua_setfield(L, -2, "INDEX_FIELD_MAX");
	lua_pushnumber(L, BOX_INDEX_PART_MAX);
	lua_setfield(L, -2, "INDEX_PART_MAX");
	lua_pushnumber(L, BOX_NAME_MAX);
	lua_setfield(L, -2, "NAME_MAX");
	lua_pushnumber(L, FORMAT_ID_MAX);
	lua_setfield(L, -2, "FORMAT_ID_MAX");
	lua_pushnumber(L, VCLOCK_MAX);
	lua_setfield(L, -2, "REPLICA_MAX");
	lua_pushnumber(L, SQL_BIND_PARAMETER_MAX);
	lua_setfield(L, -2, "SQL_BIND_PARAMETER_MAX");
	lua_pushnumber(L, BOX_SPACE_ID_TEMPORARY_MIN);
	lua_setfield(L, -2, "SPACE_ID_TEMPORARY_MIN");
	lua_pop(L, 2); /* box, schema */

	static const struct luaL_Reg space_internal_lib[] = {
		{"frommap", lbox_space_frommap},
		{"stat", lbox_space_stat},
		{NULL, NULL}
	};
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal.space", 0);
	luaL_setfuncs(L, space_internal_lib, 0);
	lua_pop(L, 1);
}
