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

static int
fail_push_event(struct lua_State *L, void *event)
{
	(void)event;
	lua_pushliteral(L, "temporary value");
	diag_set(IllegalParams, "push_event failure");
	return -1;
}

static int
fail_trigger(struct lua_State *L)
{
	return luaL_error(L, "trigger failure");
}

static void
test_stack(lbox_push_event_f push_event, const char *name)
{
	plan(2);
	header();
	struct lua_State *L = tarantool_L;
	struct rlist triggers = RLIST_HEAD_INITIALIZER(triggers);
	lua_pushliteral(L, "existing value");
	int top = lua_gettop(L);
	lua_pushcfunction(L, fail_trigger);
	lbox_trigger_reset(L, lua_gettop(L), &triggers, push_event, NULL);
	lua_settop(L, top);
	is(trigger_run(&triggers, NULL), -1, "%s fails", name);
	is(lua_gettop(L), top, "%s restores the stack height", name);

	trigger_destroy(&triggers);
	lua_settop(L, 0);
	diag_clear(diag_get());
	footer();
	check_plan();
}

static int
test_lua_trigger(void)
{
	plan(2);
	header();

	/* Reuse the fiber's stack so leaked values survive the trigger. */
	fiber()->storage.lua.stack = tarantool_L;
	test_stack(fail_push_event, "argument preparation");
	test_stack(NULL, "Lua callback");
	fiber()->storage.lua.stack = NULL;

	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tarantool_L = luaT_newteststate();
	tarantool_lua_error_init(tarantool_L);
	tarantool_lua_utils_init(tarantool_L);
	lua_settop(tarantool_L, 0);

	int rc = test_lua_trigger();

	lua_close(tarantool_L);
	tarantool_L = NULL;
	fiber_free();
	memory_free();
	return rc;
}
