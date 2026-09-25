/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "cfg.h"
#include "fiber.h"
#include "lua/utils.h"
#include "memory.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "lua_test_utils.h"

static void
test_numeric_default_getters(struct lua_State *L)
{
	plan(4);
	header();

	const char *config = "box = {cfg = {integer = 1, number = 0.5}}";
	fail_unless(luaL_dostring(L, config) == 0);
	lua_pushliteral(L, "existing value");
	int top = lua_gettop(L);

	is(cfg_geti_default("integer", 0), 1, "Read integer value");
	is(lua_gettop(L), top, "Integer getter preserves the stack height");
	lua_settop(L, top);

	is(cfg_getd_default("number", 0), 0.5, "Read double value");
	is(lua_gettop(L), top, "Double getter preserves the stack height");
	lua_settop(L, 0);

	footer();
	check_plan();
}

int
main(void)
{
	plan(1);
	header();

	memory_init();
	fiber_init(fiber_c_invoke);
	struct lua_State *L = luaT_newteststate();
	tarantool_L = L;

	test_numeric_default_getters(L);

	lua_close(L);
	tarantool_L = NULL;
	fiber_free();
	memory_free();

	footer();
	return check_plan();
}
