/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/func_adapter.h"
#include "box/lua/tuple.h"
#include "box/tuple.h"
#include "core/func_adapter.h"
#include "lua/utils.h"

/**
 * Context for func_adapter_lua.
 */
struct func_adapter_lua_ctx {
	/**
	 * Lua state which stores arguments and is used to call Lua function.
	 */
	struct lua_State *L;
	/**
	 * A reference to Lua state in Lua registry.
	 */
	int coro_ref;
	/**
	 * Saved top of L.
	 */
	int top_svp;
	/**
	 * Index of an element that will be popped next. It is used not to
	 * remove elements from the middle of the Lua stack.
	 */
	int idx;
};

static_assert(sizeof(struct func_adapter_lua_ctx) <= sizeof(struct func_adapter_ctx),
	      "sizeof(func_adapter_lua_ctx) must be <= "
	      "sizeof(func_adapter_ctx)");

/**
 * Specialization of func_adapter for Lua functions and other callable objects.
 */
struct func_adapter_lua {
	/**
	 * Virtual table.
	 */
	const struct func_adapter_vtab *vtab;
	/**
	 * Reference to the function in Lua registry.
	 */
	int func_ref;
};

/**
 * Creates (or gets cached in fiber) Lua state and saves its top.
 */
static void
func_adapter_lua_begin(struct func_adapter *base,
		       struct func_adapter_ctx *base_ctx)
{
	struct func_adapter_lua *func = (struct func_adapter_lua *)base;
	struct func_adapter_lua_ctx *ctx =
		(struct func_adapter_lua_ctx *)base_ctx;
	if (fiber()->storage.lua.stack == NULL) {
		ctx->L = luaT_newthread(tarantool_L);
		if (ctx->L == NULL)
			panic("Cannot create Lua thread");
		ctx->coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	} else {
		ctx->L = fiber()->storage.lua.stack;
		ctx->coro_ref = LUA_REFNIL;
	}
	ctx->idx = 0;
	ctx->top_svp = lua_gettop(ctx->L);
	lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, func->func_ref);
}

/**
 * Releases Lua state.
 */
static void
func_adapter_lua_end(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	lua_settop(ctx->L, ctx->top_svp);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, ctx->coro_ref);
	ctx->coro_ref = LUA_REFNIL;
	ctx->L = NULL;
}

/**
 * Call the function with arguments that were passed before.
 */
static int
func_adapter_lua_call(struct func_adapter_ctx *base_ctx)
{
	struct func_adapter_lua_ctx *ctx =
		(struct func_adapter_lua_ctx *)base_ctx;
	int nargs = lua_gettop(ctx->L) - ctx->top_svp - 1;
	if (luaT_call(ctx->L, nargs, LUA_MULTRET) != 0)
		return -1;
	ctx->idx = ctx->top_svp + 1;
	return 0;
}

static void
func_adapter_lua_push_double(struct func_adapter_ctx *base, double val)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	lua_pushnumber(ctx->L, val);
}

static void
func_adapter_lua_push_str(struct func_adapter_ctx *base,
			  const char *str, size_t len)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	lua_pushlstring(ctx->L, str, len);
}

static void
func_adapter_lua_push_tuple(struct func_adapter_ctx *base, struct tuple *tuple)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	luaT_pushtuple(ctx->L, tuple);
}

static void
func_adapter_lua_push_null(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	lua_pushnil(ctx->L);
}

/**
 * Checks if the next value is a Lua number. Cdata numeric types and decimal are
 * not supported.
 */
static bool
func_adapter_lua_is_double(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	return lua_gettop(ctx->L) >= ctx->idx &&
	       lua_type(ctx->L, ctx->idx) == LUA_TNUMBER;
}

static void
func_adapter_lua_pop_double(struct func_adapter_ctx *base, double *out)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	*out = lua_tonumber(ctx->L, ctx->idx++);
}

static bool
func_adapter_lua_is_str(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	return lua_gettop(ctx->L) >= ctx->idx &&
	       lua_type(ctx->L, ctx->idx) == LUA_TSTRING;
}

static void
func_adapter_lua_pop_str(struct func_adapter_ctx *base, const char **str,
			 size_t *len)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	*str = lua_tolstring(ctx->L, ctx->idx++, len);
}

/**
 * Check if the next value is a cdata tuple.
 */
static bool
func_adapter_lua_is_tuple(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	int idx = ctx->idx;
	return lua_gettop(ctx->L) >= idx && luaT_istuple(ctx->L, idx) != NULL;
}

/**
 * Pops cdata tuple. Does not cast Lua tables to tuples.
 */
static void
func_adapter_lua_pop_tuple(struct func_adapter_ctx *base, struct tuple **out)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	*out = luaT_istuple(ctx->L, ctx->idx++);
	assert(*out != NULL);
	tuple_ref(*out);
}

/**
 * Null in Lua can be represented in three ways: nil, box.NULL or just absence
 * of an object. The function checks all the cases.
 */
static bool
func_adapter_lua_is_null(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	return lua_gettop(ctx->L) < ctx->idx || lua_isnil(ctx->L, ctx->idx) ||
	       luaL_isnull(ctx->L, ctx->idx);
}

/**
 * Pops null value. Since it can be used when all the values were popped, index
 * is advanced only when we are in the scope of the Lua stack.
 */
static void
func_adapter_lua_pop_null(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	if (lua_gettop(ctx->L) >= ctx->idx)
		ctx->idx++;
}

/**
 * Virtual destructor.
 */
static void
func_adapter_lua_destroy(struct func_adapter *func_base)
{
	struct func_adapter_lua *func = (struct func_adapter_lua *)func_base;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, func->func_ref);
	free(func);
}

void
func_adapter_lua_get_func(struct func_adapter *func, lua_State *L)
{
	assert(func != NULL);
	assert(func->vtab->destroy == func_adapter_lua_destroy);
	struct func_adapter_lua *lua_func =
		(struct func_adapter_lua *)func;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func->func_ref);
}

struct func_adapter *
func_adapter_lua_create(lua_State *L, int idx)
{
	static const struct func_adapter_vtab vtab = {
		.begin = func_adapter_lua_begin,
		.end = func_adapter_lua_end,
		.call = func_adapter_lua_call,

		.push_double = func_adapter_lua_push_double,
		.push_str = func_adapter_lua_push_str,
		.push_tuple = func_adapter_lua_push_tuple,
		.push_null = func_adapter_lua_push_null,

		.is_double = func_adapter_lua_is_double,
		.pop_double = func_adapter_lua_pop_double,
		.is_str = func_adapter_lua_is_str,
		.pop_str = func_adapter_lua_pop_str,
		.is_tuple = func_adapter_lua_is_tuple,
		.pop_tuple = func_adapter_lua_pop_tuple,
		.is_null = func_adapter_lua_is_null,
		.pop_null = func_adapter_lua_pop_null,

		.destroy = func_adapter_lua_destroy,
	};
	struct func_adapter_lua *func = xmalloc(sizeof(*func));
	assert(luaL_iscallable(L, idx));
	lua_pushvalue(L, idx);
	func->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	func->vtab = &vtab;
	return (struct func_adapter *)func;
}
