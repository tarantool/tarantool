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
#include "box/box.h"
#include "box/lua/call.h"
#include "box/call.h"
#include "box/error.h"
#include "box/func.h"
#include "box/func_def.h"
#include "box/schema.h"
#include "fiber.h"
#include "tt_static.h"

#include "lua/utils.h"
#include "lua/serializer.h"
#include "lua/msgpack.h"
#include "lua/trigger.h"

#include "box/port.h"
#include "box/lua/misc.h"
#include "box/lua/tuple.h"
#include "small/obuf.h"
#include "trivia/util.h"
#include "mpstream/mpstream.h"
#include "box/session.h"
#include "box/iproto_features.h"
#include "core/mp_ctx.h"

/**
 * Handlers identifiers to obtain lua_Cfunction reference from
 * Lua registry table. These handlers are initialized on Tarantool
 * startup and are used until the Lua universe is destroyed.
 * Such approach reduces Lua GC usage since there is no need to
 * create short-lived GCfunc objects for the corresponding C
 * function on each iproto CALL/EVAL request or stored Lua
 * procedure call.
 */
enum handlers {
	HANDLER_CALL,
	HANDLER_CALL_BY_REF,
	HANDLER_ENCODE_CALL,
	HANDLER_ENCODE_CALL_16,
	HANDLER_EVAL,
	HANDLER_MAX,
};

static int execute_lua_refs[HANDLER_MAX];

/**
 * A copy of the default serializer with encode_error_as_ext option disabled.
 * Changes to the default serializer are propagated via an update trigger.
 * It is used for returning errors in the legacy format to clients that do
 * not support the MP_ERROR MsgPack extension.
 */
static struct luaL_serializer call_serializer_no_error_ext;

/**
 * Returns a serializer that should be used for encoding CALL/EVAL result.
 */
static struct luaL_serializer *
get_call_serializer(void)
{
	if (!iproto_features_test(&current_session()->meta.features,
				  IPROTO_FEATURE_ERROR_EXTENSION)) {
		return &call_serializer_no_error_ext;
	} else {
		return luaL_msgpack_default;
	}
}

int
box_lua_find(lua_State *L, const char *name, const char *name_end)
{
	lua_checkstack(L, 2); /* No more than 2 entries are needed. */
	int top = lua_gettop(L);

	/* Take the first token. */
	const char *start = name;
	while (start != name_end && *start != '.' &&
	       *start != ':' && *start != '[')
		start++;
	lua_pushlstring(L, name, start - name);
	lua_gettable(L, LUA_GLOBALSINDEX);

	/* Take the rest tokens. */
	while (start != name_end) {
		if (!lua_istable(L, -1) &&
		    !lua_islightuserdata(L, -1) && !lua_isuserdata(L, -1))
			goto no_such_proc;

		char delim = *start++; /* skip delimiter. */
		if (delim == '.') {
			/* Look for the next token. */
			const char *end = start;
			while (end != name_end && *end != '.' &&
			       *end != ':' && *end != '[')
				end++;
			lua_pushlstring(L, start, end - start);
			start = end;
		} else if (delim == ':') {
			lua_pushlstring(L, start, name_end - start);
			lua_gettable(L, -2); /* get function from object. */
			lua_insert(L, -2); /* swap function and object. */
			break;
		} else if (delim == '[') {
			const char *end = memchr(start, ']', name_end - start);
			if (end == NULL)
				goto no_such_proc;

			if (end - start >= 2 && start[0] == end[-1] &&
			    (start[0] == '"' || start[0] == '\'')) {
				/* Quoted string, just extract it. */
				lua_pushlstring(L, start + 1, end - start - 2);
			} else {
				/* Must be a number, convert from string. */
				lua_pushlstring(L, start, end - start);
				int success;
				lua_Number num = lua_tonumberx(L, -1, &success);
				if (!success)
					goto no_such_proc;
				lua_pop(L, 1);
				lua_pushnumber(L, num);
			}
			start = end + 1; /* skip closing bracket. */
		} else {
			goto no_such_proc;
		}

		lua_gettable(L, -2); /* get child object from parent object. */
		lua_remove(L, -2); /* drop previous parent object. */
	}

	/* Now at top+1 must be the function, and at top+2 may be the object. */
	assert(lua_gettop(L) - top >= 1 && lua_gettop(L) - top <= 2);
	if (!lua_isfunction(L, top + 1) && !lua_istable(L, top + 1)) {
		/* lua_call or lua_gettable would raise a type error
		 * for us, but our own message is more verbose. */
		goto no_such_proc;
	}

	return lua_gettop(L) - top;

no_such_proc:
	diag_set(ClientError, ER_NO_SUCH_PROC, tt_cstr(name, name_end - name));
	return -1;
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
	int count = box_lua_find(L, name, name + name_len);
	if (count < 0)
		return luaT_error(L);
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
			if (luaL_tofield(L, cfg, i, &field) < 0)
				return luaT_error(L);
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
				mpstream_encode_array(stream, 1);
				if (luamp_encode_r(L, cfg, stream, &field, 0) != 0)
					return luaT_error(L);
				lua_pop(L, 1);
			} else {
				/* `return ..., array, ...` */
				if (luamp_encode(L, cfg, stream, i) != 0)
					return luaT_error(L);
			}
		}
		return nrets;
	}
	assert(nrets == 1);

	/*
	 * Inspect the first result
	 */
	struct luaL_field root;
	if (luaL_tofield(L, cfg, 1, &root) < 0)
		return luaT_error(L);
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
		mpstream_encode_array(stream, 1);
		assert(lua_gettop(L) == 1);
		if (luamp_encode_r(L, cfg, stream, &root, 0) != 0)
			return luaT_error(L);
		return 1;
	}

	assert(root.type == MP_ARRAY);
	if (root.size == 0) {
		/* `return {}` => `{ box.tuple() }` */
		mpstream_encode_array(stream, 0);
		return 1;
	}

	/* `return { tuple, scalar, tuple }` */
	assert(root.type == MP_ARRAY && root.size > 0);
	for (uint32_t t = 1; t <= root.size; t++) {
		lua_rawgeti(L, 1, t);
		struct luaL_field field;
		if (luaL_tofield(L, cfg, -1, &field) < 0)
			return luaT_error(L);
		if (field.type == MP_EXT && (tuple = luaT_istuple(L, -1))) {
			tuple_to_mpstream(tuple, stream);
		} else if (field.type != MP_ARRAY) {
			/* The first member of root table is not tuple/array */
			if (t == 1) {
				/*
				 * `return { scalar, ... } =>
				 *        box.tuple.new(scalar, ...)`
				 */
				mpstream_encode_array(stream, root.size);
				/*
				 * Encode the first field of tuple using
				 * existing information from luaL_tofield
				 */
				if (luamp_encode_r(L, cfg, stream, &field, 0) != 0)
					return luaT_error(L);
				lua_pop(L, 1);
				assert(lua_gettop(L) == 1);
				/* Encode remaining fields as usual */
				for (uint32_t f = 2; f <= root.size; f++) {
					lua_rawgeti(L, 1, f);
					if (luamp_encode(L, cfg, stream, -1) != 0)
						return luaT_error(L);
					lua_pop(L, 1);
				}
				return 1;
			}
			/*
			 * `return { tuple/array, ..., scalar, ... } =>
			 *         { tuple/array, ..., { scalar }, ... }`
			 */
			mpstream_encode_array(stream, 1);
			if (luamp_encode_r(L, cfg, stream, &field, 0) != 0)
				return luaT_error(L);

		} else {
			/* `return { tuple/array, ..., tuple/array, ... }` */
			if (luamp_encode_r(L, cfg, stream, &field, 0) != 0)
				return luaT_error(L);
		}
		lua_pop(L, 1);
		assert(lua_gettop(L) == 1);
	}
	return root.size;
}

