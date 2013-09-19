/*
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
#include "box_lua_space.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include "space.h"
#include <say.h>

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
	lua_pushstring(L, "arity");
	lua_pushnumber(L, space->def.arity);
	lua_settable(L, i);

	/* space.n */
	lua_pushstring(L, "n");
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

	lua_pushstring(L, "enabled");
	lua_pushboolean(L, space->engine.state != READY_NO_KEYS);
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
		/* Empty the table. */
		lua_pushnil(L);  /* first key */
		while (lua_next(L, -2) != 0) {
			lua_pop(L, 1); /* remove the value. */
			lua_pushnil(L); /* set the key to nil. */
			lua_settable(L, -3);
			lua_pushnil(L); /* start over. */
		}
	}
	/*
	 * Fill space.index table with
	 * all defined indexes.
	 */
	for (int i = 0; i <= space->index_id_max; i++) {
		Index *index = space_index(space, i);
		if (index == NULL)
			continue;
		struct key_def *key_def = index->key_def;
		lua_pushnumber(L, key_def->iid);
		lua_newtable(L);		/* space.index[i] */

		lua_pushboolean(L, key_def->is_unique);
		lua_setfield(L, -2, "unique");

		lua_pushstring(L, index_type_strs[key_def->type]);
		lua_setfield(L, -2, "type");

		lua_pushnumber(L, key_def->iid);
		lua_setfield(L, -2, "id");

		lua_pushnumber(L, key_def->space_id);
		lua_setfield(L, -2, "n");

		lua_pushstring(L, key_def->name);
		lua_setfield(L, -2, "name");

		lua_pushstring(L, "key_field");
		lua_newtable(L);

		for (uint32_t j = 0; j < key_def->part_count; j++) {
			lua_pushnumber(L, j);
			lua_newtable(L);

			lua_pushstring(L,
			       field_type_strs[key_def->parts[j].type]);
			lua_setfield(L, -2, "type");

			lua_pushnumber(L, key_def->parts[j].fieldno);
			lua_setfield(L, -2, "fieldno");

			lua_settable(L, -3); /* index[i].key_field[j] */
		}

		lua_settable(L, -3); /* space.index[i].key_field */

		lua_settable(L, -3); /* space.index[i] */
		lua_rawgeti(L, -1, key_def->iid);
		lua_setfield(L, -2, key_def->name);
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
void
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
void
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
