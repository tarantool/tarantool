/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/lua/func_adapter.h"
#include "core/event.h"
#include <diag.h>
#include <fiber.h>
#include "lua/utils.h"

/**
 * Sets a trigger with passed name to the passed event.
 * The first argument is event name, the second one is trigger name, the third
 * one is new trigger handler - it can be a Lua function or another callable
 * object. If there is an already registered trigger with such name in the
 * event, it is replaced with new trigger.
 * Returns new trigger handler (which was passed as the third argument).
 */
static int
luaT_trigger_set(struct lua_State *L)
{
	if (lua_gettop(L) != 3)
		luaL_error(L, "Usage: trigger.set(event, trigger, function)");
	const char *event_name = luaL_checkstring(L, 1);
	const char *trigger_name = luaL_checkstring(L, 2);
	if (!luaL_iscallable(L, 3))
		luaL_typerror(L, 3, "callable");
	/*
	 * The following code is written in assumption no error will be thrown.
	 */
	struct event *event = event_get(event_name, true);
	assert(event != NULL);
	struct func_adapter *func = func_adapter_lua_create(L, 3);
	event_reset_trigger(event, trigger_name, func);
	/* The new handler is still at the top of the stack. */
	return 1;
}

/**
 * Deletes a trigger with passed name from passed event.
 * The first argument is event name, the second one is trigger name.
 * Returns deleted trigger handler.
 */
static int
luaT_trigger_del(struct lua_State *L)
{
	if (lua_gettop(L) != 2)
		luaL_error(L, "Usage: trigger.del(event, trigger)");
	const char *event_name = luaL_checkstring(L, 1);
	const char *trigger_name = luaL_checkstring(L, 2);
	struct event *event = event_get(event_name, false);
	if (event == NULL)
		return 0;
	struct func_adapter *old = event_find_trigger(event, trigger_name);
	if (old == NULL)
		return 0;
	func_adapter_lua_get_func(old, L);
	event_reset_trigger(event, trigger_name, NULL);
	return 1;
}

/**
 * Calls all the triggers registered on passed event with variable number of
 * arguments. Execution is stopped by a first exception.
 * First argument must be a string - all the other arguments will be passed
 * to the triggers without any processing or copying.
 * Returns no values on success. If one of the triggers threw an error, it is
 * raised again.
 */
static int
luaT_trigger_call(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "Usage: trigger.call(event, [args...])");
	const char *event_name = luaL_checkstring(L, 1);
	struct event *event = event_get(event_name, false);
	if (event == NULL)
		return 0;
	int top = lua_gettop(L);
	int narg = top - 1;
	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	struct func_adapter *trigger = NULL;
	const char *name = NULL;
	int rc = 0;
	while (rc == 0 && event_trigger_iterator_next(&it, &trigger, &name)) {
		func_adapter_lua_get_func(trigger, L);
		for (int i = top - narg + 1; i <= top; ++i)
			lua_pushvalue(L, i);
		rc = luaT_call(L, narg, 0);
	}
	event_trigger_iterator_destroy(&it);
	if (rc != 0)
		return luaT_error(L);
	return 0;
}

/**
 * Sets an array of arrays [trigger_name, trigger_handler] by event->name key
 * in a pre-created table. Never sets an empty array.
 */
static bool
trigger_info_push_event(struct event *event, void *arg)
{
	lua_State *L = (lua_State *)arg;
	struct event_trigger_iterator it;
	struct func_adapter *trigger = NULL;
	const char *name = NULL;
	int idx = 0;
	lua_createtable(L, 0, 0);
	event_trigger_iterator_create(&it, event);
	while (event_trigger_iterator_next(&it, &trigger, &name)) {
		idx++;
		lua_createtable(L, 2, 0);
		lua_pushstring(L, name);
		lua_rawseti(L, -2, 1);
		func_adapter_lua_get_func(trigger, L);
		lua_rawseti(L, -2, 2);
		lua_rawseti(L, -2, idx);
	}
	event_trigger_iterator_destroy(&it);
	lua_setfield(L, -2, event->name);
	return true;
}