static const struct port_vtab port_lua_vtab;

void
port_lua_create_at(struct port *port, struct lua_State *L, int bottom)
{
	struct port_lua *port_lua = (struct port_lua *) port;
	memset(port_lua, 0, sizeof(*port_lua));
	port_lua->vtab = &port_lua_vtab;
	port_lua->L = L;
	/*
	 * Allow to destroy the port even if no ref.
	 * @Sa luaL_unref.
	 */
	port_lua->ref = -1;
	port_lua->bottom = bottom;
}

bool
port_is_lua(struct port *port)
{
	return port->vtab == &port_lua_vtab;
}

struct execute_lua_ctx {
	int lua_ref;
	const char *name;
	uint32_t name_len;
	bool takes_raw_args;
	struct port *args;
};

static inline void
push_lua_args(lua_State *L, struct execute_lua_ctx *ctx)
{
	port_dump_lua(ctx->args, L, ctx->takes_raw_args ?
				    PORT_DUMP_LUA_MODE_MP_OBJECT :
				    PORT_DUMP_LUA_MODE_FLAT);
}

/**
 * Find a lua function by name and execute it. Used for body-less
 * UDFs, which may not yet be defined when a function definition
 * is loaded from _func table, or dynamically re-defined at any
 * time. We don't cache references to such functions.
 */
static int
execute_lua_call(lua_State *L)
{
	struct execute_lua_ctx *ctx =
		(struct execute_lua_ctx *) lua_topointer(L, 1);
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	const char *name = ctx->name;
	uint32_t name_len = ctx->name_len;

	/* How many objects are on stack after box_lua_find. */
	int oc = box_lua_find(L, name, name + name_len);
	if (oc < 0)
		return luaT_error(L);

	/* Push the rest of args (a tuple). */
	int top = lua_gettop(L);
	push_lua_args(L, ctx);
	int arg_count = lua_gettop(L) - top;

	lua_call(L, arg_count + oc - 1, LUA_MULTRET);
	return lua_gettop(L);
}

/**
 * Dereference a sandboxed function and execute it. Used for
 * persistent UDFs.
 */
static int
execute_lua_call_by_ref(lua_State *L)
{
	struct execute_lua_ctx *ctx =
		(struct execute_lua_ctx *) lua_topointer(L, 1);
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua_ref);

	/* Push the rest of args (a tuple). */
	int top = lua_gettop(L);
	push_lua_args(L, ctx);
	int arg_count = lua_gettop(L) - top;

	lua_call(L, arg_count, LUA_MULTRET);
	return lua_gettop(L);
}

