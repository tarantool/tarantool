#include <lua.h>
#include "lualib.h"
#include <lauxlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "core/tweaks.h"
#include "diag.h"
#include "fiber.h"
#include "lua/error.h"
#include "lua/msgpack.h"
#include "lua/tweaks.h"
#include "lua/utils.h"
#include "memory.h"
#include "tt_static.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "lua_test_utils.h"

static struct lua_State *L;

static bool bool_var = true;
TWEAK_BOOL(bool_var);

static int64_t int_var = 42;
TWEAK_INT(int_var);

static uint64_t uint_var = 123;
TWEAK_UINT(uint_var);

static double double_var = 3.14;
TWEAK_DOUBLE(double_var);

enum my_enum {
	MY_FOO,
	MY_BAR,
	my_enum_MAX,
};

static const char *const my_enum_strs[] = {
	"FOO",
	"BAR",
};

static enum my_enum enum_var = MY_BAR;
TWEAK_ENUM(my_enum, enum_var);

static void
test_index(void)
{
	plan(12);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, "return tweaks.no_such_var"), 0, "no_such_var");
	ok(lua_isnoneornil(L, 1), "no_such_var not found");
	lua_settop(L, 0);
	is(luaT_dostring(L, "return tweaks.bool_var"), 0, "bool_var");
	ok(!lua_isnoneornil(L, 1), "bool_var found");
	lua_settop(L, 0);
	is(luaT_dostring(L, "return tweaks.int_var"), 0, "int_var");
	ok(!lua_isnoneornil(L, 1), "int_var found");
	lua_settop(L, 0);
	is(luaT_dostring(L, "return tweaks.uint_var"), 0, "uint_var");
	ok(!lua_isnoneornil(L, 1), "uint_var found");
	lua_settop(L, 0);
	is(luaT_dostring(L, "return tweaks.double_var"), 0, "double_var");
	ok(!lua_isnoneornil(L, 1), "double_var found");
	lua_settop(L, 0);
	is(luaT_dostring(L, "return tweaks.enum_var"), 0, "enum_var");
	ok(!lua_isnoneornil(L, 1), "enum_var found");
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_newindex(void)
{
	plan(4);
	header();
	is(luaT_dostring(L, "tweaks.no_such_var = 1"), -1, "unknown option");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg, "No such option") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.bool_var = {}"), -1,
	   "invalid value - table");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Value must be boolean, number, or string") == 0,
	   "check error");
	footer();
	check_plan();
}

