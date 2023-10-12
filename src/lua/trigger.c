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
#include <tt_static.h>

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
	/**
	 * A pointer to a C function which is called
	 * upon successful execution of the trigger
	 * callback.
	 */
	lbox_pop_event_f pop_event;
	/**
	 * Zero-terminated name of the trigger.
	 * Must be unique within a trigger list.
	 */
	char name[];
};

static void
lbox_trigger_destroy(struct trigger *ptr)
{
	if (tarantool_L) {
		struct lbox_trigger *trigger = (struct lbox_trigger *) ptr;
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, trigger->ref);
		TRASH(trigger);
	}
	TRASH(ptr);
	free(ptr);
}

static int
lbox_trigger_run(struct trigger *ptr, void *event)
{
	struct lbox_trigger *trigger = (struct lbox_trigger *) ptr;
	int rc = -1;
	/*
	 * Create a new coro and reference it. Remove it
	 * from tarantool_L stack, which is a) scarce
	 * b) can be used by other triggers while this
	 * trigger yields, so when it's time to clean
	 * up the coro, we wouldn't know which stack position
	 * it is on.
	 */
	lua_State *L;
	int coro_ref = LUA_NOREF;
	if (fiber()->storage.lua.stack == NULL) {
		L = luaT_newthread(tarantool_L);
		if (L == NULL)
			goto out;
		coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	} else {
		L = fiber()->storage.lua.stack;
		coro_ref = LUA_REFNIL;
	}
	int top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
	int nargs = 0;
	if (trigger->push_event != NULL) {
		nargs = trigger->push_event(L, event);
		if (nargs < 0)
			goto out;
	}
	/*
	 * There are two cases why we can't access `trigger` after
	 * calling it's function:
	 * - trigger can be unregistered and destroyed
	 *   directly in its function.
	 * - trigger function may yield and someone destroy trigger
	 *   at this moment.
	 * So we keep 'trigger->pop_event' in local variable for
	 * further use.
	 */
	lbox_pop_event_f pop_event = trigger->pop_event;
	trigger = NULL;
	if (luaT_call(L, nargs, LUA_MULTRET))
		goto out;
	int nret = lua_gettop(L) - top;
	if (pop_event != NULL &&
	    pop_event(L, nret, event) != 0) {
		lua_settop(L, top);
		goto out;
	}
	/*
	 * Clear the stack after pop_event saves all
	 * the needed return values.
	 */
	lua_settop(L, top);
	rc = 0;
out:
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	return rc;
}

struct lbox_trigger *
lbox_trigger_create(struct lua_State *L, int idx, const char *name,
		    size_t name_len, struct rlist *list,
		    lbox_push_event_f push_event, lbox_pop_event_f pop_event)
{
	assert(name != NULL && name_len > 0);
	struct lbox_trigger *trg = xmalloc(sizeof(*trg) + name_len + 1);
	trigger_create(&trg->base, lbox_trigger_run, NULL,
		       lbox_trigger_destroy);
	lua_pushvalue(L, idx);
	trg->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	trg->push_event = push_event;
	trg->pop_event = pop_event;
	trigger_add(list, &trg->base);
	strlcpy(trg->name, name, name_len + 1);
	return trg;
}

/**
 * Find an lbox_trigger with a particular name in a list of triggers.
 */
static struct lbox_trigger *
lbox_trigger_find(const char *name, struct rlist *list)
{
	struct lbox_trigger *trigger;
	/** Find the old trigger, if any. */
	rlist_foreach_entry(trigger, list, base.link) {
		if (trigger->base.run == lbox_trigger_run &&
		    strcmp(trigger->name, name) == 0)
			return trigger;
	}
	return NULL;
}

static int
lbox_list_all_triggers(struct lua_State *L, struct rlist *list)
{
	struct lbox_trigger *trigger;
	int count = 1;
	lua_newtable(L);
	rlist_foreach_entry(trigger, list, base.link) {
		if (trigger->base.run == lbox_trigger_run) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
			lua_rawseti(L, -2, count);
			count++;
		}
	}
	return 1;
}

/**
 * Checks positional arguments for lbox_trigger_reset.
 * Throws an error if the format is not suitable.
 */