static int
execute_lua_eval(lua_State *L)
{
	struct execute_lua_ctx *ctx =
		(struct execute_lua_ctx *) lua_topointer(L, 1);
	lua_settop(L, 0); /* clear the stack to simplify the logic below */

	/* Compile expression */
	const char *expr = ctx->name;
	uint32_t expr_len = ctx->name_len;
	if (luaL_loadbuffer(L, expr, expr_len, "=eval")) {
		diag_set(LuajitError, lua_tostring(L, -1));
		luaT_error_at(L, 0);
	}

	/* Unpack arguments */
	int top = lua_gettop(L);
	push_lua_args(L, ctx);
	int arg_count = lua_gettop(L) - top;

	/* Call compiled code */
	lua_call(L, arg_count, LUA_MULTRET);
	return lua_gettop(L);
}

struct encode_lua_ctx {
	struct port_lua *port;
	struct mpstream *stream;
	/** MsgPack encoding context to save meta information to. */
	struct mp_ctx *mp_ctx;
};

/**
 * Encode call results to msgpack from Lua stack.
 * Lua stack has the following structure -- the last element is
 * lightuserdata pointer to encode_lua_ctx, all other values are
 * arguments to process.
 * The function encodes all given Lua objects to msgpack stream
 * from context, sets port's size and returns no value on the Lua
 * stack.
 * XXX: This function *MUST* be called under lua_pcall(), because
 * luamp_encode() may raise an error.
 */
static int
encode_lua_call(lua_State *L)
{
	assert(lua_islightuserdata(L, -1));
	struct encode_lua_ctx *encode_lua_ctx =
		(struct encode_lua_ctx *) lua_topointer(L, -1);
	assert(encode_lua_ctx->port->L == L);
	/* Delete ctx from the stack. */
	lua_pop(L, 1);
	/*
	 * Add all elements from Lua stack to the buffer.
	 *
	 * TODO: forbid explicit yield from __serialize or __index here
	 */
	struct luaL_serializer *cfg = get_call_serializer();
	const int size = lua_gettop(L);
	for (int i = 1; i <= size; ++i) {
		if (luamp_encode_with_ctx(L, cfg, encode_lua_ctx->stream, i,
					  encode_lua_ctx->mp_ctx, NULL) != 0)
			return luaT_error(L);
	}
	encode_lua_ctx->port->size = size;
	mpstream_flush(encode_lua_ctx->stream);
	return 0;
}

/**
 * Encode call_16 results to msgpack from Lua stack.
 * Lua stack has the following structure -- the last element is
 * lightuserdata pointer to encode_lua_ctx, all other values are
 * arguments to process.
 * The function encodes all given Lua objects to msgpack stream
 * from context, sets port's size and returns no value on the Lua
 * stack.
 * XXX: This function *MUST* be called under lua_pcall(), because
 * luamp_encode() may raise an error.
 */
static int
encode_lua_call_16(lua_State *L)
{
	assert(lua_islightuserdata(L, -1));
	struct encode_lua_ctx *ctx =
		(struct encode_lua_ctx *) lua_topointer(L, -1);
	assert(ctx->port->L == L);
	/* Delete ctx from the stack. */
	lua_pop(L, 1);
	/*
	 * Add all elements from Lua stack to the buffer.
	 *
	 * TODO: forbid explicit yield from __serialize or __index here
	 */
	struct luaL_serializer *cfg = get_call_serializer();
	ctx->port->size = luamp_encode_call_16(L, cfg, ctx->stream);
	mpstream_flush(ctx->stream);
	return 0;
}

/**
 * Get port contents as raw MsgPack. Encodes the port's Lua values on
 * the current fiber's region using a MsgPack stream.
 */
static const char *
port_lua_get_msgpack(struct port *base, uint32_t *size);

static inline int
port_lua_do_dump_with_ctx(struct port *base, struct mpstream *stream,
			  enum handlers handler, struct mp_ctx *mp_ctx)
{
	struct port_lua *port = (struct port_lua *) base;
	assert(port->vtab == &port_lua_vtab);
	/*
	 * Use the same global state, assuming the encoder doesn't
	 * yield.
	 */
	struct encode_lua_ctx encode_lua_ctx = {
		.port = port,
		.stream = stream,
		.mp_ctx = mp_ctx,
	};
	lua_State *L = port->L;
	/*
	 * At the moment Lua stack holds only values to encode.
	 * Push corresponding encoder, push duplicates of values
	 * so that the port can be dumped multiple times and push
	 * encode context as lightuserdata to the top.
	 */
	const int size = lua_gettop(L) - port->bottom + 1;
	lua_rawgeti(L, LUA_REGISTRYINDEX, execute_lua_refs[handler]);
	assert(lua_isfunction(L, -1) && lua_iscfunction(L, -1));
	for (int i = 0; i < size; i++)
		lua_pushvalue(L, port->bottom + i);
	lua_pushlightuserdata(L, &encode_lua_ctx);
	/* nargs -- all arguments + lightuserdata. */
	if (luaT_call(L, size + 1, 0) != 0)
		return -1;
	return port->size;
}

