#include <stdio.h>

#include "diag.h"
#include "fiber.h"
#include "lualib.h"
#include "memory.h"
#include "reflection.h"

#include "lua/utils.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
check_error(const char *type, const char *msg)
{
	struct error *err = diag_last_error(&fiber()->diag);
	ok(strcmp(err->type->name, type) == 0,
	   "expected %s, got %s", type, err->type->name);
	ok(strcmp(err->errmsg, msg) == 0,
	   "expected '%s', got '%s'", msg, err->errmsg);
}

static void
test_toerror(lua_State *L)
{
	plan(4);
	header();

	/* test NON Tarantool error on stack */
	lua_pushstring(L, "test Lua error");
	luaT_toerror(L);
	check_error("LuajitError", "test Lua error");
	/*
	 * luaT_toerror adds param on stack thru luaT_tolstring.
	 * Unfortunately the latter is public API.
	 */
	lua_pop(L, 2);

	/* test Tarantool error on stack */
	struct error *e = BuildIllegalParams(__FILE__, __LINE__,
					     "test non-Lua error");
	luaT_pusherror(L, e);
	luaT_toerror(L);
	check_error("IllegalParams", "test non-Lua error");
	lua_pop(L, 1);

	footer();
	check_plan();
}

static void
test_call(lua_State *L)
{
	plan(6);
	header();

	int v;
	const char *expr;

	/* test no error on call */
	expr = "local a = {...} return a[1], a[2]";
	fail_unless(luaL_loadstring(L, expr) == 0);
	lua_pushinteger(L, 3);
	lua_pushinteger(L, 5);
	ok(luaT_call(L, 2, 2) == 0, "call no error");
	v = lua_tointeger(L, -2);
	is(v, 3, "expected 3, got %d", v);
	v = lua_tointeger(L, -1);
	is(v, 5, "expected 5, got %d", v);
	lua_pop(L, 2);

	/* test with error on call */
	expr = "return error('test error')";
	fail_unless(luaL_loadstring(L, expr) == 0);
	ok(luaT_call(L, 0, 0) != 0, "call with error");
	check_error("LuajitError", "test error");
	/* See comment is test_toerror about stack size. */
	lua_pop(L, 2);

	footer();
	check_plan();
}

static void
test_dostring(lua_State *L)
{
	plan(9);
	header();

	int v;
	/* test no error on call */
	ok(luaT_dostring(L, "return 3, 5") == 0, "call no error");
	v = lua_tointeger(L, -2);
	is(v, 3, "expected 3, got %d", v);
	v = lua_tointeger(L, -1);
	is(v, 5, "expected 5, got %d", v);
	lua_pop(L, 2);

	/* test with error on call */
	const char *expr = "return error('test error')";
	ok(luaT_dostring(L, expr) != 0, "call with error");
	check_error("LuajitError", "test error");
	/* See comment is test_toerror about stack size. */
	lua_pop(L, 2);

	/* test code loading error */
	ok(luaT_dostring(L, "*") != 0, "code loading error");
	check_error("LuajitError",
		    "[string \"*\"]:1: unexpected symbol near '*'");
	lua_pop(L, 1);

	footer();
	check_plan();
}

int
main(void)
{
	plan(3);
	header();

	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	memory_init();
	fiber_init(fiber_c_invoke);
	tarantool_lua_error_init(L);

	test_toerror(L);
	test_call(L);
	test_dostring(L);

	fiber_free();
	memory_free();
	lua_close(L);

	footer();
	return check_plan();
}