static void
lbox_trigger_check_positional_input(struct lua_State *L, int bottom)
{
	/* Push optional arguments. */
	lua_settop(L, bottom + 2);

	/*
	 * (nil, function) is OK, deletes the trigger
	 * (function, nil), is OK, adds the trigger
	 * (function, function) is OK, replaces the trigger
	 * no arguments is OK, lists all trigger
	 * anything else is error.
	 */
	bool ok = true;
	/* Name must be a string if it is passed. */
	ok = ok && (lua_isnil(L, bottom + 2) || luaL_isnull(L, bottom + 2) ||
		    lua_type(L, bottom + 2) == LUA_TSTRING);
	ok = ok && (lua_isnil(L, bottom + 1) || luaL_isnull(L, bottom + 1) ||
		    luaL_iscallable(L, bottom + 1));
	ok = ok && (lua_isnil(L, bottom) || luaL_isnull(L, bottom) ||
		    luaL_iscallable(L, bottom));
	if (!ok)
		luaL_error(L, "trigger reset: incorrect arguments");
}

/**
 * Sets or deletes lbox_trigger by name depending on passed arguments.
 * Value at name_idx must be a string, value at func_idx must be a callable
 * object, nil or box.NULL. Otherwise, an error will be thrown.
 */
static int
lbox_trigger_reset_by_name(struct lua_State *L, struct rlist *list,
			   lbox_push_event_f push_event,
			   lbox_pop_event_f pop_event,
			   int name_idx, int func_idx)
{
	if (lua_type(L, name_idx) != LUA_TSTRING)
		luaL_error(L, "name must be a string");
	int ret_count = 0;
	size_t name_len = 0;
	const char *name = lua_tolstring(L, name_idx, &name_len);
	struct lbox_trigger *old_trg = lbox_trigger_find(name, list);
	if (old_trg != NULL)
		list = &old_trg->base.link;
	if (luaL_iscallable(L, func_idx)) {
		lbox_trigger_create(L, func_idx, name, name_len, list,
				    push_event, pop_event);
		lua_pushvalue(L, func_idx);
		ret_count++;
	} else if (!lua_isnil(L, func_idx) && !luaL_isnull(L, func_idx)) {
		return luaL_error(L, "func must be a callable object or nil");
	}
	if (old_trg != NULL) {
		trigger_clear(&old_trg->base);
		lbox_trigger_destroy(&old_trg->base);
	}
	return ret_count;
}

int
lbox_trigger_reset(struct lua_State *L, int bottom, struct rlist *list,
		   lbox_push_event_f push_event, lbox_pop_event_f pop_event)
{
	assert(L != NULL);
	assert(bottom >= 1);
	assert(list != NULL);
	/* Use key-value API if the first argument is a non-callable table. */
	if (lua_gettop(L) == bottom && lua_istable(L, -1) &&
	    !luaL_iscallable(L, -1)) {
		lua_getfield(L, bottom, "name");
		lua_getfield(L, bottom, "func");
		return lbox_trigger_reset_by_name(L, list, push_event,
						  pop_event, -2, -1);
	}
	/**
	 * If the stack is empty, pushes nils for optional
	 * arguments
	 */
	lbox_trigger_check_positional_input(L, bottom);
	const int top = bottom + 2;
	if (!lua_isnil(L, top) && !luaL_isnull(L, top))
		return lbox_trigger_reset_by_name(L, list, push_event,
						  pop_event, top, bottom);
	/* If no args - return triggers table */
	if ((lua_isnil(L, bottom) || luaL_isnull(L, bottom)) &&
	    (lua_isnil(L, bottom + 1) || luaL_isnull(L, bottom + 1)))
		return lbox_list_all_triggers(L, list);

	int ret_count = 0;

	const void *old_handler = NULL;
	const char *old_name = NULL;
	struct lbox_trigger *old_trg = NULL;
	if (luaL_iscallable(L, bottom + 1)) {
		old_handler = lua_topointer(L, bottom + 1);
		old_name = tt_sprintf("%p", old_handler);
		old_trg = lbox_trigger_find(old_name, list);
		if (old_trg == NULL)
			return luaL_error(L, "trigger reset: "
					  "Trigger is not found");
	}
	const void *new_handler = NULL;
	const char *new_name = NULL;
	if (luaL_iscallable(L, bottom)) {
		new_handler = lua_topointer(L, bottom);
		new_name = tt_sprintf("%p", new_handler);
		ret_count = 1;
		lua_pushvalue(L, bottom);
	}
	/*
	 * Function lua_topointer can return NULL, so let's use names to check
	 * if handlers are passed - they are assured not to be NULL in the case.
	 */
	if (new_name != NULL && old_name != NULL) {
		if (old_handler == new_handler) {
			/* Triggers are the same - do nothing. */
			goto out;
		} else {
			trigger_clear(&old_trg->base);
			lbox_trigger_destroy(&old_trg->base);
			/*
			 * Delete a trigger with new name to surely place the
			 * new trigger at the beginning of the trigger list.
			 */
			old_trg = lbox_trigger_find(new_name, list);
			if (old_trg != NULL) {
				trigger_clear(&old_trg->base);
				lbox_trigger_destroy(&old_trg->base);
			}
			lbox_trigger_create(L, bottom, new_name,
					    strlen(new_name), list,
					    push_event, pop_event);
		}
	} else if (old_name != NULL) {
		trigger_clear(&old_trg->base);
		lbox_trigger_destroy(&old_trg->base);
	} else {
		assert(new_name != NULL);
		old_trg = lbox_trigger_find(new_name, list);
		if (old_trg == NULL) {
			lbox_trigger_create(
				L, bottom, new_name, strlen(new_name), list,
				push_event, pop_event);
		}
	}
out:
	return ret_count;
}