static inline int
port_lua_do_dump(struct port *base, struct mpstream *stream,
		 enum handlers handler)
{
	return port_lua_do_dump_with_ctx(base, stream, handler, NULL);
}

/**
 * Dump Lua port contents to output buffer in MsgPack format.
 */
static int
port_lua_dump(struct port *base, struct obuf *out, struct mp_ctx *ctx)
{
	struct port_lua *port = (struct port_lua *) base;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      luamp_error, port->L);
	return port_lua_do_dump_with_ctx(base, &stream, HANDLER_ENCODE_CALL,
					 ctx);
}

/**
 * Dump Lua port contents to output buffer in MsgPack format.
 */
static int
port_lua_dump_16(struct port *base, struct obuf *out, struct mp_ctx *ctx)
{
	struct port_lua *port = (struct port_lua *)base;
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb,
		      luamp_error, port->L);
	return port_lua_do_dump_with_ctx(base, &stream, HANDLER_ENCODE_CALL_16,
					 ctx);
}

/**
 * Dump port contents to Lua. Simply moves values from the Lua stack owned by
 * the port to the provided Lua stack.
 */
static void
port_lua_dump_lua(struct port *base, struct lua_State *L,
		  enum port_dump_lua_mode mode)
{
	assert(mode == PORT_DUMP_LUA_MODE_FLAT ||
	       mode == PORT_DUMP_LUA_MODE_MP_OBJECT);
	if (mode == PORT_DUMP_LUA_MODE_FLAT) {
		struct port_lua *port = (struct port_lua *)base;
		uint32_t size = lua_gettop(port->L) - port->bottom + 1;
		/*
		 * Duplicate values so that the port can be dumped multiple
		 * times.
		 */
		for (size_t i = 0; i < size; i++)
			lua_pushvalue(port->L, port->bottom + i);
		lua_xmove(port->L, L, size);
		port->size = size;
	} else {
		port_dump_lua_mp_object_mode_slow(base, L, &fiber()->gc,
						  port_lua_get_msgpack);
	}
}

static const char *
port_lua_get_msgpack(struct port *base, uint32_t *size)
{
	struct port_lua *port = (struct port_lua *) base;
	struct region *region = &fiber()->gc;
	uint32_t region_svp = region_used(region);
	struct mpstream stream;
	int port_size = lua_gettop(port->L) - port->bottom + 1;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      luamp_error, port->L);
	mpstream_encode_array(&stream, port_size);
	int rc = port_lua_do_dump(base, &stream, HANDLER_ENCODE_CALL);
	if (rc < 0) {
		region_truncate(region, region_svp);
		return NULL;
	}
	*size = region_used(region) - region_svp;
	const char *data = region_join(region, *size);
	if (data == NULL) {
		diag_set(OutOfMemory, *size, "region", "data");
		region_truncate(region, region_svp);
		return NULL;
	}
	return data;
}

static void
port_lua_destroy(struct port *base)
{
	struct port_lua *port = (struct port_lua *)base;
	assert(port->vtab == &port_lua_vtab);
	lua_settop(port->L, port->bottom - 1);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, port->ref);
}

/**
 * Dump port lua as a YAML document. It is extern since depends on
 * lyaml module.
 */
extern const char *
port_lua_dump_plain(struct port *port, uint32_t *size);

extern struct Mem *
port_lua_get_vdbemem(struct port *base, uint32_t *size);

const struct port_c_entry *
port_lua_get_c_entries(struct port *base)
{
	struct port_lua *port = (struct port_lua *)base;
	struct lua_State *L = port->L;
	size_t size = lua_gettop(L) - port->bottom + 1;
	if (size == 0)
		return NULL;

	struct port_c_entry *arr =
		xregion_alloc_array(&fiber()->gc, struct port_c_entry, size);

	/* Link the list. */
	for (size_t i = 0; i < size - 1; i++)
		arr[i].next = &arr[i + 1];
	arr[size - 1].next = NULL;

	/* Put values. */
	for (size_t arr_idx = 0; arr_idx < size; arr_idx++) {
		int lua_idx = arr_idx + port->bottom;
		struct port_c_entry *e = &arr[arr_idx];
		switch (lua_type(L, lua_idx)) {
		case LUA_TNIL:
			e->type = PORT_C_ENTRY_NULL;
			break;
		case LUA_TBOOLEAN:
			e->type = PORT_C_ENTRY_BOOL;
			e->boolean = lua_toboolean(L, lua_idx);
			break;
		case LUA_TNUMBER:
			e->type = PORT_C_ENTRY_NUMBER;
			e->number = lua_tonumber(L, lua_idx);
			break;
		case LUA_TSTRING: {
			e->type = PORT_C_ENTRY_STR;
			size_t len;
			const char *data = lua_tolstring(L, lua_idx, &len);
			e->str.data = data;
			e->str.size = len;
			break;
		}
		default: {
			struct tuple *tuple = NULL;
			tuple = luaT_istuple(L, lua_idx);
			if (tuple != NULL) {
				e->type = PORT_C_ENTRY_TUPLE;
				e->tuple = tuple;
				/*
				 * Do not reference the tuple:
				 * this entry does not own it.
				 */
				break;
			}

			size_t len;
			const char *data = NULL;
			data = luamp_get(L, lua_idx, &len);
			if (data != NULL) {
				e->type = PORT_C_ENTRY_MP_OBJECT;
				e->mp.data = data;
				e->mp.size = len;
				e->mp.ctx = NULL;
				break;
			}

			if (luaL_isnull(L, lua_idx)) {
				e->type = PORT_C_ENTRY_NULL;
				break;
			}

			/* Unsupported value. */
			e->type = PORT_C_ENTRY_UNKNOWN;
		}
		}
	}
	return arr;
}

