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
	is(luaL_tolstring_strict(L, -1, &len), NULL, "number");
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

static int
checkstring_cb(lua_State *L)
{
	lua_pushboolean(L, true);
	luaT_checkstring(L, -1);
	return 0;
}

static void
test_checkstring(lua_State *L)
{
	plan(6);
	header();

	const char *str;

	lua_pushstring(L, "foo");
	str = luaT_checkstring(L, -1);
	ok(strcmp(str, "foo") == 0, "got '%s'", str);
	lua_pop(L, 1);

	lua_pushnumber(L, 11);
	str = luaT_checkstring(L, -1);
	ok(strcmp(str, "11") == 0, "got '%s'", str);
	lua_pop(L, 1);

	lua_pushnumber(L, 36.6);
	str = luaT_checkstring(L, -1);
	ok(strcmp(str, "36.6") == 0, "got '%s'", str);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checkstring_cb, NULL) == LUA_ERRRUN, "error status");
	struct error *err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected string as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	footer();
	check_plan();
}

static int
checklstring_cb(lua_State *L)
{
	lua_pushboolean(L, true);
	size_t len;
	luaT_checklstring(L, -1, &len);
	return 0;
}

static void
test_checklstring(lua_State *L)
{
	plan(9);
	header();

	const char *str;
	size_t len;

	lua_pushstring(L, "foo");
	str = luaT_checklstring(L, -1, &len);
	ok(strcmp(str, "foo") == 0, "got '%s'", str);
	ok(strlen("foo") == len, "got %zd", len);
	lua_pop(L, 1);

	lua_pushnumber(L, 11);
	str = luaT_checklstring(L, -1, &len);
	ok(strcmp(str, "11") == 0, "got '%s'", str);
	ok(strlen("11") == len, "got %zd", len);
	lua_pop(L, 1);

	lua_pushnumber(L, 36.6);
	str = luaT_checklstring(L, -1, &len);
	ok(strcmp(str, "36.6") == 0, "got '%s'", str);
	ok(strlen("36.6") == len, "got %zd", len);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checklstring_cb, NULL) == LUA_ERRRUN, "error status");
	struct error *err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected string as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	footer();
	check_plan();
}

static int
checkint_cb(lua_State *L)
{
	lua_pushnil(L);
	luaT_checkint(L, -1);
	return 0;
}

static void
test_checkint(lua_State *L)
{
	plan(6);
	header();

	int i;

	lua_pushnumber(L, 11);
	i = luaT_checkint(L, -1);
	ok(i == 11, "got %d", i);
	lua_pop(L, 1);

	lua_pushnumber(L, 36.6);
	i = luaT_checkint(L, -1);
	ok(i == 36, "got %d", i);
	lua_pop(L, 1);

	lua_pushstring(L, "36.6");
	i = luaT_checkint(L, -1);
	ok(i == 36, "got %d", i);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checkint_cb, NULL) == LUA_ERRRUN, "error status");
	struct error *err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected integer as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	footer();
	check_plan();
}

static int
checknumber_cb(lua_State *L)
{
	lua_pushboolean(L, false);
	luaT_checknumber(L, -1);
	return 0;
}

static void
test_checknumber(lua_State *L)
{
	plan(6);
	header();

	double f;

	lua_pushnumber(L, 11);
	f = luaT_checknumber(L, -1);
	ok(f == 11, "got %f", f);
	lua_pop(L, 1);

	lua_pushnumber(L, 36.6);
	f = luaT_checknumber(L, -1);
	ok(f == 36.6, "got %f", f);
	lua_pop(L, 1);

	lua_pushstring(L, "36.6");
	f = luaT_checknumber(L, -1);
	ok(f == 36.6, "got %f", f);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checknumber_cb, NULL) == LUA_ERRRUN, "error status");
	struct error *err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected number as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	footer();
	check_plan();
}

static int
checkudata_udata_cb(lua_State *L)
{
	lua_newuserdata(L, 1);
	luaL_getmetatable(L, "test_udata_2");
	lua_setmetatable(L, -2);
	luaT_checkudata(L, -1, "test_udata_1");
	return 0;
}

static int
checkudata_string_cb(lua_State *L)
{
	lua_pushstring(L, "foo");
	luaT_checkudata(L, -1, "test_udata_1");
	return 0;
}

static void
test_checkudata(lua_State *L)
{
	plan(7);
	header();

	static const struct luaL_Reg meta[] = {{ NULL, NULL }};
	const char *name_1 = "test_udata_1";
	const char *name_2 = "test_udata_2";
	luaL_register_type(L, name_1, meta);
	luaL_register_type(L, name_2, meta);

	void *p = lua_newuserdata(L, 1);
	luaL_getmetatable(L, name_1);
	lua_setmetatable(L, -2);

	void *r = luaT_checkudata(L, -1, name_1);
	ok(r == p, "expected %p, got %p", p, r);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checkudata_udata_cb, NULL) == LUA_ERRRUN,
	   "error status");
	struct error *err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected test_udata_1 as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checkudata_string_cb, NULL) == LUA_ERRRUN,
	   "error status");
	err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected test_udata_1 as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	footer();
	check_plan();
}

static int
checktype_cb(lua_State *L)
{
	lua_pushstring(L, "foo");
	luaT_checktype(L, -1, LUA_TNUMBER);
	return 0;
}

static void
test_checktype(lua_State *L)
{
	plan(3);
	header();

	lua_pushstring(L, "foo");
	luaT_checktype(L, -1, LUA_TSTRING);
	lua_pop(L, 1);

	ok(lua_cpcall(L, checktype_cb, NULL) == LUA_ERRRUN, "error status");
	struct error *err = luaL_iserror(L, -1);
	ok(err != NULL, "not NULL");
	ok(strcmp(err->errmsg, "expected number as -1 argument") == 0,
	   "got '%s'", err->errmsg);
	lua_pop(L, 1);

	footer();
	check_plan();
}

static void
test_optint(lua_State *L)
{
	plan(5);
	header();

	int i;

	lua_pushnumber(L, 11);
	i = luaT_optint(L, -1, 17);
	ok(i == 11, "got %d", i);
	lua_pop(L, 1);

	lua_pushnumber(L, 36.6);
	i = luaT_optint(L, -1, 17);
	ok(i == 36, "got %d", i);
	lua_pop(L, 1);

	lua_pushstring(L, "36.6");
	i = luaT_optint(L, -1, 17);
	ok(i == 36, "got %d", i);
	lua_pop(L, 1);

	lua_pushnil(L);
	i = luaT_optint(L, -1, 17);
	ok(i == 17, "got %d", i);
	lua_pop(L, 1);

	i = luaT_optint(L, 1, 17);
	ok(i == 17, "got %d", i);

	footer();
	check_plan();
}

int
main(void)
{
	plan(12);
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
	test_checkstring(L);
	test_checklstring(L);
	test_checkint(L);
	test_checknumber(L);
	test_checkudata(L);
	test_checktype(L);
	test_optint(L);

	fiber_free();
	memory_free();
	lua_close(L);

	footer();
	return check_plan();
}