static void
test_bool_var(void)
{
	plan(10);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.bool_var = 'true'"), -1,
	   "set invalid value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected boolean") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.bool_var = false"), 0, "set value");
	is(bool_var, false, "check C value");
	is(luaT_dostring(L, "return tweaks.bool_var"), 0, "get value");
	is(lua_toboolean(L, 1), false, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.bool_var = true"), 0, "set value");
	is(bool_var, true, "check C value");
	is(luaT_dostring(L, "return tweaks.bool_var"), 0, "get value");
	is(lua_toboolean(L, 1), true, "check Lua value");
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_int_var(void)
{
	plan(22);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.int_var = true"), -1,
	   "set invalid value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected integer") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.int_var = 1.5"), -1,
	   "set double value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected integer") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.int_var = 9223372036854775808ULL"), -1,
	   "set too big value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, must be <= 9223372036854775807") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.int_var = -9223372036854775808LL"), 0,
	   "set min value");
	is(int_var, INT64_MIN, "check C value");
	is(luaT_dostring(L, "return tweaks.int_var"), 0, "get value");
	is(luaL_toint64(L, 1), INT64_MIN, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.int_var = 9223372036854775807LL"), 0,
	   "set max value");
	is(int_var, INT64_MAX, "check C value");
	is(luaT_dostring(L, "return tweaks.int_var"), 0, "get value");
	is(luaL_toint64(L, 1), INT64_MAX, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.int_var = 11"), 0, "set value");
	is(int_var, 11, "check C value");
	is(luaT_dostring(L, "return tweaks.int_var"), 0, "get value");
	is(lua_tointeger(L, 1), 11, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.int_var = 42"), 0, "set value");
	is(int_var, 42, "check C value");
	is(luaT_dostring(L, "return tweaks.int_var"), 0, "get value");
	is(lua_tointeger(L, 1), 42, "check Lua value");
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_uint_var(void)
{
	plan(18);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.uint_var = true"), -1,
	   "set invalid value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected integer") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.uint_var = 1.5"), -1,
	   "set double value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected integer") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.uint_var = -1"), -1,
	   "set negative value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, must be >= 0") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.uint_var = 18446744073709551615ULL"), 0,
	   "set max value");
	is(uint_var, UINT64_MAX, "check C value");
	is(luaT_dostring(L, "return tweaks.uint_var"), 0, "get value");
	is(luaL_touint64(L, 1), UINT64_MAX, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.uint_var = 11"), 0, "set value");
	is(uint_var, 11, "check C value");
	is(luaT_dostring(L, "return tweaks.uint_var"), 0, "get value");
	is(lua_tointeger(L, 1), 11, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.uint_var = 123"), 0, "set value");
	is(uint_var, 123, "check C value");
	is(luaT_dostring(L, "return tweaks.uint_var"), 0, "get value");
	is(lua_tointeger(L, 1), 123, "check Lua value");
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_double_var(void)
{
	plan(18);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.double_var = true"), -1,
	   "set invalid value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected number") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.double_var = 11"), 0, "set int value");
	is(double_var, 11, "check C value");
	is(luaT_dostring(L, "return tweaks.double_var"), 0, "get value");
	is(lua_tonumber(L, 1), 11, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.double_var = -9223372036854775808LL"), 0,
	   "set min int value");
	is(double_var, (double)INT64_MIN, "check C value");
	is(luaT_dostring(L, "return tweaks.double_var"), 0, "get value");
	is(lua_tonumber(L, 1), (double)INT64_MIN, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.double_var = 18446744073709551615ULL"), 0,
	   "set max int value");
	is(double_var, (double)UINT64_MAX, "check C value");
	is(luaT_dostring(L, "return tweaks.double_var"), 0, "get value");
	is(lua_tonumber(L, 1), (double)UINT64_MAX, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.double_var = 3.14"), 0, "set double value");
	is(double_var, 3.14, "check C value");
	is(luaT_dostring(L, "return tweaks.double_var"), 0, "get value");
	is(lua_tonumber(L, 1), 3.14, "check Lua value");
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_enum_var(void)
{
	plan(12);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.enum_var = 42"), -1,
	   "set invalid value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected one of: 'FOO', 'BAR'") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.enum_var = 'enum'"), -1,
	   "set invalid value");
	ok(!diag_is_empty(diag_get()) &&
	   strcmp(diag_last_error(diag_get())->errmsg,
		  "Invalid value, expected one of: 'FOO', 'BAR'") == 0,
	   "check error");
	is(luaT_dostring(L, "tweaks.enum_var = 'FOO'"), 0, "set value");
	is(enum_var, MY_FOO, "check C value");
	is(luaT_dostring(L, "return tweaks.enum_var"), 0, "get value");
	is(strcmp(lua_tostring(L, 1), "FOO"), 0, "check Lua value");
	lua_settop(L, 0);
	is(luaT_dostring(L, "tweaks.enum_var = 'BAR'"), 0, "set value");
	is(enum_var, MY_BAR, "check C value");
	is(luaT_dostring(L, "return tweaks.enum_var"), 0, "get value");
	is(strcmp(lua_tostring(L, 1), "BAR"), 0, "check Lua value");
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_tweak_table(const char *method)
{
	plan(6);
	header();
	lua_settop(L, 0);
	is(luaT_dostring(L, tt_sprintf("return getmetatable(tweaks).%s()",
				       method)),
	   0, "call");
	lua_getfield(L, 1, "bool_var");
	is(lua_toboolean(L, 2), true, "bool_var");
	lua_pop(L, 1);
	lua_getfield(L, 1, "int_var");
	is(lua_tointeger(L, 2), 42, "int_var");
	lua_pop(L, 1);
	lua_getfield(L, 1, "uint_var");
	is(lua_tointeger(L, 2), 123, "uint_var");
	lua_pop(L, 1);
	lua_getfield(L, 1, "double_var");
	is(lua_tonumber(L, 2), 3.14, "double_var");
	lua_pop(L, 1);
	lua_getfield(L, 1, "enum_var");
	is(strcmp(lua_tostring(L, 2), "BAR"), 0, "enum_var");
	lua_pop(L, 1);
	lua_settop(L, 0);
	footer();
	check_plan();
}

static void
test_serialize(void)
{
	plan(1);
	header();
	test_tweak_table("__serialize");
	footer();
	check_plan();
}

static void
test_autocomplete(void)
{
	plan(1);
	header();
	test_tweak_table("__autocomplete");
	footer();
	check_plan();
}

static int
test_lua_tweaks(void)
{
	plan(9);
	header();
	test_index();
	test_newindex();
	test_bool_var();
	test_int_var();
	test_uint_var();
	test_double_var();
	test_enum_var();
	test_serialize();
	test_autocomplete();
	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);

	L = luaT_newteststate();
	luaopen_msgpack(L);
	lua_pop(L, 1);
	tarantool_lua_tweaks_init(L);

	/*
	 * luaT_newmodule() assumes that tarantool has a special
	 * loader for built-in modules. That's true, when all the
	 * initialization code is executed. However, in the unit
	 * test we don't do that.
	 *
	 * In particular, tarantool_lua_init() function is not
	 * called in a unit test.
	 *
	 * Assign the module into package.loaded directly instead.
	 *
	 *  | local mod = loaders.builtin['internal.tweaks']
	 *  | package.loaded['internal.tweaks'] = mod
	 */
	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	lua_getfield(L, LUA_REGISTRYINDEX, "_TARANTOOL_BUILTIN");
	lua_getfield(L, -1, "internal.tweaks");
	lua_setfield(L, -3, "internal.tweaks");
	lua_pop(L, 2);

	tarantool_lua_error_init(L);
	fail_unless(luaT_dostring(
			L, "tweaks = require('internal.tweaks')") == 0);
	int rc = test_lua_tweaks();
	lua_close(L);
	fiber_free();
	memory_free();
	return rc;
}