static const struct port_vtab port_lua_vtab = {
	.dump_msgpack = port_lua_dump,
	.dump_msgpack_16 = port_lua_dump_16,
	.dump_lua = port_lua_dump_lua,
	.dump_plain = port_lua_dump_plain,
	.get_msgpack = port_lua_get_msgpack,
	.get_vdbemem = port_lua_get_vdbemem,
	.get_c_entries = port_lua_get_c_entries,
	.destroy = port_lua_destroy,
};

static inline int
box_process_lua(enum handlers handler, struct execute_lua_ctx *ctx,
		struct port *ret)
{
	lua_State *L = luaT_newthread(tarantool_L);
	if (L == NULL)
		return -1;
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	port_lua_create(ret, L);
	((struct port_lua *) ret)->ref = coro_ref;

	/*
	 * A code that need a temporary fiber-local Lua state may
	 * save some time and resources for creating a new state
	 * and use this one.
	 */
	bool has_lua_stack = fiber()->storage.lua.stack != NULL;
	if (!has_lua_stack)
		fiber()->storage.lua.stack = L;

	lua_rawgeti(L, LUA_REGISTRYINDEX, execute_lua_refs[handler]);
	assert(lua_isfunction(L, -1));
	lua_pushlightuserdata(L, ctx);
	if (luaT_call(L, 1, LUA_MULTRET) != 0) {
		if (!has_lua_stack)
			fiber()->storage.lua.stack = NULL;
		port_lua_destroy(ret);
		return -1;
	}

	/*
	 * Since this field is optional we're not obligated to
	 * keep it until the Lua state will be unreferenced in
	 * port_lua_destroy().
	 *
	 * There is no much sense to keep it beyond the Lua call,
	 * so let's zap now.
	 *
	 * But: keep the stack if it was present before the call,
	 * because it would be counter-intuitive if the existing
	 * state pointer would be zapped after this function call.
	 */
	if (!has_lua_stack)
		fiber()->storage.lua.stack = NULL;

	return 0;
}

int
box_lua_call(const char *name, uint32_t name_len,
	     struct port *args, struct port *ret)
{
	struct execute_lua_ctx ctx;
	ctx.name = name;
	ctx.name_len = name_len;
	ctx.args = args;
	ctx.takes_raw_args = false;
	return box_process_lua(HANDLER_CALL, &ctx, ret);
}

int
box_lua_eval(const char *expr, uint32_t expr_len,
	     struct port *args, struct port *ret)
{
	struct execute_lua_ctx ctx;
	ctx.name = expr;
	ctx.name_len = expr_len;
	ctx.args = args;
	ctx.takes_raw_args = false;
	return box_process_lua(HANDLER_EVAL, &ctx, ret);
}

struct func_lua {
	/** Function object base class. */
	struct func base;
	/**
	 * For a persistent function: a reference to the
	 * function body. Otherwise LUA_REFNIL.
	 */
	int lua_ref;
};

static struct func_vtab func_lua_vtab;
static struct func_vtab func_persistent_lua_vtab;

static const char *default_sandbox_exports[] = {
	"assert", "error", "ipairs", "math", "next", "pairs", "pcall", "print",
	"select", "string", "table", "tonumber", "tostring", "type", "unpack",
	"xpcall", "utf8",
};

/**
 * Assemble a new sandbox with given exports table on the top of
 * a given Lua stack. All modules in exports list are copied
 * deeply to ensure the immutability of this system object.
 */
static int
prepare_lua_sandbox(struct lua_State *L, const char *exports[],
		    int export_count)
{
	lua_createtable(L, export_count, 0);
	if (export_count == 0)
		return 0;
	int rc = -1;
	const char *deepcopy = "table.deepcopy";
	int luaL_deepcopy_func_ref = LUA_REFNIL;
	int ret = box_lua_find(L, deepcopy, deepcopy + strlen(deepcopy));
	if (ret < 0)
		goto end;
	luaL_deepcopy_func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	assert(luaL_deepcopy_func_ref != LUA_REFNIL);
	for (int i = 0; i < export_count; i++) {
		uint32_t name_len = strlen(exports[i]);
		ret = box_lua_find(L, exports[i], exports[i] + name_len);
		if (ret < 0)
			goto end;
		switch (lua_type(L, -1)) {
		case LUA_TTABLE:
			lua_rawgeti(L, LUA_REGISTRYINDEX,
				    luaL_deepcopy_func_ref);
			lua_insert(L, -2);
			lua_call(L, 1, 1);
			break;
		case LUA_TFUNCTION:
			break;
		default:
			unreachable();
		}
		lua_setfield(L, -2, exports[i]);
	}
	rc = 0;
end:
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, luaL_deepcopy_func_ref);
	return rc;
}

