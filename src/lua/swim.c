/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "swim/swim.h"
#include "trigger.h"
#include "diag.h"
#include "lua/utils.h"

static uint32_t ctid_swim_member_ptr;
static uint32_t ctid_swim_ptr;

/** Push member event context into a Lua stack. */
static int
lua_swim_member_event_push(struct lua_State *L, void *event)
{
	struct swim_on_member_event_ctx *ctx =
		(struct swim_on_member_event_ctx *) event;
	*(struct swim_member **) luaL_pushcdata(L, ctid_swim_member_ptr) =
		ctx->member;
	lua_pushinteger(L, ctx->events);
	return 2;
}

/**
 * Normalize arguments for swim_object:on_member_event.
 * Is implemented in C because is_callable function is not exported to Lua.
 *
 * Input format:
 * 1. new_trigger,
 * 2. old_trigger or ctx - is considered as old_trigger if object is callable.
 * 3. trigger_name or ctx - is considered as trigger_name if object is string.
 * 4. ctx or nil
 * Also, ctx can be passed with key-value trigger API using key "ctx".
 *
 * Output format: new_trigger, old_trigger, trigger_name, ctx.
 */
static int
lua_swim_on_member_event_normalize_arguments(struct lua_State *L)
{
	if (!luaL_iscallable(L, 1) && lua_istable(L, 1)) {
		/* Key-value API is used. */
		lua_getfield(L, 1, "func");
		lua_pushnil(L);
		lua_getfield(L, 1, "name");
		lua_getfield(L, 1, "ctx");
		return 4;
	}
	/* Fill the stack with nils. */
	lua_settop(L, 4);
	const int default_ctx_idx = 4;
	int ctx_idx = default_ctx_idx;
	/* New trigger. */
	lua_pushvalue(L, 1);
	/* Old trigger or ctx. */
	if (luaL_iscallable(L, 2) || lua_isnil(L, 2) || luaL_isnull(L, 2)) {
		lua_pushvalue(L, 2);
	} else {
		lua_pushnil(L);
		ctx_idx = 2;
	}
	/* Name or ctx, if it wasn't met earlier. */
	if (lua_type(L, 3) == LUA_TSTRING || lua_isnil(L, 3) ||
	    luaL_isnull(L, 3) || ctx_idx != default_ctx_idx) {
		lua_pushvalue(L, 3);
	} else {
		lua_pushnil(L);
		ctx_idx = 3;
	}
	/* Context. */
	lua_pushvalue(L, ctx_idx);
	return 4;
}

/** Set or/and delete a trigger on a SWIM member event. */
static int
lua_swim_on_member_event(struct lua_State *L)
{
	uint32_t ctypeid;
	struct swim *s = *(struct swim **) luaL_checkcdata(L, 1, &ctypeid);
	return lbox_trigger_reset(L, 2, swim_trigger_list_on_member_event(s),
				  lua_swim_member_event_push, NULL);
}

/**
 * Create a new SWIM instance. SWIM is not created via FFI,
 * because this operation yields.
 * @retval 1 Success. A SWIM instance pointer is on the stack.
 * @retval 2 Error. Nil and an error object are pushed.
 */
static int
lua_swim_new(struct lua_State *L)
{
	uint64_t generation = luaL_checkuint64(L, 1);
	struct swim *s = swim_new(generation);
	if (s == NULL)
		return luaT_push_nil_and_error(L);
	*(struct swim **) luaL_pushcdata(L, ctid_swim_ptr) = s;
	return 1;
}

/**
 * Delete a SWIM instance. SWIM is not deleted via FFI, because
 * this operation yields.
 */
static int
lua_swim_delete(struct lua_State *L)
{
	uint32_t ctypeid;
	struct swim *s = *(struct swim **) luaL_checkcdata(L, 1, &ctypeid);
	assert(ctypeid == ctid_swim_ptr);
	swim_delete(s);
	return 0;
}

/**
 * Gracefully leave the cluster, broadcast a notification, and delete the SWIM
 * instance. It is not FFI, because this operation yields.
 */
static int
lua_swim_quit(struct lua_State *L)
{
	uint32_t ctypeid;
	struct swim *s = *(struct swim **) luaL_checkcdata(L, 1, &ctypeid);
	assert(ctypeid == ctid_swim_ptr);
	swim_quit(s);
	return 0;
}

void
tarantool_lua_swim_init(struct lua_State *L)
{
	luaL_cdef(L, "struct swim_member; struct swim;");
	ctid_swim_member_ptr = luaL_ctypeid(L, "struct swim_member *");
	ctid_swim_ptr = luaL_ctypeid(L, "struct swim *");

	static const struct luaL_Reg lua_swim_internal_methods [] = {
		{"swim_new", lua_swim_new},
		{"swim_delete", lua_swim_delete},
		{"swim_quit", lua_swim_quit},
		{"swim_on_member_event", lua_swim_on_member_event},
		{"swim_on_member_event_normalize_arguments",
			lua_swim_on_member_event_normalize_arguments},
		{NULL, NULL}
	};
	luaT_newmodule(L, "swim.lib", lua_swim_internal_methods);
	lua_pop(L, 1);
}
