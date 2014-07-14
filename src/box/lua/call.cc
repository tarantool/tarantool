/*
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
#include "pickle.h"

#include "box/lua/error.h"
#include "box/lua/tuple.h"
#include "box/lua/index.h"
#include "box/lua/space.h"
#include "box/lua/stat.h"
#include "box/lua/info.h"
#include "box/tuple.h"

#include "lua/utils.h"
#include "lua/msgpack.h"
#include "tbuf.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "box/box.h"
#include "box/port.h"
#include "box/request.h"
#include "box/txn.h"
#include "box/access.h"
#include "box/schema.h"

/* contents of box.lua, misc.lua, box.net.lua respectively */
extern char schema_lua[], load_cfg_lua[];
static const char *lua_sources[] = { schema_lua, load_cfg_lua, NULL };

/*
 * Functions, exported in box_lua.h should have prefix
 * "box_lua_"; functions, available in Lua "box"
 * should start with "lbox_".
 */

/** {{{ Lua I/O: facilities to intercept box output
 * and push into Lua stack.
 */

struct port_lua
{
	struct port_vtab *vtab;
	struct lua_State *L;
};

static inline struct port_lua *
port_lua(struct port *port) { return (struct port_lua *) port; }

/*
 * For addU32/dupU32 do nothing -- the only uint32_t Box can give
 * us is tuple count, and we don't need it, since we intercept
 * everything into Lua stack first.
 * @sa port_add_lua_multret
 */