/**
 * Assemble a Lua function object by user-defined function body.
 * On success, returns a Lua reference to the loaded function.
 * On error, returns LUA_NOREF and sets diag.
 */
static int
func_persistent_lua_load(const struct func_def *def)
{
	assert(def->body != NULL);
	int func_ref = LUA_NOREF;
	int top = lua_gettop(tarantool_L);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *load_pref = "return ";
	uint32_t load_str_sz =
		strlen(load_pref) + strlen(def->body) + 1;
	char *load_str = region_alloc(region, load_str_sz);
	if (load_str == NULL) {
		diag_set(OutOfMemory, load_str_sz, "region", "load_str");
		return LUA_NOREF;
	}
	snprintf(load_str, load_str_sz, "%s%s", load_pref, def->body);

	/*
	 * Perform loading of the persistent Lua function
	 * in a new sandboxed Lua thread. The sandbox is
	 * required to guarantee the safety of executing
	 * an arbitrary user-defined code
	 * (e.g. body = 'fiber.yield()').
	 */
	lua_State *coro_L = luaT_newthread(tarantool_L);
	if (coro_L == NULL)
		return LUA_NOREF;
	if (!def->is_sandboxed) {
		/*
		 * Keep the original env to apply to a non-sandboxed
		 * persistent function. It is necessary since
		 * the created object inherits its parent env.
		 */
		lua_getfenv(tarantool_L, -1);
		lua_insert(tarantool_L, -2);
	}
	if (prepare_lua_sandbox(tarantool_L, NULL, 0) != 0)
		unreachable();
	lua_setfenv(tarantool_L, -2);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	if (luaL_loadstring(coro_L, load_str) != 0 ||
	    lua_pcall(coro_L, 0, 1, 0) != 0) {
		diag_set(ClientError, ER_LOAD_FUNCTION, def->name,
			 luaT_tolstring(coro_L, -1, NULL));
		goto end;
	}
	if (!lua_isfunction(coro_L, -1)) {
		diag_set(ClientError, ER_LOAD_FUNCTION, def->name,
			 "given body doesn't define a function");
		goto end;
	}
	lua_xmove(coro_L, tarantool_L, 1);
	if (def->is_sandboxed) {
		if (prepare_lua_sandbox(tarantool_L, default_sandbox_exports,
					nelem(default_sandbox_exports)) != 0) {
			diag_add(ClientError, ER_LOAD_FUNCTION,
				 def->name, "can't prepare a Lua sandbox");
			goto end;
		}
	} else {
		lua_insert(tarantool_L, -2);
	}
	lua_setfenv(tarantool_L, -2);
	func_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
end:
	lua_settop(tarantool_L, top);
	region_truncate(region, region_svp);
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	return func_ref;
}

struct func *
func_lua_new(const struct func_def *def)
{
	assert(def->language == FUNC_LANGUAGE_LUA);
	struct func_lua *func =
		(struct func_lua *) malloc(sizeof(struct func_lua));
	if (func == NULL) {
		diag_set(OutOfMemory, sizeof(*func), "malloc", "func");
		return NULL;
	}
	if (def->body != NULL) {
		func->lua_ref = func_persistent_lua_load(def);
		if (func->lua_ref == LUA_NOREF) {
			free(func);
			return NULL;
		}
		func->base.vtab = &func_persistent_lua_vtab;
	} else {
		func->lua_ref = LUA_REFNIL;
		func->base.vtab = &func_lua_vtab;
	}
	return &func->base;
}

static void
func_lua_destroy(struct func *func)
{
	assert(func != NULL && func->def->language == FUNC_LANGUAGE_LUA);
	assert(func->vtab == &func_lua_vtab);
	TRASH(func);
	free(func);
}

static inline int
func_lua_call(struct func *func, struct port *args, struct port *ret)
{
	assert(func != NULL && func->def->language == FUNC_LANGUAGE_LUA);
	assert(func->vtab == &func_lua_vtab);
	struct execute_lua_ctx ctx;
	ctx.name = func->def->name;
	ctx.name_len = func->def->name_len;
	ctx.args = args;
	ctx.takes_raw_args = func->def->opts.takes_raw_args;
	return box_process_lua(HANDLER_CALL, &ctx, ret);
}

static struct func_vtab func_lua_vtab = {
	.call = func_lua_call,
	.destroy = func_lua_destroy,
};

static void
func_persistent_lua_unload(struct func_lua *func)
{
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, func->lua_ref);
}

static void
func_persistent_lua_destroy(struct func *base)
{
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_LUA &&
	       base->def->body != NULL);
	assert(base->vtab == &func_persistent_lua_vtab);
	struct func_lua *func = (struct func_lua *) base;
	func_persistent_lua_unload(func);
	free(func);
}

