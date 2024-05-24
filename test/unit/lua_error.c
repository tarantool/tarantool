#include <stdio.h>

#include "diag.h"
#include "fiber.h"
#include "lualib.h"
#include "memory.h"

#include "lua/error.h"
#include "lua/utils.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static int
raise_error(lua_State *L)
{
	diag_set(IllegalParams, "foo");
	return luaT_error(L);
}

static int
raise_error_at(lua_State *L)
{
	int level = lua_tointeger(L, 1);
	diag_set(IllegalParams, "bar");
	return luaT_error_at(L, level);
}

static int
error_trace(lua_State *L)
{
	struct error *e = luaT_checkerror(L, 1);

	lua_createtable(L, 0, 2);
	lua_pushstring(L, "file");
	lua_pushstring(L, e->file);
	lua_settable(L, -3);
	lua_pushstring(L, "line");
	lua_pushinteger(L, e->line);
	lua_settable(L, -3);
	return 1;
}

const char *test_error_lua =
"local this_file = debug.getinfo(1, 'S').short_src\n"
"local line1 = debug.getinfo(1, 'l').currentline + 1\n"
"local f1 = function(fn, ...) fn(...) end\n"
"local line2 = debug.getinfo(1, 'l').currentline + 1\n"
"local f2 = function(fn, ...) f1(fn, ...) end\n"
"local line3 = debug.getinfo(1, 'l').currentline + 1\n"
"local f3 = function(fn, ...) f2(fn, ...) end\n"
"\n"
"local function check(line, fn, ...)\n"
"    local ok, err = pcall(f3, fn, ...)\n"
"\n"
"    assert(not ok, string.format('got %s', err))\n"
"    local trace = test_error_trace(err)\n"
"    assert(trace.file == this_file, string.format('got \"%s\"', trace.file))\n"
"    assert(trace.line == line,\n"
"           string.format('expected %d, got %d', line, trace.line))\n"
"end\n"
"\n"
"assert(test_raise_error ~= nil)"
"check(line1, test_raise_error)\n"
"check(line1, test_raise_error_at, 1)\n"
"check(line2, test_raise_error_at, 2)\n"
"check(line3, test_raise_error_at, 3)\n"
"\n"
"local ok, err = pcall(f3, test_raise_error_at, 0)\n"
"assert(not ok, string.format('got %s', err))\n"
"local trace = test_error_trace(err)\n"
"assert(string.find(trace.file, 'lua_error%.c') ~= nil,\n"
"       string.format('got %s', trace.file))\n";

static void
test_error(lua_State *L)
{
	plan(1);
	header();

	int rc;

	lua_pushcfunction(L, raise_error);
	lua_setglobal(L, "test_raise_error");
	lua_pushcfunction(L, raise_error_at);
	lua_setglobal(L, "test_raise_error_at");
	lua_pushcfunction(L, error_trace);
	lua_setglobal(L, "test_error_trace");

	rc = luaT_dostring(L, test_error_lua);
	ok(rc == 0, rc == 0 ? "OK" : "got %s",
	   diag_last_error(diag_get())->errmsg);

	footer();
	check_plan();
}

int
main(void)
{
	plan(1);
	header();

	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	memory_init();
	tarantool_lua_error_init(L);

	test_error(L);

	memory_free();
	lua_close(L);

	footer();
	return check_plan();
}