/**
 * Pushes a key-value table, where the key is the event name and value is an
 * array of triggers, represented by two-element [trigger_name, trigger_handler]
 * arrays, registered on this event, in the order in which they will be called.
 * If an event name is passed, a table contains only one key which is passed
 * argument, if there is an event with such a name, or returned table is empty,
 * if the event does not exist.
 */
static int
luaT_trigger_info(struct lua_State *L)
{
	if (lua_gettop(L) > 1)
		luaL_error(L, "Usage: trigger.info([event])");
	if (lua_gettop(L) == 0) {
		lua_createtable(L, 0, 0);
		bool ok = event_foreach(trigger_info_push_event, L);
		assert(ok);
		(void)ok;
	} else {
		const char *event_name = luaL_checkstring(L, 1);
		struct event *event = event_get(event_name, false);
		if (event == NULL || !event_has_triggers(event)) {
			lua_createtable(L, 0, 0);
			return 1;
		}
		lua_createtable(L, 0, 1);
		trigger_info_push_event(event, L);
	}
	return 1;
}

static const char *event_trigger_iterator_typename =
	"trigger.event_trigger_iterator";

/**
 * Gets event_trigger_iterator from Lua stack with type check.
 */
static inline struct event_trigger_iterator *
luaT_check_event_trigger_iterator(struct lua_State *L, int idx)
{
	return luaL_checkudata(L, idx, event_trigger_iterator_typename);
}

/**
 * Takes an iterator step.
 */
static int
luaT_trigger_iterator_next(struct lua_State *L)
{
	struct event_trigger_iterator *it =
		luaT_check_event_trigger_iterator(L, 1);
	struct func_adapter *trigger = NULL;
	const char *name = NULL;
	if (event_trigger_iterator_next(it, &trigger, &name)) {
		lua_pushstring(L, name);
		func_adapter_lua_get_func(trigger, L);
		return 2;
	}
	return 0;
}

/**
 * Takes an iterator step of exhausted iterator.
 */
static int
luaT_trigger_iterator_next_exhausted(struct lua_State *L)
{
	(void)L;
	return 0;
}

/**
 * Destroys an iterator.
 */
static int
luaT_trigger_iterator_gc(struct lua_State *L)
{
	struct event_trigger_iterator *it =
		luaT_check_event_trigger_iterator(L, 1);
	event_trigger_iterator_destroy(it);
	TRASH(it);
	return 0;
}

/**
 * Creates iterator over triggers of event with passed name.
 * The iterator yields a pair [trigger_name, trigger_handler].
 * Return next method of iterator and iterator itself.
 */
static int
luaT_trigger_pairs(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "Usage: trigger.pairs(event)");
	const char *event_name = luaL_checkstring(L, 1);
	struct event *event = event_get(event_name, false);
	if (event == NULL) {
		lua_pushcfunction(L, luaT_trigger_iterator_next_exhausted);
		return 1;
	}
	lua_pushcfunction(L, luaT_trigger_iterator_next);
	struct event_trigger_iterator *it = lua_newuserdata(L, sizeof(*it));
	event_trigger_iterator_create(it, event);
	luaL_getmetatable(L, event_trigger_iterator_typename);
	lua_setmetatable(L, -2);
	return 2;
}

void
box_lua_trigger_init(struct lua_State *L)
{
	const struct luaL_Reg module_funcs[] = {
		{"set", luaT_trigger_set},
		{"del", luaT_trigger_del},
		{"call", luaT_trigger_call},
		{"info", luaT_trigger_info},
		{"pairs", luaT_trigger_pairs},
		{NULL, NULL}
	};
	luaT_newmodule(L, "trigger", module_funcs);
	lua_pop(L, 1);
	const struct luaL_Reg trigger_iterator_methods[] = {
		{"__gc", luaT_trigger_iterator_gc},
		{NULL, NULL}
	};
	luaL_register_type(L, event_trigger_iterator_typename,
			   trigger_iterator_methods);
}