static inline int
func_persistent_lua_call(struct func *base, struct port *args, struct port *ret)
{
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_LUA &&
	       base->def->body != NULL);
	assert(base->vtab == &func_persistent_lua_vtab);
	struct func_lua *func = (struct func_lua *)base;
	struct execute_lua_ctx ctx;
	ctx.lua_ref = func->lua_ref;
	ctx.args = args;
	ctx.takes_raw_args = base->def->opts.takes_raw_args;
	return box_process_lua(HANDLER_CALL_BY_REF, &ctx, ret);

}

static struct func_vtab func_persistent_lua_vtab = {
	.call = func_persistent_lua_call,
	.destroy = func_persistent_lua_destroy,
};

static int
lbox_module_reload(lua_State *L)
{
	if (box_check_configured() != 0)
		return luaT_error(L);
	const char *name = luaT_checkstring(L, 1);
	if (box_module_reload(name) != 0)
		return luaT_error(L);
	return 0;
}

int
lbox_func_call(struct lua_State *L)
{
	if (box_check_configured() != 0)
		return luaT_error(L);
	if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
		diag_set(IllegalParams, "Use func:call(...)");
		return luaT_error(L);
	}

	size_t name_len;
	const char *name = lua_tolstring(L, 1, &name_len);
	struct func *func = func_by_name(name, name_len);
	if (func == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FUNCTION,
			 tt_cstr(name, name_len));
		return luaT_error(L);
	}

	/*
	 * Prepare a new Lua stack for input arguments
	 * before the function call to pass it into the
	 * pcall-sandboxed tarantool_L handler.
	 */
	lua_State *args_L = luaT_newthread(tarantool_L);
	if (args_L == NULL)
		return luaT_error(L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_xmove(L, args_L, lua_gettop(L) - 1);
	struct port args;
	port_lua_create(&args, args_L);
	((struct port_lua *) &args)->ref = coro_ref;

	struct port ret;
	if (func_call(func, &args, &ret) != 0) {
		port_destroy(&args);
		return luaT_error(L);
	}

	int top = lua_gettop(L);
	port_dump_lua(&ret, L, PORT_DUMP_LUA_MODE_FLAT);
	int cnt = lua_gettop(L) - top;

	port_destroy(&ret);
	port_destroy(&args);
	return cnt;
}

static void
lbox_func_new(struct lua_State *L, struct func *func)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_getfield(L, -1, "func");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1); /* pop nil */
		lua_newtable(L);
		lua_setfield(L, -2, "func");
		lua_getfield(L, -1, "func");
	}
	lua_rawgeti(L, -1, func->def->fid);
	if (lua_isnil(L, -1)) {
		/*
		 * If the function already exists, modify it,
		 * rather than create a new one -- to not
		 * invalidate Lua variable references to old func
		 * outside the box.schema.func[].
		 */
		lua_pop(L, 1);
		lua_newtable(L);
		lua_rawseti(L, -2, func->def->fid);
		lua_rawgeti(L, -1, func->def->fid);
	} else {
		/* Clear the reference to old func by old name. */
		lua_getfield(L, -1, "name");
		lua_pushnil(L);
		lua_settable(L, -4);
	}
	int top = lua_gettop(L);
	lua_pushstring(L, "id");
	lua_pushnumber(L, func->def->fid);
	lua_settable(L, top);
	lua_pushstring(L, "name");
	lua_pushstring(L, func->def->name);
	lua_settable(L, top);
	lua_pushstring(L, "setuid");
	lua_pushboolean(L, func->def->setuid);
	lua_settable(L, top);
	lua_pushstring(L, "language");
	lua_pushstring(L, func_language_strs[func->def->language]);
	lua_settable(L, top);
	lua_pushstring(L, "returns");
	lua_pushstring(L, field_type_strs[func->def->returns]);
	lua_settable(L, top);
	lua_pushstring(L, "aggregate");
	lua_pushstring(L, func_aggregate_strs[func->def->aggregate]);
	lua_settable(L, top);
	lua_pushstring(L, "body");
	if (func->def->body != NULL)
		lua_pushstring(L, func->def->body);
	else
		lua_pushnil(L);
	lua_settable(L, top);
	lua_pushstring(L, "comment");
	if (func->def->comment != NULL)
		lua_pushstring(L, func->def->comment);
	else
		lua_pushnil(L);
	lua_settable(L, top);
	lua_pushstring(L, "exports");
	lua_newtable(L);
	lua_pushboolean(L, func->def->exports.lua);
	lua_setfield(L, -2, "lua");
	lua_pushboolean(L, func->def->exports.sql);
	lua_setfield(L, -2, "sql");
	lua_settable(L, -3);
	lua_pushstring(L, "is_deterministic");
	lua_pushboolean(L, func->def->is_deterministic);
	lua_settable(L, top);
	lua_pushstring(L, "is_multikey");
	lua_pushboolean(L, func->def->opts.is_multikey);
	lua_settable(L, top);
	lua_pushstring(L, "takes_raw_args");
	lua_pushboolean(L, func->def->opts.takes_raw_args);
	lua_settable(L, top);
	lua_pushstring(L, "is_sandboxed");
	if (func->def->body != NULL)
		lua_pushboolean(L, func->def->is_sandboxed);
	else
		lua_pushnil(L);
	lua_settable(L, top);

	/* Bless func object. */
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "schema");
	lua_gettable(L, -2);
	lua_pushstring(L, "func");
	lua_gettable(L, -2);
	lua_pushstring(L, "bless");
	lua_gettable(L, -2);

	lua_pushvalue(L, top);
	lua_call(L, 1, 0);
	lua_pop(L, 3);

	lua_setfield(L, -2, func->def->name);

	lua_pop(L, 2);
}

