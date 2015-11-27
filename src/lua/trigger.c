/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "lua/trigger.h"
#include "lua/utils.h"
#include <diag.h>
#include <fiber.h>

struct lbox_trigger
{
	struct trigger base;
	/** A reference to Lua trigger function. */
	int ref;
	/*
	 * A pointer to a C function which pushes the
	 * event data to Lua stack as arguments of the
	 * Lua trigger.
	 */
	lbox_push_event_f push_event;
};

static void
lbox_trigger_destroy(struct trigger *ptr)
{
	if (tarantool_L) {
		struct lbox_trigger *trigger = (struct lbox_trigger *) ptr;
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, trigger->ref);
	}
	free(ptr);
}

static void
lbox_trigger_run(struct trigger *ptr, void *event)
{
	struct lbox_trigger *trigger = (struct lbox_trigger *) ptr;
	/*
	 * Create a new coro and reference it. Remove it
	 * from tarantool_L stack, which is a) scarce
	 * b) can be used by other triggers while this
	 * trigger yields, so when it's time to clean
	 * up the coro, we wouldn't know which stack position
	 * it is on.
	 *
	 * XXX: lua_newthread() may throw if out of memory,
	 * this needs to be wrapped with lua_pcall() as well.
	 * Don't, since it's a stupid overhead on every trigger
	 * invocation, and in future we plan to hack into Lua
	 * C API to fix this.
	 */
	struct lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
	int top = trigger->push_event(L, event);
	if (lbox_call(L, top, 0)) {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		diag_raise();
	}
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
}

static struct lbox_trigger *
lbox_trigger_find(struct lua_State *L, int index, struct rlist *list)
{
	struct lbox_trigger *trigger;
	/** Find the old trigger, if any. */
	rlist_foreach_entry(trigger, list, base.link) {
		if (trigger->base.run == lbox_trigger_run) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
			bool found = lua_equal(L, index, lua_gettop(L));
			lua_pop(L, 1);
			if (found)
				return trigger;
		}
	}
	return NULL;
}

static int
lbox_list_all_triggers(struct lua_State *L, struct rlist *list)
{
	struct lbox_trigger *trigger;
	int count = 1;
	lua_newtable(L);
	rlist_foreach_entry_reverse(trigger, list, base.link) {
		if (trigger->base.run == lbox_trigger_run) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
			lua_rawseti(L, -2, count);
			count++;
		}
	}
	return 1;
}

static void
lbox_trigger_check_input(struct lua_State *L, int top)
{
	assert(lua_checkstack(L, top));
	/* Push optional arguments. */
	while (lua_gettop(L) < top)
		lua_pushnil(L);
	/*
	 * (nil, function) is OK, deletes the trigger
	 * (function, nil), is OK, adds the trigger
	 * (function, function) is OK, replaces the trigger
	 * no arguments is OK, lists all trigger
	 * anything else is error.
	 */
	if ((lua_isnil(L, top) && lua_isnil(L, top - 1)) ||
	    (lua_isfunction(L, top) && lua_isnil(L, top - 1)) ||
	    (lua_isnil(L, top) && lua_isfunction(L, top - 1)) ||
	    (lua_isfunction(L, top) && lua_isfunction(L, top - 1)))
		return;

	luaL_error(L, "trigger reset: incorrect arguments");
}

int
lbox_trigger_reset(struct lua_State *L, int top,
		   struct rlist *list, lbox_push_event_f push_event)
{
	/**
	 * If the stack is empty, pushes nils for optional
	 * arguments
	 */
	lbox_trigger_check_input(L, top);
	/* If no args - return triggers table */
	if (lua_isnil(L, top) && lua_isnil(L, top - 1))
		return lbox_list_all_triggers(L, list);

	struct lbox_trigger *trg = lbox_trigger_find(L, top, list);

	if (trg) {
		luaL_unref(L, LUA_REGISTRYINDEX, trg->ref);

	} else if (lua_isfunction(L, top)) {
		luaL_error(L, "trigger reset: Trigger is not found");
	}
	/*
	 * During update of a trigger, we must preserve its
	 * relative position in the list.
	 */
	if (lua_isfunction(L, top - 1)) {
		if (trg == NULL) {
			trg = (struct lbox_trigger *) malloc(sizeof(*trg));
			if (trg == NULL)
				luaL_error(L, "failed to allocate trigger");
			trg->base.run = lbox_trigger_run;
			trg->base.data = NULL;
			trg->base.destroy = lbox_trigger_destroy;
			trg->ref = LUA_NOREF;
			trg->push_event = push_event;
			trigger_add(list, &trg->base);
		}
		/*
		 * Make the new trigger occupy the top
		 * slot of the Lua stack.
		 */
		lua_pop(L, 1);
		/* Reference. */
		trg->ref = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_rawgeti(L, LUA_REGISTRYINDEX, trg->ref);
		return 1;

	} else {
		trigger_clear(&trg->base);
		free(trg);
	}
	return 0;
}