static void
port_lua_add_tuple(struct port *port, struct tuple *tuple)
{
	lua_State *L = port_lua(port)->L;
	try {
		lbox_pushtuple(L, tuple);
	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}

void
port_lua_create(struct port_lua *port, struct lua_State *L)
{
	static struct port_vtab port_lua_vtab = {
		port_lua_add_tuple,
		null_port_eof,
	};
	port->vtab = &port_lua_vtab;
	port->L = L;
}

static void
port_lua_table_add_tuple(struct port *port, struct tuple *tuple)
{
	lua_State *L = port_lua(port)->L;
	try {
		int idx = luaL_getn(L, -1);	/* TODO: can be optimized */
		lbox_pushtuple(L, tuple);
		lua_rawseti(L, -2, idx + 1);

	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}

/** Add all tuples to a Lua table. */
static struct port *
port_lua_table_create(struct lua_State *L)
{
	static struct port_vtab port_lua_vtab = {
		port_lua_table_add_tuple,
		null_port_eof,
	};
	struct port_lua *port = (struct port_lua *)
			region_alloc(&fiber()->gc, sizeof(struct port_lua));
	port->vtab = &port_lua_vtab;
	port->L = L;
	/* The destination table to append tuples to. */
	lua_newtable(L);
	return (struct port *) port;
}

static void
port_add_lua_ret(struct port *port, struct lua_State *L, int index)
{
	struct tuple *tuple = lua_totuple(L, index, index);
	TupleGuard guard(tuple);
	port_add_tuple(port, tuple);
}

/**
 * Add all elements from Lua stack to fiber iov.
 *
 * To allow clients to understand a complex return from
 * a procedure, we are compatible with SELECT protocol,
 * and return the number of return values first, and
 * then each return value as a tuple.
 *
 * If a Lua stack contains at least one scalar, each
 * value on the stack is converted to a tuple. A Lua
 * is converted to a tuple with multiple fields.
 *
 * If the stack is a Lua table, each member of which is
 * not scalar, each member of the table is converted to
 * a tuple. This way very large lists of return values can
 * be used, since Lua stack size is limited by 8000 elements,
 * while Lua table size is pretty much unlimited.
 */
static void
port_add_lua_multret(struct port *port, struct lua_State *L)
{
	int nargs = lua_gettop(L);
	/** Check if we deal with a table of tables. */
	if (nargs == 1 && lua_istable(L, 1)) {
		/*
		 * The table is not empty and consists of tables
		 * or tuples. Treat each table element as a tuple,
		 * and push it.
		 */
		lua_pushnil(L);
		int has_keys = lua_next(L, 1);
		if (has_keys  && (lua_istable(L, -1) || lua_istuple(L, -1))) {
			do {
				port_add_lua_ret(port, L, lua_gettop(L));
				lua_pop(L, 1);
			} while (lua_next(L, 1));
			return;
		} else if (has_keys) {
			lua_pop(L, 1);
		}
	}
	for (int i = 1; i <= nargs; ++i) {
		port_add_lua_ret(port, L, i);
	}
}

/* }}} */

/**
 * The main extension provided to Lua by Tarantool/Box --
 * ability to call INSERT/UPDATE/SELECT/DELETE from within
 * a Lua procedure.
 *
 * This is a low-level API, and it expects
 * all arguments to be packed in accordance
 * with the binary protocol format (iproto
 * header excluded).
 *
 * Signature:
 * box.process(op_code, request)
 */
static int
lbox_process(lua_State *L)
{
	uint32_t op = lua_tointeger(L, 1); /* Get the first arg. */
	size_t sz;
	const char *req = luaL_checklstring(L, 2, &sz); /* Second arg. */
	if (op == IPROTO_CALL) {
		/*
		 * We should not be doing a CALL from within a CALL.
		 * To invoke one stored procedure from another, one must
		 * do it in Lua directly. This deals with
		 * infinite recursion, stack overflow and such.
		 */
		return luaL_error(L, "box.process(CALL, ...) is not allowed");
	}
	/* Capture all output into a Lua table. */
	struct port *port_lua = port_lua_table_create(L);
	struct request request;
	request_create(&request, op);
	request_decode(&request, req, sz);
	box_process(port_lua, &request);

	return 1;
}

void
lbox_request_create(struct request *request,
		    struct lua_State *L, enum iproto_request_type type,
		    int key, int tuple)
{
	request_create(request, type);
	request->space_id = lua_tointeger(L, 1);
	if (key > 0) {
		struct tbuf *key_buf = tbuf_new(&fiber()->gc);
		luamp_encode(L, key_buf, key);
		request->key = key_buf->data;
		request->key_end = key_buf->data + key_buf->size;
		if (mp_typeof(*request->key) != MP_ARRAY)
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
	}
	if (tuple > 0) {
		struct tbuf *tuple_buf = tbuf_new(&fiber()->gc);
		luamp_encode(L, tuple_buf, tuple);
		request->tuple = tuple_buf->data;
		request->tuple_end = tuple_buf->data + tuple_buf->size;
		if (mp_typeof(*request->tuple) != MP_ARRAY)
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
	}
}

static void
port_ffi_add_tuple(struct port *port, struct tuple *tuple)
{
	struct port_ffi *port_ffi = (struct port_ffi *) port;
	if (port_ffi->size >= port_ffi->capacity) {
		uint32_t capacity = (port_ffi->capacity > 0) ?
				2 * port_ffi->capacity : 1024;
		struct tuple **ret = (struct tuple **)
			realloc(port_ffi->ret, sizeof(*ret) * capacity);
		assert(ret != NULL);
		port_ffi->ret = ret;
		port_ffi->capacity = capacity;
	}
	port_ffi->ret[port_ffi->size++] = tuple;
}

struct port_vtab port_ffi_vtab = {
	port_ffi_add_tuple,
	null_port_eof,
};

void
port_ffi_create(struct port_ffi *port)
{
	memset(port, 0, sizeof(*port));
	port->vtab = &port_ffi_vtab;
}

void
port_ffi_destroy(struct port_ffi *port)
{
	free(port->ret);
	port->capacity = port->size = 0;
}

int
boxffi_select(struct port *port, uint32_t space_id, uint32_t index_id,
	      int iterator, uint32_t offset, uint32_t limit,
	      const char *key, const char *key_end)
{
	struct request request;
	request_create(&request, IPROTO_SELECT);
	request.space_id = space_id;
	request.index_id = index_id;
	request.limit = limit;
	request.offset = offset;
	request.iterator = iterator;
	request.key = key;
	request.key_end = key_end;

	try {
		box_process(port, &request);
		return 0;
	} catch (Exception *e) {
		/* will be hanled by box.error() in Lua */
		return -1;
	}
}

static int
lbox_insert(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:insert(tuple)");

	struct request request;
	struct port_lua port;
	lbox_request_create(&request, L, IPROTO_INSERT, -1, 2);
	port_lua_create(&port, L);
	box_process((struct port *) &port, &request);
	return lua_gettop(L) - 2;
}

static int
lbox_replace(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:replace(tuple)");

	struct request request;
	struct port_lua port;
	lbox_request_create(&request, L, IPROTO_REPLACE, -1, 2);
	port_lua_create(&port, L);
	box_process((struct port *) &port, &request);
	return lua_gettop(L) - 2;
}

static int
lbox_update(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "Usage space:update(key, ops)");

	struct request request;
	struct port_lua port;
	lbox_request_create(&request, L, IPROTO_UPDATE, 3, 4);
	request.field_base = 1; /* field ids are one-indexed */
	port_lua_create(&port, L);
	/* Ignore index_id for now */
	box_process((struct port *) &port, &request);
	return lua_gettop(L) - 4;
}

static int
lbox_delete(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "Usage space:delete(key)");

	struct request request;
	struct port_lua port;
	lbox_request_create(&request, L, IPROTO_DELETE, 3, -1);
	port_lua_create(&port, L);
	/* Ignore index_id for now */
	box_process((struct port *) &port, &request);
	return lua_gettop(L) - 3;
}

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
		if (! lua_istable(L, -1))
			tnt_raise(ClientError, ER_NO_SUCH_PROC,
				  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
	}

	/* box.something:method */
	if ((end = (const char *) memchr(start, ':', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! (lua_istable(L, -1) ||
			lua_islightuserdata(L, -1) || lua_isuserdata(L, -1) ))
				tnt_raise(ClientError, ER_NO_SUCH_PROC,
					  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
		objstack = index;
	}


	lua_pushlstring(L, start, name_end - start);
	lua_gettable(L, index);
	if (! lua_isfunction(L, -1)) {
		/* lua_call or lua_gettable would raise a type error
		 * for us, but our own message is more verbose. */
		tnt_raise(ClientError, ER_NO_SUCH_PROC,
			  name_end - name, name);
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

static inline void
access_check_func(const char *name, uint32_t name_len,
		  struct user *user, uint8_t access)
{
	if (access == 0)
		return;

	struct func_def *func = func_by_name(name, name_len);
	if (func == NULL || (func->uid != user->uid && user->uid != ADMIN &&
			     access & ~func->access[user->auth_token])) {
		char name_buf[BOX_NAME_MAX + 1];
		snprintf(name_buf, sizeof(name_buf), "%.*s", name_len, name);

		tnt_raise(ClientError, ER_FUNCTION_ACCESS_DENIED,
			  priv_name(access), user->name, name_buf);
	}
}

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
void
box_lua_call(struct request *request, struct port *port)
{
	struct user *user = user();
	lua_State *L = lua_newthread(tarantool_L);
	LuarefGuard coro_ref(tarantool_L);
	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);

	uint8_t access = PRIV_X & ~user->universal_access;

	/* Try to find a function by name */
	int oc = box_lua_find(L, name, name + name_len);
	/**
	 * Check access to the function. Sic: the order
	 * is important, as is described in
	 * https://github.com/tarantool/tarantool/issues/300
	 * - if a function does not exist, say it first.
	 */
	access_check_func(name, name_len, user, access);
	/* Push the rest of args (a tuple). */
	const char *args = request->tuple;
	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "call: out of stack");

	for (uint32_t i = 0; i < arg_count; i++) {
		luamp_decode(L, &args);
	}
	lbox_call(L, arg_count + oc - 1, LUA_MULTRET);
	/* Send results of the called procedure to the client. */
	port_add_lua_multret(port, L);
}

static int
lbox_snapshot(struct lua_State *L)
{
	int ret = box_snapshot();
	if (ret == 0) {
		lua_pushstring(L, "ok");
		return 1;
	}
	luaL_error(L, "can't save snapshot, errno %d (%s)",
		   ret, strerror(ret));
	return 1;
}

static const struct luaL_reg boxlib[] = {
	{"snapshot", lbox_snapshot},
	{NULL, NULL}
};

static const struct luaL_reg boxlib_internal[] = {
	{"process", lbox_process},
	{"call_loadproc",  lbox_call_loadproc},
	{"insert", lbox_insert},
	{"replace", lbox_replace},
	{"update", lbox_update},
	{"delete", lbox_delete},
	{NULL, NULL}
};

void
box_lua_init(struct lua_State *L)
{
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);
	luaL_register_module(L, "box.internal", boxlib_internal);
	lua_pop(L, 1);

	box_lua_error_init(L);
	box_lua_tuple_init(L);
	box_lua_index_init(L);
	box_lua_space_init(L);
	box_lua_info_init(L);
	box_lua_stat_init(L);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	assert(lua_gettop(L) == 0);
}
