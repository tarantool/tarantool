/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/call.h"
#include "box/error.h"
#include "fiber.h"

#include "lua/utils.h"
#include "lua/msgpack.h"

#include "box/txn.h"
#include "box/request.h"
#include "box/xrow.h"
#include "box/iproto_port.h"
#include "box/lua/tuple.h"
#include "small/obuf.h"

/**
 * A helper to find a Lua function by name and put it
 * on top of the stack.
 */
static int
box_lua_find(lua_State *L, const char *name, const char *name_end)
{
	int index = LUA_GLOBALSINDEX;
	int objstack = 0;
	const char *start = name, *end;

	while ((end = (const char *) memchr(start, '.', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! lua_istable(L, -1)) {
			diag_set(ClientError, ER_NO_SUCH_PROC,
				 name_end - name, name);
			lbox_error(L);
		}
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
	}

	/* box.something:method */
	if ((end = (const char *) memchr(start, ':', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! (lua_istable(L, -1) ||
			lua_islightuserdata(L, -1) || lua_isuserdata(L, -1) )) {
				diag_set(ClientError, ER_NO_SUCH_PROC,
					  name_end - name, name);
				lbox_error(L);
		}
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
		objstack = index;
	}


	lua_pushlstring(L, start, name_end - start);
	lua_gettable(L, index);
	if (!lua_isfunction(L, -1) && !lua_istable(L, -1)) {
		/* lua_call or lua_gettable would raise a type error
		 * for us, but our own message is more verbose. */
		diag_set(ClientError, ER_NO_SUCH_PROC,
			  name_end - name, name);
		lbox_error(L);
	}
	/* setting stack that it would contain only
	 * the function pointer. */
	if (index != LUA_GLOBALSINDEX) {
		if (objstack == 0) {        /* no object, only a function */
			lua_replace(L, 1);
		} else if (objstack == 1) { /* just two values, swap them */
			lua_insert(L, -2);
		} else {		    /* long path */
			lua_insert(L, 1);
			lua_insert(L, 2);
			objstack = 1;
		}
		lua_settop(L, 1 + objstack);
	}
	return 1 + objstack;
}

/**
 * A helper to find lua stored procedures for box.call.
 * box.call iteslf is pure Lua, to avoid issues
 * with infinite call recursion smashing C
 * thread stack.
 */

static int
lbox_call_loadproc(struct lua_State *L)
{
	const char *name;
	size_t name_len;
	name = lua_tolstring(L, 1, &name_len);
	return box_lua_find(L, name, name + name_len);
}

struct lua_function_ctx {
	struct request *request;
	struct obuf *out;
	struct obuf_svp svp;
	/* true if `out' was changed and `svp' can be used for rollback  */
	bool out_is_dirty;
};

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
static inline int
execute_lua_call(lua_State *L)
{
	struct lua_function_ctx *ctx = (struct lua_function_ctx *)
		lua_topointer(L, 1);
	struct request *request = ctx->request;
	struct obuf *out = ctx->out;
	struct obuf_svp *svp = &ctx->svp;
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);

	int oc = 0; /* how many objects are on stack after box_lua_find */
	/* Try to find a function by name in Lua */
	oc = box_lua_find(L, name, name + name_len);

	/* Push the rest of args (a tuple). */
	const char *args = request->tuple;

	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "call: out of stack");

	for (uint32_t i = 0; i < arg_count; i++)
		luamp_decode(L, luaL_msgpack_default, &args);
	lua_call(L, arg_count + oc - 1, LUA_MULTRET);

	/**
	 * Add all elements from Lua stack to iproto.
	 *
	 * To allow clients to understand a complex return from
	 * a procedure, we are compatible with SELECT protocol,
	 * and return the number of return values first, and
	 * then each return value as a tuple.
	 *
	 * If a Lua stack contains at least one scalar, each
	 * value on the stack is converted to a tuple. A single
	 * Lua with scalars is converted to a tuple with multiple
	 * fields.
	 *
	 * If the stack is a Lua table, each member of which is
	 * not scalar, each member of the table is converted to
	 * a tuple. This way very large lists of return values can
	 * be used, since Lua stack size is limited by 8000 elements,
	 * while Lua table size is pretty much unlimited.
	 */
	uint32_t count = 0;
	/* TODO: forbid explicit yield from __serialize or __index here */
	if (iproto_prepare_select(out, svp) != 0)
		lbox_error(L);
	ctx->out_is_dirty = true;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      luamp_error, L);

	/** Check if we deal with a table of tables. */
	int nrets = lua_gettop(L);
	if (nrets == 1 && luaL_isarray(L, 1)) {
		/*
		 * The table is not empty and consists of tables
		 * or tuples. Treat each table element as a tuple,
		 * and push it.
		 */
		lua_pushnil(L);
		int has_keys = lua_next(L, 1);
		if (has_keys && (luaL_isarray(L, lua_gettop(L)) || lua_istuple(L, -1))) {
			do {
				luamp_encode_tuple(L, luaL_msgpack_default,
						   &stream, -1);
				++count;
				lua_pop(L, 1);
			} while (lua_next(L, 1));
			goto done;
		} else if (has_keys) {
			lua_pop(L, 1);
		}
	}
	for (int i = 1; i <= nrets; ++i) {
		luamp_convert_tuple(L, luaL_msgpack_default, &stream, i);
		++count;
	}

