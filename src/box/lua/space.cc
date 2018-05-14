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
#include "box/lua/space.h"
#include "box/lua/tuple.h"
#include "lua/utils.h"
#include "lua/trigger.h"

extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
} /* extern "C" */

#include "box/space.h"
#include "box/schema.h"
#include "box/user_def.h"
#include "box/tuple.h"
#include "box/txn.h"
#include "box/vclock.h" /* VCLOCK_MAX */
#include "box/sequence.h"
#include "box/coll_id_cache.h"

/**
 * Trigger function for all spaces
 */
static int
lbox_push_txn_stmt(struct lua_State *L, void *event)
{
	struct txn_stmt *stmt = txn_current_stmt((struct txn *) event);

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
	/* @todo: maybe the space object has to be here */
	lua_pushstring(L, stmt->space->def->name);
	return 3;
}

static int
lbox_pop_txn_stmt(struct lua_State *L, void *event)
{
	struct txn_stmt *stmt = txn_current_stmt((struct txn *) event);

	if (lua_gettop(L) < 1) {
		/* No return value - nothing to do. */
		return 0;
	}

	struct tuple *result = luaT_istuple(L, 1);
	if (result == NULL && !lua_isnil(L, 1) && !luaL_isnull(L, 1)) {
		/* Invalid return value - raise error. */
		diag_set(ClientError, ER_BEFORE_REPLACE_RET,
			 lua_typename(L, lua_type(L, 1)));
		return -1;
	}

	/* Update the new tuple. */
	if (result != NULL)
		tuple_ref(result);
	if (stmt->new_tuple != NULL)
		tuple_unref(stmt->new_tuple);
	stmt->new_tuple = result;
	return 0;
}

/**
 * Set/Reset/Get space.on_replace trigger
 */
static int
lbox_space_on_replace(struct lua_State *L)
{
	int top = lua_gettop(L);

	if (top < 1 || !lua_istable(L, 1)) {
		luaL_error(L,
	   "usage: space:on_replace(function | nil, [function | nil])");
	}
	lua_getfield(L, 1, "id"); /* Get space id. */
	uint32_t id = lua_tonumber(L, lua_gettop(L));
	struct space *space = space_cache_find_xc(id);
	lua_pop(L, 1);

	return lbox_trigger_reset(L, 3, &space->on_replace,
				  lbox_push_txn_stmt, NULL);
}

/**
 * Set/Reset/Get space.before_replace trigger
 */
static int
lbox_space_before_replace(struct lua_State *L)
{
	int top = lua_gettop(L);

	if (top < 1 || !lua_istable(L, 1)) {
		luaL_error(L,
	   "usage: space:before_replace(function | nil, [function | nil])");
	}
	lua_getfield(L, 1, "id"); /* Get space id. */
	uint32_t id = lua_tonumber(L, lua_gettop(L));
	struct space *space = space_cache_find_xc(id);
	lua_pop(L, 1);

	return lbox_trigger_reset(L, 3, &space->before_replace,
				  lbox_push_txn_stmt, lbox_pop_txn_stmt);
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

	/* space.is_temp */
	lua_pushstring(L, "temporary");
	lua_pushboolean(L, space_is_temporary(space));
	lua_settable(L, i);

	/* space.name */
	lua_pushstring(L, "name");
	lua_pushstring(L, space_name(space));
	lua_settable(L, i);

	/* space.engine */
	lua_pushstring(L, "engine");
	lua_pushstring(L, space->def->engine_name);
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

		lua_pushstring(L, index_type_strs[index_def->type]);
		lua_setfield(L, -2, "type");

		lua_pushnumber(L, index_def->iid);
		lua_setfield(L, -2, "id");

		lua_pushnumber(L, space->def->id);
		lua_setfield(L, -2, "space_id");

		lua_pushstring(L, index_def->name);
		lua_setfield(L, -2, "name");

		lua_pushstring(L, "parts");
		lua_newtable(L);

		for (uint32_t j = 0; j < index_def->key_def->part_count; j++) {
			lua_pushnumber(L, j + 1);
			lua_newtable(L);
			const struct key_part *part =
				&index_def->key_def->parts[j];

			lua_pushstring(L, field_type_strs[part->type]);
			lua_setfield(L, -2, "type");

			lua_pushnumber(L, part->fieldno + TUPLE_INDEX_BASE);
			lua_setfield(L, -2, "fieldno");

			lua_pushboolean(L, part->is_nullable);
			lua_setfield(L, -2, "is_nullable");

			if (part->coll_id != COLL_NONE) {
				struct coll_id *coll_id =
					coll_by_id(part->coll_id);
				assert(coll_id != NULL);
				lua_pushstring(L, coll_id->name);
				lua_setfield(L, -2, "collation");
			}

			lua_settable(L, -3); /* index[k].parts[j] */
		}

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

		if (space_is_vinyl(space)) {
			lua_pushstring(L, "options");
			lua_newtable(L);

			lua_pushnumber(L, index_opts->range_size);
			lua_setfield(L, -2, "range_size");

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

static void
box_lua_space_new_or_delete(struct trigger *trigger, void *event)
{
	struct lua_State *L = (struct lua_State *) trigger->data;
	struct space *space = (struct space *) event;

	if (space_by_id(space->def->id) != NULL) {
		box_lua_space_new(L, space);
	} else {
		box_lua_space_delete(L, space->def->id);
	}
}

static struct trigger on_alter_space_in_lua = {
	RLIST_LINK_INITIALIZER, box_lua_space_new_or_delete, NULL, NULL
};

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
		lua_pushstring(L, tt_sprintf("Space with id '%d' "\
					     "doesn't exist", id));
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
			lua_pushstring(L, tt_sprintf("Unknown field '%s'",
						     key));
			return 2;
		}
		lua_rawseti(L, -3, fieldno+1);
	}
	if (table)
		return 1;

	lua_replace(L, 1);
	lua_settop(L, 1);
	return lbox_tuple_new(L);
usage_error:
	return luaL_error(L, "Usage: space:frommap(map, opts)");
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
	lua_pushnumber(L, BOX_VFUNC_ID);
	lua_setfield(L, -2, "VFUNC_ID");
	lua_pushnumber(L, BOX_PRIV_ID);
	lua_setfield(L, -2, "PRIV_ID");
	lua_pushnumber(L, BOX_VPRIV_ID);
	lua_setfield(L, -2, "VPRIV_ID");
	lua_pushnumber(L, BOX_CLUSTER_ID);
	lua_setfield(L, -2, "CLUSTER_ID");
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
	lua_pop(L, 2); /* box, schema */

	static const struct luaL_Reg space_internal_lib[] = {
		{"frommap", lbox_space_frommap},
		{NULL, NULL}
	};
	luaL_register(L, "box.internal.space", space_internal_lib);
	lua_pop(L, 1);
}
