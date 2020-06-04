#include <lua.h>
#include <lauxlib.h>
#include "lib/core/diag.h"
#include "fiber.h"
#include "lua/utils.h"
#include "box/merger.h"

/**
 * Verify whether a temporary fiber-local Lua state has the same
 * amount of stack slots before and after merge_source_next()
 * call.
 *
 * A merge source is designed to be used from plain C code without
 * passing any Lua state explicitly. There are merge sources
 * ('table', 'buffer', 'tuple') that require temporary Lua stack
 * to fetch next tuple and they use fiber-local Lua stack when it
 * is available.
 *
 * Such calls should not left garbage on the fiber-local Lua
 * stack, because many of them in row may overflow the stack.
 *
 * The module built as a separate dynamic library, but it uses
 * internal tarantool functions. So it is not a 'real' external
 * module, but the stub that imitates usage of a merge source from
 * tarantool code.
 */

/**
 * Extract a merge source from the Lua stack.
 */
static struct merge_source *
luaT_check_merge_source(struct lua_State *L, int idx)
{
	uint32_t cdata_type;
	struct merge_source **source_ptr = luaL_checkcdata(L, idx, &cdata_type);
	assert(source_ptr != NULL);
	return *source_ptr;
}

/**
 * Call merge_source_next() virtual method of a merge source.
 *
 * The purpose of this function is to verify whether the
 * fiber-local Lua stack is properly cleaned after
 * merge_source_next() call on the passed merge source.
 *
 * The function is to be called from Lua. Lua API is the
 * following:
 *
 * Parameters:
 *
 * - merge_source   A merge source object to call
 *                  merge_source_next() on it.
 *
 * Return values:
 *
 * - is_next_ok     Whether the call is successful.
 * - err_msg        Error message from the call or nil.
 * - is_stack_even  Whether the fiber-local Lua stack is
 *                  even after the call.
 */
static int
lbox_check_merge_source_call_next(struct lua_State *L)
{
	assert(lua_gettop(L) == 1);

	/*
	 * Ensure that there is a reusable temporary Lua stack.
	 *
	 * Note: It is the same as <L> for a Lua born fiber (at
	 * least at the moment of writing), but it is the
	 * implementation detail and the test looks more clean
	 * when we don't lean on this fact.
	 */
	struct lua_State *temporary_L = fiber_self()->storage.lua.stack;
	assert(temporary_L != NULL);

	struct tuple *tuple;
	struct merge_source *source = luaT_check_merge_source(L, 1);

	int top = lua_gettop(temporary_L);
	int rc = merge_source_next(source, NULL, &tuple);
	(void) tuple;
	bool is_stack_even = lua_gettop(temporary_L) == top;
	struct error *e = diag_last_error(diag_get());

	lua_pushboolean(L, rc == 0);
	if (rc == 0)
		lua_pushnil(L);
	else
		lua_pushstring(L, e->errmsg);
	lua_pushboolean(L, is_stack_even);
	return 3;
}

/**
 * Register the module.
 */
LUA_API int
luaopen_check_merge_source(struct lua_State *L)
{
	static const struct luaL_Reg meta[] = {
		{"call_next", lbox_check_merge_source_call_next},
		{NULL, NULL}
	};
	luaL_register(L, "merge_source", meta);
	return 1;
}