done:
	mpstream_flush(&stream);
	iproto_reply_select(out, svp, request->header->sync, count);
	return 0;
}

static int
execute_lua_eval(lua_State *L)
{
	struct lua_function_ctx *ctx = (struct lua_function_ctx *)
		lua_topointer(L, 1);
	struct request *request = ctx->request;
	struct obuf *out = ctx->out;
	struct obuf_svp *svp = &ctx->svp;
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	/* Compile expression */
	const char *expr = request->key;
	uint32_t expr_len = mp_decode_strl(&expr);
	if (luaL_loadbuffer(L, expr, expr_len, "=eval")) {
		diag_set(LuajitError, lua_tostring(L, -1));
		lbox_error(L);
	}

	/* Unpack arguments */
	const char *args = request->tuple;
	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "eval: out of stack");
	for (uint32_t i = 0; i < arg_count; i++) {
		luamp_decode(L, luaL_msgpack_default, &args);
	}

	/* Call compiled code */
	lua_call(L, arg_count, LUA_MULTRET);

	/* Send results of the called procedure to the client. */
	if (iproto_prepare_select(out, svp) != 0)
		diag_raise();
	ctx->out_is_dirty = true;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      luamp_error, L);
	int nrets = lua_gettop(L);
	for (int k = 1; k <= nrets; ++k) {
		luamp_encode(L, luaL_msgpack_default, &stream, k);
	}
	mpstream_flush(&stream);
	iproto_reply_select(out, svp, request->header->sync, nrets);

	return 0;
}

static inline int
box_process_lua(struct request *request, struct obuf *out, lua_CFunction handler)
{
	struct lua_function_ctx ctx = { request, out, {0, 0, 0}, false };

	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	int rc = lbox_cpcall(L, handler, &ctx);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	if (rc != 0) {
		if (ctx.out_is_dirty) {
			/*
			 * Output buffer has been altered, rollback to svp.
			 * (!) Please note that a save point for output buffer
			 * must be taken only after finishing executing of Lua
			 * function because Lua can yield and leave the
			 * buffer in inconsistent state (a parallel request
			 * from the same connection will break the protocol).
			 */
			obuf_rollback_to_svp(out, &ctx.svp);
		}

		return -1;
	}
	return 0;
}

int
box_lua_call(struct request *request, struct obuf *out)
{
	return box_process_lua(request, out, execute_lua_call);
}

int
box_lua_eval(struct request *request, struct obuf *out)
{
	return box_process_lua(request, out, execute_lua_eval);
}

static const struct luaL_reg boxlib_internal[] = {
	{"call_loadproc",  lbox_call_loadproc},
	{NULL, NULL}
};

void
box_lua_call_init(struct lua_State *L)
{
	luaL_register(L, "box.internal", boxlib_internal);
	lua_pop(L, 1);

#if 0
	/* Get CTypeID for `struct port *' */
	int rc = luaL_cdef(L, "struct port;");
	assert(rc == 0);
	(void) rc;
	CTID_STRUCT_PORT_PTR = luaL_ctypeid(L, "struct port *");
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
#endif
}
