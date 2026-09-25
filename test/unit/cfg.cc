/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/box.h"
#include "cfg.h"
#include "diag.h"
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

static void
test_quorum_formula_error(struct lua_State *L)
{
	plan(2);
	header();

	const char *config =
		"box = {cfg = {replication_synchro_quorum = '('}}";
	fail_unless(luaL_dostring(L, config) == 0);
	lua_pushliteral(L, "existing value");
	int top = lua_gettop(L);

	is(box_set_replication_synchro_quorum(), -1,
	   "Invalid quorum formula is rejected");
	is(lua_gettop(L), top, "Formula error preserves the stack height");
	lua_settop(L, 0);
	diag_clear(diag_get());

	footer();
	check_plan();
}

int
main(void)
{
	plan(2);
	header();

	memory_init();
	fiber_init(fiber_c_invoke);
	struct lua_State *L = luaT_newteststate();
	tarantool_L = L;

	test_numeric_default_getters(L);
	test_quorum_formula_error(L);

	lua_close(L);
	tarantool_L = NULL;
	fiber_free();
	memory_free();

	footer();
	return check_plan();
}
