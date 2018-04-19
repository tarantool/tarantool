/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/call.h"
#include "box/error.h"
#include "fiber.h"

#include "lua/utils.h"
#include "lua/msgpack.h"

#include "box/xrow.h"
#include "box/port.h"
#include "box/lua/tuple.h"
#include "small/obuf.h"
#include "trivia/util.h"

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
			luaT_error(L);
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
				luaT_error(L);
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
		luaT_error(L);
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

/*
 * Encode CALL/EVAL result.
 */
static inline uint32_t
luamp_encode_call(lua_State *L, struct luaL_serializer *cfg,
		  struct mpstream *stream)
{
	int count = lua_gettop(L);
	for (int i = 1; i <= count; ++i)
		luamp_encode(L, cfg, stream, i);
	return count;
}

/*
 * Encode CALL_16 result.
 *
 * To allow clients to understand a complex return from
 * a procedure, we are compatible with SELECT protocol,
 * and return the number of return values first, and
 * then each return value as a tuple.
 *
 * The following conversion rules apply:
 *
 * If a Lua stack contains at least one scalar, each
 * value on the stack is converted to a tuple. A stack
 * containing a single Lua table with scalars is converted to
 * a tuple with multiple fields.
 *
 * If the stack is a Lua table, each member of which is
 * not scalar, each member of the table is converted to
 * a tuple. This way very large lists of return values can
 * be used, since Lua stack size is limited by 8000 elements,
 * while Lua table size is pretty much unlimited.
 *
 * Please read gh-291 carefully before "fixing" this code.
 */
static inline uint32_t
luamp_encode_call_16(lua_State *L, struct luaL_serializer *cfg,
		     struct mpstream *stream)
{
	int nrets = lua_gettop(L);
	if (nrets == 0) {
		return 0;
	} else if (nrets > 1) {
		/*
		 * Multireturn:
		 * `return 1, box.tuple.new(...), array, 3, ...`
		 */
		for (int i = 1; i <= nrets; ++i) {
			struct luaL_field field;
			luaL_tofield(L, cfg, i, &field);
			struct tuple *tuple;
			if (field.type == MP_EXT &&
			    (tuple = luaT_istuple(L, i)) != NULL) {
				/* `return ..., box.tuple.new(...), ...` */
				tuple_to_mpstream(tuple, stream);
			} else if (field.type != MP_ARRAY) {
				/*
				 * `return ..., scalar, ... =>
				 *         ..., { scalar }, ...`
				 */
				lua_pushvalue(L, i);
				luamp_encode_array(cfg, stream, 1);
				luamp_encode_r(L, cfg, stream, &field, 0);
				lua_pop(L, 1);
			} else {
				/* `return ..., array, ...` */
				luamp_encode(L, cfg, stream, i);
			}
		}
		return nrets;
	}
	assert(nrets == 1);

	/*
	 * Inspect the first result
	 */
	struct luaL_field root;
	luaL_tofield(L, cfg, 1, &root);
	struct tuple *tuple;
	if (root.type == MP_EXT && (tuple = luaT_istuple(L, 1)) != NULL) {
		/* `return box.tuple()` */
		tuple_to_mpstream(tuple, stream);
		return 1;
	} else if (root.type != MP_ARRAY) {
		/*
		 * `return scalar`
		 * `return map`
		 */
		luamp_encode_array(cfg, stream, 1);
		assert(lua_gettop(L) == 1);
		luamp_encode_r(L, cfg, stream, &root, 0);
		return 1;
	}

	assert(root.type == MP_ARRAY);
	if (root.size == 0) {
		/* `return {}` => `{ box.tuple() }` */
		luamp_encode_array(cfg, stream, 0);
		return 1;
	}

	/* `return { tuple, scalar, tuple }` */
	assert(root.type == MP_ARRAY && root.size > 0);
	for (uint32_t t = 1; t <= root.size; t++) {
		lua_rawgeti(L, 1, t);
		struct luaL_field field;
		luaL_tofield(L, cfg, -1, &field);
		if (field.type == MP_EXT && (tuple = luaT_istuple(L, -1))) {
			tuple_to_mpstream(tuple, stream);
		} else if (field.type != MP_ARRAY) {
			/* The first member of root table is not tuple/array */
			if (t == 1) {
				/*
				 * `return { scalar, ... } =>
				 *        box.tuple.new(scalar, ...)`
				 */
				luamp_encode_array(cfg, stream, root.size);
				/*
				 * Encode the first field of tuple using
				 * existing information from luaL_tofield
				 */
				luamp_encode_r(L, cfg, stream, &field, 0);
				lua_pop(L, 1);
				assert(lua_gettop(L) == 1);
				/* Encode remaining fields as usual */
				for (uint32_t f = 2; f <= root.size; f++) {
					lua_rawgeti(L, 1, f);
					luamp_encode(L, cfg, stream, -1);
					lua_pop(L, 1);
				}
				return 1;
			}
			/*
			 * `return { tuple/array, ..., scalar, ... } =>
			 *         { tuple/array, ..., { scalar }, ... }`
			 */
			luamp_encode_array(cfg, stream, 1);
			luamp_encode_r(L, cfg, stream, &field, 0);
		} else {
			/* `return { tuple/array, ..., tuple/array, ... }` */
			luamp_encode_r(L, cfg, stream, &field, 0);
		}
		lua_pop(L, 1);
		assert(lua_gettop(L) == 1);
	}
	return root.size;
}

