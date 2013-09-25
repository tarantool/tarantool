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
#include "lua/utils.h"

extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
} /* extern "C" */

#include "space.h"
#include "schema.h"
#include <trigger.h>
#include <rlist.h>
#include <scoped_guard.h>
#include "box_lua.h"
#include "txn.h"


/**
 * Run user trigger with lua context
 */
static void
space_user_trigger_luactx(struct lua_State *L, va_list ap)
{
	struct lua_trigger *trigger = va_arg(ap, struct lua_trigger *);
	struct txn *txn = va_arg(ap, struct txn *);

	lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);

	if (txn->old_tuple)
		lbox_pushtuple(L, txn->old_tuple);
	else
		lua_pushnil(L);

	if (txn->new_tuple)
		lbox_pushtuple(L, txn->new_tuple);
	else
		lua_pushnil(L);

	/* TODO: may me space object have to be here */
	lua_pushstring(L, txn->space->def.name);

	lua_call(L, 3, 0);
}

/**
 * Trigger function for all spaces
 */
static void
space_user_trigger(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	box_luactx(space_user_trigger_luactx, trigger, txn);
}

/**
 * lua_trigger destroy method with lua context
 */
static void
space_user_trigger_destroy_luaref(struct lua_State *L, va_list ap)
{
	int ref = va_arg(ap, int);
	luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

/**
 * destroy trigger method (can be called from space_delete method)
 */
static void
space_user_trigger_destroy(struct trigger *trigger)
{
	struct lua_trigger *lt = (struct lua_trigger *)trigger;
	trigger_clear(trigger);
	box_luactx(space_user_trigger_destroy_luaref, lt->ref);
	free(trigger);
}

/**
 * Set/Reset/Get space.on_replace trigger
 */
static int
lbox_space_on_replace_trigger(struct lua_State *L)
{
	int top = lua_gettop(L);

	if ( top == 0 || !lua_istable(L, 1))
		luaL_error(L, "usage: space:on_replace "
				"instead space.on_replace");

	lua_pushstring(L, "n");
	lua_rawget(L, 1);
	if (lua_isnil(L, -1))
		luaL_error(L, "Can't find space.n property");

	int sno = lua_tointeger(L, -1);
	lua_pop(L, 1);


	struct space *space = space_find(sno);


	struct trigger *trigger;
	struct lua_trigger *current = NULL;
	rlist_foreach_entry(trigger, &space->on_replace, link) {
		if (trigger->run == space_user_trigger) {
			current = (struct lua_trigger *)trigger;
			break;
		}
	}


	/* get current trigger function */
	if (top == 1) {
		if (!current) {
			lua_pushnil(L);
			return 1;
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, current->ref);
		return 1;
	}

	/* set or re-set the trigger */
	if (!lua_isfunction(L, 2) && !lua_isnil(L, 2)) {
		luaL_error(L,
				"usage: space:on_replace([ function | nil ])");
	}

	/* cleanup trigger */
	if (lua_isnil(L, 2)) {
		if (current) {
			luaL_unref(L, LUA_REGISTRYINDEX, current->ref);
			trigger_clear(&current->trigger);
			free(current);
		}
		lua_pushnil(L);
		return 1;
	}



	/* save ref on trigger function */
	lua_pushvalue(L, 2);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	if (current) {
		luaL_unref(L, LUA_REGISTRYINDEX, current->ref);
		current->ref = cb_ref;
		lua_pushvalue(L, 2);
		return 1;
	}

	auto scoped_guard = make_scoped_guard([&] {
		luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
	});


	current = (struct lua_trigger *)malloc(sizeof(struct lua_trigger));

	if (!current)
		luaL_error(L, "Can't allocate memory for trigger");

	current->trigger.run = space_user_trigger;
	current->trigger.destroy = space_user_trigger_destroy;
	current->ref = cb_ref;
	trigger_set(&space->on_replace, &current->trigger);

	scoped_guard.is_active = false;
	lua_pushvalue(L, 2);
	return 1;

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
	lua_pushboolean(L, space_index(space, 0) != 0);
	lua_settable(L, i);


        /* space:on_replace */
        lua_pushstring(L, "on_replace");
        lua_pushcfunction(L, lbox_space_on_replace_trigger);
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
