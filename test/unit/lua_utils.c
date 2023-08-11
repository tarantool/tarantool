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
	fail_if(lua_gettop(L) != 2);
	v = lua_tointeger(L, -2);
	is(v, 3, "got %d", v);
	v = lua_tointeger(L, -1);
	is(v, 5, "got %d", v);
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
	plan(11);
	header();

	int v;
	/* test no error on call */
	ok(luaT_dostring(L, "return 3, 5") == 0, "call no error");
	fail_if(lua_gettop(L) != 2);
	v = lua_tointeger(L, -2);
	is(v, 3, "got %d", v);
	v = lua_tointeger(L, -1);
	is(v, 5, "got %d", v);
	lua_pop(L, 2);

	/* test with error on call */
	const char *expr = "return error('test error')";
	ok(luaT_dostring(L, expr) != 0, "call with error");
	check_error("LuajitError", "test error");
	ok(lua_gettop(L) == 0, "got %d", lua_gettop(L));

	/* test code loading error */
	ok(luaT_dostring(L, "*") != 0, "code loading error");
	check_error("LuajitError",
		    "[string \"*\"]:1: unexpected symbol near '*'");
	ok(lua_gettop(L) == 0, "got %d", lua_gettop(L));

	footer();
	check_plan();
}

static void
test_tolstring_strict(lua_State *L)
{
	plan(3);
	header();

	size_t len;
	const char *s;

	lua_pushstring(L, "foo");
	s = luaL_tolstring_strict(L, -1, &len);
	is(len, 3, "string length");
	is(strcmp(s, "foo"), 0, "string data");
	lua_pop(L, 1);

	lua_pushnumber(L, 42);
	is(luaL_tolstring_strict(L, -1, &len), NULL, "number")
	lua_pop(L, 1);

	footer();
	check_plan();
}

static void
test_tointeger_strict(lua_State *L)
{
	plan(6);
	header();

	int val;

	lua_pushnumber(L, 42);
	ok(luaL_tointeger_strict(L, -1, &val), "integer status");
	is(val, 42, "integer value");
	lua_pop(L, 1);

	lua_pushnumber(L, 42.5);
	ok(!luaL_tointeger_strict(L, -1, &val), "floating point number");
	lua_pop(L, 1);

	lua_pushnumber(L, 1e42);
	ok(!luaL_tointeger_strict(L, -1, &val), "big positive number");
	lua_pop(L, 1);

	lua_pushnumber(L, -1e42);
	ok(!luaL_tointeger_strict(L, -1, &val), "big negative number");
	lua_pop(L, 1);

	lua_pushstring(L, "42");
	ok(!luaL_tointeger_strict(L, -1, &val), "string convertible to number");
	lua_pop(L, 1);

	footer();
	check_plan();
}

int
main(void)
{
	plan(5);
	header();

	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	memory_init();
	fiber_init(fiber_c_invoke);
	tarantool_lua_error_init(L);

	test_toerror(L);
	test_call(L);
	test_dostring(L);
	test_tolstring_strict(L);
	test_tointeger_strict(L);

	fiber_free();
	memory_free();
	lua_close(L);

	footer();
	return check_plan();
}
