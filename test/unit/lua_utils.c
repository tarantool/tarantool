#include <stdio.h>

#include "diag.h"
#include "fiber.h"
#include "lualib.h"
#include "memory.h"
#include "reflection.h"

#include "lua/utils.h"
#include "lua_test_utils.h"

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
	plan(6);
	header();

	/* test NON Tarantool error on stack */
	lua_pushstring(L, "test Lua error");
	luaT_toerror(L);
	/*
	 * luaT_toerror adds param on stack thru luaT_tolstring.
	 * Unfortunately the latter is public API.
	 */
	lua_pop(L, 2);
	lua_gc(L, LUA_GCCOLLECT, 0);
	check_error("LuajitError", "test Lua error");

	/* Check that messages exceeding the inline buffer survive Lua GC. */
	char long_msg[DIAG_ERRMSG_MAX + 1];
	memset(long_msg, 'a', sizeof(long_msg) - 1);
	long_msg[sizeof(long_msg) - 1] = '\0';
	lua_pushstring(L, long_msg);
	luaT_toerror(L);
	lua_pop(L, 2);
	lua_gc(L, LUA_GCCOLLECT, 0);
	check_error("LuajitError", long_msg);

	/* test Tarantool error on stack */
	struct error *e = BuildIllegalParams(__FILE__, __LINE__,
					     "test non-Lua error");
	luaT_pusherror(L, e);
	luaT_toerror(L);
	lua_pop(L, 1);
	lua_gc(L, LUA_GCCOLLECT, 0);
	check_error("IllegalParams", "test non-Lua error");

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

/** Allocator state for failing Lua allocations. */
struct fail_alloc_ctx {
	/** Original allocation function. */
	lua_Alloc alloc;
	/** Original allocator's context. */
	void *ud;
};

static void *
fail_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	struct fail_alloc_ctx *ctx = ud;
	if (nsize > osize)
		return NULL;
	return ctx->alloc(ctx->ud, ptr, osize, nsize);
}

static void
test_newthread(lua_State *L)
{
	plan(7);
	header();

	fail_unless(lua_checkstack(L, LUA_MINSTACK));
	lua_pushliteral(L, "existing value");
	int top = lua_gettop(L);
	struct fail_alloc_ctx ctx;
	ctx.alloc = lua_getallocf(L, &ctx.ud);
	lua_setallocf(L, fail_alloc, &ctx);
	struct lua_State *thread = luaT_newthread(L);
	lua_setallocf(L, ctx.alloc, ctx.ud);
	is(thread, NULL, "allocation failure is reported");
	is(lua_gettop(L), top, "failed creation preserves stack height");
	check_error("LuajitError", "not enough memory");

	thread = luaT_newthread(L);
	ok(thread != NULL, "creation succeeds after allocation is restored");
	is(lua_gettop(L), top + 1, "successful creation pushes one coroutine");
	is(lua_tothread(L, -1), thread,
	   "the created coroutine is on the stack");
	lua_settop(L, 0);

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

int
main(void)
{
	plan(4);
	header();

	struct lua_State *L = luaT_newteststate();
	memory_init();
	fiber_init(fiber_c_invoke);
	tarantool_lua_error_init(L);
	tarantool_lua_utils_init(L);

	test_toerror(L);
	test_call(L);
	test_newthread(L);
	test_dostring(L);

	fiber_free();
	memory_free();
	lua_close(L);

	footer();
	return check_plan();
}
