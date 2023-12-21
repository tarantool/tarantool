/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/func_adapter.h"
#include "box/lua/tuple.h"
#include "box/tuple.h"
#include "core/func_adapter.h"
#include "lua/msgpack.h"
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
func_adapter_lua_push_bool(struct func_adapter_ctx *base, bool val)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	lua_pushboolean(ctx->L, val);
}

static void
func_adapter_lua_push_null(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	lua_pushnil(ctx->L);
}

static void
func_adapter_lua_push_msgpack(struct func_adapter_ctx *base, const char *data,
			      const char *data_end, struct mp_ctx *mp_ctx)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	luamp_push_with_ctx(ctx->L, data, data_end, mp_ctx);
}

/**
 * This function must be pushed as a closure with 4 upvalues.
 * Advances the iterator - invokes iterator_next() with a saved state.
 * For details, see description of func_adapter_lua_push_iterator().
 */
static int
func_adapter_lua_iterator_next(lua_State *L)
{
	func_adapter_iterator_next_f iterator_next =
		lua_touserdata(L, lua_upvalueindex(1));
	void *state = lua_touserdata(L, lua_upvalueindex(2));
	struct func_adapter *func_base =
		lua_touserdata(L, lua_upvalueindex(3));
	struct func_adapter_ctx *ctx_base =
		lua_touserdata(L, lua_upvalueindex(4));
	struct func_adapter_lua_ctx *ctx =
		(struct func_adapter_lua_ctx *)ctx_base;
	int top_svp = lua_gettop(ctx->L);
	int rc = iterator_next(func_base, ctx_base, state);
	if (rc != 0)
		luaT_error(L);
	int top = lua_gettop(ctx->L);
	assert(top >= top_svp);
	return top - top_svp;
}

/**
 * This function must be pushed as a closure with 4 upvalues.
 * Creates an iterator - pushes another closure with the same upvalues.
 * For details, see description of func_adapter_lua_push_iterator().
 */
static int
func_adapter_lua_iterator_start(lua_State *L)
{
	for (int i = 1; i <= 4; ++i)
		lua_pushvalue(L, lua_upvalueindex(i));
	lua_pushcclosure(L, func_adapter_lua_iterator_next, 4);
	return 1;
}

/**
 * Iterators in Lua are implemented as usual functions (or closures), which
 * return the next element. So this function pushes a closure, that returns an
 * actual iterator - another closure, which is a wrapper over iterator_next.
 * That's how it looks from Lua:
 * function(iter)
 *     for v1, v2 in iter() do
 *         process(v1, v2)
 *     end
 * end
 */
static void
func_adapter_lua_push_iterator(struct func_adapter *func,
			       struct func_adapter_ctx *base_ctx, void *state,
			       func_adapter_iterator_next_f iterator_next)
{
	struct func_adapter_lua_ctx *ctx =
		(struct func_adapter_lua_ctx *)base_ctx;
	lua_pushlightuserdata(ctx->L, iterator_next);
	lua_pushlightuserdata(ctx->L, state);
	lua_pushlightuserdata(ctx->L, func);
	lua_pushlightuserdata(ctx->L, ctx);
	lua_pushcclosure(ctx->L, func_adapter_lua_iterator_start, 4);
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

static bool
func_adapter_lua_is_bool(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	return lua_gettop(ctx->L) >= ctx->idx &&
	       lua_isboolean(ctx->L, ctx->idx);
}

static void
func_adapter_lua_pop_bool(struct func_adapter_ctx *base, bool *val)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	*val = lua_toboolean(ctx->L, ctx->idx++);
}

/**
 * Null in Lua can be represented in two ways: nil or box.NULL.
 * The function checks both cases.
 */
static bool
func_adapter_lua_is_null(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	return lua_gettop(ctx->L) >= ctx->idx &&
	       (lua_isnil(ctx->L, ctx->idx) || luaL_isnull(ctx->L, ctx->idx));
}

static void
func_adapter_lua_pop_null(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	ctx->idx++;
}

static bool
func_adapter_lua_is_empty(struct func_adapter_ctx *base)
{
	struct func_adapter_lua_ctx *ctx = (struct func_adapter_lua_ctx *)base;
	return lua_gettop(ctx->L) < ctx->idx;
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
		.push_bool = func_adapter_lua_push_bool,
		.push_null = func_adapter_lua_push_null,
		.push_msgpack = func_adapter_lua_push_msgpack,
		.push_iterator = func_adapter_lua_push_iterator,

		.is_double = func_adapter_lua_is_double,
		.pop_double = func_adapter_lua_pop_double,
		.is_str = func_adapter_lua_is_str,
		.pop_str = func_adapter_lua_pop_str,
		.is_tuple = func_adapter_lua_is_tuple,
		.pop_tuple = func_adapter_lua_pop_tuple,
		.is_bool = func_adapter_lua_is_bool,
		.pop_bool = func_adapter_lua_pop_bool,
		.is_null = func_adapter_lua_is_null,
		.pop_null = func_adapter_lua_pop_null,
		.is_empty = func_adapter_lua_is_empty,

		.destroy = func_adapter_lua_destroy,
	};
	struct func_adapter_lua *func = xmalloc(sizeof(*func));
	assert(luaL_iscallable(L, idx));
	lua_pushvalue(L, idx);
	func->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	func->vtab = &vtab;
	return (struct func_adapter *)func;
}