const char *trigger_list_typename = "trigger.trigger_list";

/**
 * Gets trigger_list from Lua stack with type check.
 */
static inline struct rlist *
luaT_check_trigger_list(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, trigger_list_typename);
}

/**
 * Creates new trigger list in Lua.
 */
static int
luaT_trigger_list_new(struct lua_State *L)
{
	struct rlist *trigger_list = lua_newuserdata(L, sizeof(*trigger_list));
	rlist_create(trigger_list);
	luaL_getmetatable(L, trigger_list_typename);
	lua_setmetatable(L, -2);
	return 1;
}

/**
 * Runs all triggers from trigger list with passed arguments.
 */
static int
luaT_trigger_list_run(struct lua_State *L)
{
	struct rlist *trigger_list = luaT_check_trigger_list(L, 1);
	int top = lua_gettop(L);
	struct lbox_trigger *trigger;
	rlist_foreach_entry(trigger, trigger_list, base.link) {
		/* Only lbox_trigger is expected to be here. */
		assert(trigger->base.run == lbox_trigger_run);
		lua_rawgeti(L, LUA_REGISTRYINDEX, trigger->ref);
		for (int i = 2; i <= top; i++)
			lua_pushvalue(L, i);
		if (luaT_call(L, top - 1, 0) != 0)
			luaT_error(L);
	}
	return 0;
}

/**
 * Metamethod __call for trigger list.
 * See description of lbox_trigger_reset for details.
 */
static int
luaT_trigger_list_call(struct lua_State *L)
{
	struct rlist *trigger_list = luaT_check_trigger_list(L, 1);
	return lbox_trigger_reset(L, 2, trigger_list, NULL, NULL);
}

/**
 * Destroys a trigger list.
 */
static int
luaT_trigger_list_gc(struct lua_State *L)
{
	struct rlist *trigger_list = luaT_check_trigger_list(L, 1);
	trigger_destroy(trigger_list);
	return 0;
}

void
tarantool_lua_trigger_init(struct lua_State *L)
{
	const struct luaL_Reg module_funcs[] = {
		{"new", luaT_trigger_list_new},
		{NULL, NULL}
	};
	luaT_newmodule(L, "internal.trigger", module_funcs);
	lua_pop(L, 1);
	const struct luaL_Reg trigger_list_methods[] = {
		{"run", luaT_trigger_list_run},
		{"__call", luaT_trigger_list_call},
		{"__gc", luaT_trigger_list_gc},
		{NULL, NULL}
	};
	luaL_register_type(L, trigger_list_typename,
			   trigger_list_methods);
}
