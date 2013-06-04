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
int
lbox_pushspace(struct lua_State *L, struct space *space)
{
	lua_newtable(L);

	/* space.cardinality */
	lua_pushstring(L, "cardinality");
	lua_pushnumber(L, space->arity);
	lua_settable(L, -3);

	/* space.n */
	lua_pushstring(L, "n");
	lua_pushnumber(L, space->no);
	lua_settable(L, -3);

	/* all exists spaces are enabled */
	lua_pushstring(L, "enabled");
	lua_pushboolean(L, 1);
	lua_settable(L, -3);

	/* legacy field */
	lua_pushstring(L, "estimated_rows");
	lua_pushnumber(L, 0);
	lua_settable(L, -3);

	/* space.index */
	lua_pushstring(L, "index");
	lua_newtable(L);
	/*
	 * Fill space.index table with
	 * all defined indexes.
	 */
	for (int i = 0; i < space->key_count; i++) {
		lua_pushnumber(L, i);
		lua_newtable(L);		/* space.index[i] */

		lua_pushstring(L, "unique");
		lua_pushboolean(L, space->key_defs[i].is_unique);
		lua_settable(L, -3);

		lua_pushstring(L, "type");

		lua_pushstring(L, index_type_strs[space->key_defs[i].type]);
		lua_settable(L, -3);

		lua_pushstring(L, "key_field");
		lua_newtable(L);

		for (u32 j = 0; j < space->key_defs[i].part_count; j++) {
			lua_pushnumber(L, j);
			lua_newtable(L);

			lua_pushstring(L, "type");
			switch (space->key_defs[i].parts[j].type) {
			case NUM:
				lua_pushstring(L, "NUM");
				break;
			case NUM64:
				lua_pushstring(L, "NUM64");
				break;
			case STRING:
				lua_pushstring(L, "STR");
				break;
			default:
				lua_pushstring(L, "UNKNOWN");
				break;
			}
			lua_settable(L, -3);

			lua_pushstring(L, "fieldno");
			lua_pushnumber(L, space->key_defs[i].parts[j].fieldno);
			lua_settable(L, -3);

			lua_settable(L, -3);
		}

		lua_settable(L, -3);	/* space[i].key_field */

		lua_settable(L, -3);	/* space[i] */
	}

	lua_settable(L, -3);	/* push space.index */

	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "bless_space");
	lua_gettable(L, -2);

	lua_pushvalue(L, -3);			/* box, bless, space */
	lua_call(L, 1, 0);
	lua_pop(L, 1);	/* cleanup stack */

	return 1;
}

/**
 * A callback adapter for space_foreach().
 */
static void
lbox_add_space(struct space *space, struct lua_State *L)
{
	lua_pushnumber(L, space->no);
	lbox_pushspace(L, space);
	lua_settable(L, -3);
}

static void
lbox_add_space_wrapper(struct space *space, void *param)
{
	lbox_add_space(space, (struct lua_State *) param);
}

/**
 * Make all spaces available in Lua via box.space
 * array.
 */
void
box_lua_load_cfg(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "space");
	lua_newtable(L);
	space_foreach(lbox_add_space_wrapper, L);	/* fill box.space */
	lua_settable(L, -3);
	lua_pop(L, 1);
	assert(lua_gettop(L) == 0);
}