static void
lbox_func_delete(struct lua_State *L, struct func *func)
{
	uint32_t fid = func->def->fid;
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_getfield(L, -1, "func");
	assert(!lua_isnil(L, -1));
	lua_rawgeti(L, -1, fid);
	if (!lua_isnil(L, -1)) {
		lua_getfield(L, -1, "name");
		lua_pushnil(L);
		lua_rawset(L, -4);
		lua_pop(L, 1); /* pop func */
		lua_pushnil(L);
		lua_rawseti(L, -2, fid);
	} else {
		lua_pop(L, 1);
	}
	lua_pop(L, 2); /* box, func */
}

static int
lbox_func_new_or_delete(struct trigger *trigger, void *event)
{
	struct lua_State *L = (struct lua_State *) trigger->data;
	struct func *func = (struct func *)event;
	if (!func->def->exports.lua)
		return 0;
	if (func_by_id(func->def->fid) != NULL)
		lbox_func_new(L, func);
	else
		lbox_func_delete(L, func);
	return 0;
}

static int
lbox_box_lua_call_runtime_priv_reset(struct lua_State *L)
{
	if (lua_gettop(L) != 0) {
		diag_set(IllegalParams,
			 "Usage: box.internal.lua_call_runtime_priv_reset()");
		return luaT_error(L);
	}

	box_lua_call_runtime_priv_reset();
	return 0;
}

static int
lbox_box_lua_call_runtime_priv_grant(struct lua_State *L)
{
	if (lua_gettop(L) != 2 || lua_type(L, 1) != LUA_TSTRING ||
	    lua_type(L, 2) != LUA_TSTRING) {
		diag_set(IllegalParams,
			 "Usage: box.internal.lua_call_runtime_priv_grant(user, func)");
		return luaT_error(L);
	}

	size_t grantee_name_len;
	const char *grantee_name = luaL_checklstring(L, 1, &grantee_name_len);

	size_t func_name_len;
	const char *func_name = luaL_checklstring(L, 2, &func_name_len);

	box_lua_call_runtime_priv_grant(grantee_name,
					(uint32_t)grantee_name_len,
					func_name, (uint32_t)func_name_len);
	return 0;
}

static void
call_serializer_update_options(void)
{
	luaL_serializer_copy_options(&call_serializer_no_error_ext,
				     luaL_msgpack_default);
	call_serializer_no_error_ext.encode_error_as_ext = 0;
}

static int
on_msgpack_serializer_update(struct trigger *trigger, void *event)
{
	(void)trigger;
	(void)event;
	call_serializer_update_options();
	return 0;
}

static TRIGGER(on_alter_func_in_lua, lbox_func_new_or_delete);

static const struct luaL_Reg boxlib_internal[] = {
	{"call_loadproc",  lbox_call_loadproc},
	{"module_reload", lbox_module_reload},
	{"func_call", lbox_func_call},
	{"lua_call_runtime_priv_grant", lbox_box_lua_call_runtime_priv_grant},
	{"lua_call_runtime_priv_reset", lbox_box_lua_call_runtime_priv_reset},
	{NULL, NULL}
};

void
box_lua_call_init(struct lua_State *L)
{
	call_serializer_update_options();
	trigger_create(&call_serializer_no_error_ext.update_trigger,
		       on_msgpack_serializer_update, NULL, NULL);
	trigger_add(&luaL_msgpack_default->on_update,
		    &call_serializer_no_error_ext.update_trigger);

	luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal", 0);
	luaL_setfuncs(L, boxlib_internal, 0);
	lua_pop(L, 1);
	/*
	 * Register the trigger that will push persistent
	 * Lua functions objects to Lua.
	 */
	on_alter_func_in_lua.data = L;
	trigger_add(&on_alter_func, &on_alter_func_in_lua);

	lua_CFunction handles[] = {
		[HANDLER_CALL] = execute_lua_call,
		[HANDLER_CALL_BY_REF] = execute_lua_call_by_ref,
		[HANDLER_ENCODE_CALL] = encode_lua_call,
		[HANDLER_ENCODE_CALL_16] = encode_lua_call_16,
		[HANDLER_EVAL] = execute_lua_eval,
	};

	for (int i = 0; i < HANDLER_MAX; i++) {
		lua_pushcfunction(L, handles[i]);
		execute_lua_refs[i] = luaL_ref(L, LUA_REGISTRYINDEX);
	}

#if 0
	/* Get CTypeID for `struct port *' */
	int rc = luaL_cdef(L, "struct port;");
	assert(rc == 0);
	(void) rc;
	CTID_STRUCT_PORT_PTR = luaL_ctypeid(L, "struct port *");
	assert(CTID_STRUCT_TUPLE_REF != 0);
#endif
}