static int
execute_lua_call(lua_State *L)
{
	struct call_request *request = (struct call_request *)
		lua_topointer(L, 1);
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	const char *name = request->name;
	uint32_t name_len = mp_decode_strl(&name);

	int oc = 0; /* how many objects are on stack after box_lua_find */
	/* Try to find a function by name in Lua */
	oc = box_lua_find(L, name, name + name_len);

	/* Push the rest of args (a tuple). */
	const char *args = request->args;

	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "call: out of stack");

	for (uint32_t i = 0; i < arg_count; i++)
		luamp_decode(L, luaL_msgpack_default, &args);
	lua_call(L, arg_count + oc - 1, LUA_MULTRET);
	return lua_gettop(L);
}

static int
execute_lua_eval(lua_State *L)
{
	struct call_request *request = (struct call_request *)
		lua_topointer(L, 1);
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	/* Compile expression */
	const char *expr = request->expr;
	uint32_t expr_len = mp_decode_strl(&expr);
	if (luaL_loadbuffer(L, expr, expr_len, "=eval")) {
		diag_set(LuajitError, lua_tostring(L, -1));
		luaT_error(L);
	}

	/* Unpack arguments */
	const char *args = request->args;
	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "eval: out of stack");
	for (uint32_t i = 0; i < arg_count; i++) {
		luamp_decode(L, luaL_msgpack_default, &args);
	}

	/* Call compiled code */
	lua_call(L, arg_count, LUA_MULTRET);
	return lua_gettop(L);
}

struct encode_lua_call_ctx {
	/** Buffer to append the call result to. */
	struct obuf *out;
	/** If set, use Tarantool 1.6 output format. */
	bool call_16;
	/** Number of values in the output. */
	int count;
};

static int
encode_lua_call(lua_State *L)
{
	struct encode_lua_call_ctx *ctx = (struct encode_lua_call_ctx *)
		lua_topointer(L, -1);
	lua_pop(L, 1);

	/*
	 * Add all elements from Lua stack to the buffer.
	 *
	 * TODO: forbid explicit yield from __serialize or __index here
	 */

	struct mpstream stream;
	mpstream_init(&stream, ctx->out, obuf_reserve_cb, obuf_alloc_cb,
		      luamp_error, L);

	struct luaL_serializer *cfg = luaL_msgpack_default;
	if (ctx->call_16)
		ctx->count = luamp_encode_call_16(L, cfg, &stream);
	else
		ctx->count = luamp_encode_call(L, cfg, &stream);

	mpstream_flush(&stream);
	return 0;
}

/**
 * Port for storing the result of a Lua CALL/EVAL.
 */
struct port_lua {
	const struct port_vtab *vtab;
	/** Lua state that stores the result. */
	struct lua_State *L;
	/** Reference to L in tarantool_L. */
	int ref;
};
static_assert(sizeof(struct port_lua) <= sizeof(struct port),
	      "sizeof(struct port_lua) must be <= sizeof(struct port)");

static const struct port_vtab port_lua_vtab;

static inline int
port_lua_do_dump(struct port *base, bool call_16, struct obuf *out)
{
	struct port_lua *port = (struct port_lua *)base;
	assert(port->vtab == &port_lua_vtab);

	struct lua_State *L = port->L;
	struct encode_lua_call_ctx ctx = { out, call_16, 0 };
	lua_pushlightuserdata(L, &ctx);
	if (luaT_call(L, lua_gettop(L) - 1, 0) != 0)
		return -1;

	return ctx.count;
}

static int
port_lua_dump(struct port *base, struct obuf *out)
{
	return port_lua_do_dump(base, false, out);
}

static int
port_lua_dump_16(struct port *base, struct obuf *out)
{
	return port_lua_do_dump(base, true, out);
}

static void
port_lua_destroy(struct port *base)
{
	struct port_lua *port = (struct port_lua *)base;
	assert(port->vtab == &port_lua_vtab);

	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, port->ref);
}

static const struct port_vtab port_lua_vtab = {
	.dump_msgpack = port_lua_dump,
	.dump_msgpack_16 = port_lua_dump_16,
	.destroy = port_lua_destroy,
};

static inline int
box_process_lua(struct call_request *request, struct port *base,
		lua_CFunction handler)
{
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);

	/*
	 * Push the encoder function first - values returned by
	 * the handler will be passed to it as arguments, see
	 * port_lua_dump().
	 */
	lua_pushcfunction(L, encode_lua_call);

	lua_pushcfunction(L, handler);
	lua_pushlightuserdata(L, request);
	if (luaT_call(L, 1, LUA_MULTRET) != 0) {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		return -1;
	}

	struct port_lua *port = (struct port_lua *)base;
	port->vtab = &port_lua_vtab;
	port->L = L;
	port->ref = coro_ref;
	return 0;
}

int
box_lua_call(struct call_request *request, struct port *port)
{
	return box_process_lua(request, port, execute_lua_call);
}

int
box_lua_eval(struct call_request *request, struct port *port)
{
	return box_process_lua(request, port, execute_lua_eval);
}

static int
lbox_func_reload(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	if (box_func_reload(name) != 0)
		return luaT_error(L);
	return 0;
}

static const struct luaL_Reg boxlib_internal[] = {
	{"call_loadproc",  lbox_call_loadproc},
	{"func_reload", lbox_func_reload},
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
