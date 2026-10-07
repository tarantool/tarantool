/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "diag.h"
#include "fiber.h"
#include "lua/error.h"
#include "lua/trigger.h"
#include "lua/utils.h"
#include "memory.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "lua_test_utils.h"

/** Stages at which a trigger can fail. */
enum failure {
	FAILURE_NONE,
	FAILURE_PUSH,
	FAILURE_LUA,
	FAILURE_BOX,
	FAILURE_POP,
	failure_MAX,
};

static int
push_event(struct lua_State *L, void *event)
{
	enum failure failure = *(enum failure *)event;
	lua_pushinteger(L, failure);
	if (failure == FAILURE_PUSH) {
		diag_set(IllegalParams, "trigger failure");
		return -1;
	}
	return 1;
}

static int
run_trigger(struct lua_State *L)
{
	enum failure failure = lua_tointeger(L, 1);
	if (failure == FAILURE_LUA)
		return luaL_error(L, "trigger failure");
	if (failure == FAILURE_BOX) {
		diag_set(IllegalParams, "trigger failure");
		return luaT_error(L);
	}
	lua_pushinteger(L, 42);
	return 1;
}

static int
pop_event(struct lua_State *L, int nret, void *event)
{
	fail_unless(nret == 1 && lua_tointeger(L, -1) == 42);
	if (*(enum failure *)event == FAILURE_POP) {
		lua_pushliteral(L, "temporary value");
		diag_set(IllegalParams, "trigger failure");
		return -1;
	}
	return 0;
}

static void
test_stack(bool reuse_stack, enum failure failure)
{
	plan(3 * (failure == FAILURE_NONE ? 3 : 5));
	header();
	struct lua_State *L = tarantool_L;
	fiber()->storage.lua.stack = reuse_stack ? L : NULL;
	struct rlist triggers = RLIST_HEAD_INITIALIZER(triggers);
	lua_pushinteger(L, 123);
	lua_pushcfunction(L, run_trigger);
	lua_pushnil(L);
	lua_pushliteral(L, "test");
	lbox_trigger_reset(L, 2, &triggers, push_event, pop_event);
	lua_settop(L, 1);

	for (int i = 0; i < 3; ++i) {
		int rc = trigger_run(&triggers, &failure);
		is(rc, failure == FAILURE_NONE ? 0 : -1,
		   "trigger result (reuse=%d, failure=%d)",
		   reuse_stack, failure);
		is(lua_gettop(L), 1, "stack height is preserved");
		is(lua_tointeger(L, 1), 123, "stack value is preserved");
		if (failure != FAILURE_NONE) {
			struct error *err = diag_last_error(diag_get());
			fail_unless(err != NULL);
			is(strcmp(err->errmsg, "trigger failure"), 0,
			   "error message in the diag is preserved");
			const char *type = failure == FAILURE_LUA ?
					   "LuajitError" : "IllegalParams";
			is(strcmp(err->type->name, type), 0,
			   "error type in the diag is preserved");
		}
		diag_clear(diag_get());
	}

	trigger_destroy(&triggers);
	lua_settop(L, 0);
	fiber()->storage.lua.stack = NULL;
	footer();
	check_plan();
}

int
main(void)
{
	plan(2 * failure_MAX);
	memory_init();
	fiber_init(fiber_c_invoke);
	tarantool_L = luaT_newteststate();
	tarantool_lua_error_init(tarantool_L);
	tarantool_lua_utils_init(tarantool_L);
	lua_settop(tarantool_L, 0);
	for (int reuse = 0; reuse < 2; ++reuse) {
		for (int failure = 0; failure < failure_MAX; ++failure)
			test_stack(reuse, failure);
	}
	lua_close(tarantool_L);
	tarantool_L = NULL;
	fiber_free();
	memory_free();
	return check_plan();
}
