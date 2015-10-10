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
#include "pickle.h"

#include "box/lua/error.h"
#include "box/lua/tuple.h"
#include "box/lua/index.h"
#include "box/lua/space.h"
#include "box/lua/stat.h"
#include "box/lua/sophia.h"
#include "box/lua/info.h"
#include "box/lua/session.h"
#include "box/lua/net_box.h"
#include "box/tuple.h"

#include "lua/utils.h"
#include "lua/msgpack.h"
#include "iobuf.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "box/box.h"
#include "box/port.h"
#include "box/request.h"
#include "box/engine.h"
#include "box/txn.h"
#include "box/user_def.h"
#include "box/user.h"
#include "box/func.h"
#include "box/schema.h"
#include "box/session.h"
#include "box/iproto_constants.h"
#include "box/iproto_port.h"

/* contents of box.lua, misc.lua, box.net.lua respectively */
extern char session_lua[],
	schema_lua[],
	load_cfg_lua[],
	snapshot_daemon_lua[],
	net_box_lua[];

static const char *lua_sources[] = {
	session_lua,
	schema_lua,
	snapshot_daemon_lua,
	load_cfg_lua,
	net_box_lua,
	NULL
};

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
	size_t size; /* for port_lua_add_tuple */
};

static inline struct port_lua *
port_lua(struct port *port) { return (struct port_lua *) port; }

/*
 * For addU32/dupU32 do nothing -- the only uint32_t Box can give
 * us is tuple count, and we don't need it, since we intercept
 * everything into Lua stack first.
 * @sa port_add_lua_multret
 */

extern "C" void
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
		lbox_pushtuple(L, tuple);
		lua_rawseti(L, -2, ++port_lua(port)->size);
	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}

/** Add all tuples to a Lua table. */
void
port_lua_table_create(struct port_lua *port, struct lua_State *L)
{
	static struct port_vtab port_lua_vtab = {
		port_lua_table_add_tuple,
		null_port_eof,
	};
	port->vtab = &port_lua_vtab;
	port->L = L;
	port->size = 0;
	/* The destination table to append tuples to. */
	lua_newtable(L);
}

/* }}} */

static int
lbox_select(lua_State *L)
{
	if (lua_gettop(L) != 6 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
		!lua_isnumber(L, 3) || !lua_isnumber(L, 4) || !lua_isnumber(L, 5)) {
		return luaL_error(L, "Usage index:select(iterator, offset, "
				  "limit, key)");
	}

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	int iterator = lua_tointeger(L, 3);
	uint32_t offset = lua_tointeger(L, 4);
	uint32_t limit = lua_tointeger(L, 5);

	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 6, &key_len);

	int top = lua_gettop(L);
	struct port_lua port;
	port_lua_table_create(&port, L);
	if (box_select((struct port *) &port, space_id, index_id, iterator,
			offset, limit, key, key + key_len) != 0)
		return lbox_error(L);
	return lua_gettop(L) - top;
}

static int
lbox_insert(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:insert(tuple)");

	uint32_t space_id = lua_tointeger(L, 1);
	size_t tuple_len;
	const char *tuple = lbox_encode_tuple_on_gc(L, 2, &tuple_len);

	box_tuple_t *result;
	if (box_insert(space_id, tuple, tuple + tuple_len, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_replace(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:replace(tuple)");

	uint32_t space_id = lua_tointeger(L, 1);
	size_t tuple_len;
	const char *tuple = lbox_encode_tuple_on_gc(L, 2, &tuple_len);

	box_tuple_t *result;
	if (box_replace(space_id, tuple, tuple + tuple_len, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_update(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    lua_type(L, 3) != LUA_TTABLE || lua_type(L, 4) != LUA_TTABLE)
		return luaL_error(L, "Usage index:update(key, ops)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);
	size_t ops_len;
	const char *ops = lbox_encode_tuple_on_gc(L, 4, &ops_len);

	box_tuple_t *result;
	if (box_update(space_id, index_id, key, key + key_len,
		       ops, ops + ops_len, 1, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_upsert(lua_State *L)
{
	if (lua_gettop(L) != 5 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    lua_type(L, 3) != LUA_TTABLE || lua_type(L, 4) != LUA_TTABLE ||
	    lua_type(L, 5) != LUA_TTABLE)
		return luaL_error(L, "Usage index:upsert(key, ops, tuple)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);
	size_t ops_len;
	const char *ops = lbox_encode_tuple_on_gc(L, 4, &ops_len);
	size_t tuple_len;
	const char *tuple = lbox_encode_tuple_on_gc(L, 5, &tuple_len);

	box_tuple_t *result;
	if (box_upsert(space_id, index_id, key, key + key_len,
		       ops, ops + ops_len, tuple, tuple + tuple_len, 1,
		       &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_delete(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    lua_type(L, 3) != LUA_TTABLE)
		return luaL_error(L, "Usage space:delete(key)");

	uint32_t space_id = lua_tointeger(L, 1);
	uint32_t index_id = lua_tointeger(L, 2);
	size_t key_len;
	const char *key = lbox_encode_tuple_on_gc(L, 3, &key_len);

	box_tuple_t *result;
	if (box_delete(space_id, index_id, key, key + key_len, &result) != 0)
		return lbox_error(L);
	return lbox_pushtupleornil(L, result);
}

static int
lbox_commit(lua_State *L)
{
	if (box_txn_commit() != 0)
		return lbox_error(L);
	return 0;
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
	if (!lua_isfunction(L, -1) && !lua_istable(L, -1)) {
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

/**
 * Check access to a function and change the current
 * user id if the function is a set-definer-user-id one.
 * The original user is restored in the destructor.
 */
struct SetuidGuard
{
	/** True if the function was set-user-id one. */
	bool setuid;
	struct credentials *orig_credentials;

	inline SetuidGuard(const char *name, uint32_t name_len,
			   uint8_t access, struct func *func);
	inline ~SetuidGuard();
};

SetuidGuard::SetuidGuard(const char *name, uint32_t name_len,
			 uint8_t access, struct func *func)
	:setuid(false)
	,orig_credentials(current_user())
{

	/*
	 * If the user has universal access, don't bother with setuid.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	if ((orig_credentials->universal_access & PRIV_ALL) == PRIV_ALL)
		return;
	access &= ~orig_credentials->universal_access;
	if (func == NULL && access == 0) {
		/**
		 * Well, the function is not explicitly defined,
		 * so it's obviously not a setuid one.
		 */
		return;
	}
	if (func == NULL || (func->def.uid != orig_credentials->uid &&
	     access & ~func->access[orig_credentials->auth_token].effective)) {
		/* Access violation, report error. */
		char name_buf[BOX_NAME_MAX + 1];
		snprintf(name_buf, sizeof(name_buf), "%.*s", name_len, name);
		struct user *user = user_cache_find(orig_credentials->uid);

		tnt_raise(ClientError, ER_FUNCTION_ACCESS_DENIED,
			  priv_name(access), user->name, name_buf);
	}
	if (func->def.setuid) {
		/** Remember and change the current user id. */
		if (func->owner_credentials.auth_token >= BOX_USER_MAX) {
			/*
			 * Fill the cache upon first access, since
			 * when func is created, no user may
			 * be around to fill it (recovery of
			 * system spaces from a snapshot).
			 */
			struct user *owner = user_cache_find(func->def.uid);
			credentials_init(&func->owner_credentials, owner);
		}
		setuid = true;
		fiber_set_user(fiber(), &func->owner_credentials);
	}
}

SetuidGuard::~SetuidGuard()
{
	if (setuid)
		fiber_set_user(fiber(), orig_credentials);
}


static inline void
execute_c_call(struct func *func, struct request *request, struct obuf *out)
{
	assert(func != NULL && func->def.language == FUNC_LANGUAGE_C);
	if (func->func == NULL)
		func_load(func);

	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);
	SetuidGuard setuid(name, name_len, PRIV_X, func);

	struct port_buf port_buf;
	port_buf_create(&port_buf);
	auto guard = make_scoped_guard([&]{
		port_buf_destroy(&port_buf);
	});

	box_function_ctx_t ctx = { request, &port_buf.base };
	int rc = 0;
	try {
		rc = func->func(&ctx, request->tuple, request->tuple_end);
	} catch (...) {
		panic("C++ exception thrown from stored C function");
	}
	if (in_txn()) {
		say_warn("a transaction is active at CALL return");
		txn_rollback();
	}

	if (rc != 0) {
		fiber_testerror();
		tnt_raise(ClientError, ER_PROC_C, "unknown procedure error");
	}

	struct obuf_svp svp = iproto_prepare_select(out);
	try {
		for (struct port_buf_entry *entry = port_buf.first;
		     entry != NULL; entry = entry->next) {
			tuple_to_obuf(entry->tuple, out);
		}
		iproto_reply_select(out, &svp, request->header->sync,
				    port_buf.size);
	} catch (Exception *e) {
		obuf_rollback_to_svp(out, &svp);
		txn_rollback();
		/* Let all well-behaved exceptions pass through. */
		throw;
	}
}

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
static inline void
execute_lua_call(lua_State *L, struct func *func, struct request *request,
		 struct obuf *out)
{
	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);

	int oc = 0; /* how many objects are on stack after box_lua_find */
	/* Try to find a function by name in Lua */
	oc = box_lua_find(L, name, name + name_len);
	/**
	 * Check access to the function and optionally change
	 * execution time user id (set user id). Sic: the order
	 * is important, as is described in
	 * https://github.com/tarantool/tarantool/issues/300
	 * - if a function does not exist, say it first.
	 */
	SetuidGuard setuid(name, name_len, PRIV_X, func);
	/* Push the rest of args (a tuple). */
	const char *args = request->tuple;

	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "call: out of stack");

	for (uint32_t i = 0; i < arg_count; i++)
		luamp_decode(L, luaL_msgpack_default, &args);
	lua_call(L, arg_count + oc - 1, LUA_MULTRET);

	if (in_txn()) {
		say_warn("a transaction is active at CALL return");
		txn_rollback();
	}

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
	struct obuf_svp svp = iproto_prepare_select(out);
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb);

	try {
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
		iproto_reply_select(out, &svp, request->header->sync, count);
	} catch (...) {
		obuf_rollback_to_svp(out, &svp);
		throw;
	}
}

void
box_lua_call(struct request *request, struct obuf *out)
{
	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);

	struct func *func = func_by_name(name, name_len);
	if (func != NULL && func->def.language == FUNC_LANGUAGE_C)
		return execute_c_call(func, request, out);

	/*
	 * func == NULL means that perhaps the user has a global
	 * "EXECUTE" privilege, so no specific grant to a function.
	 */
	assert(func == NULL || func->def.language == FUNC_LANGUAGE_LUA);
	lua_State *L = NULL;
	try {
		L = lua_newthread(tarantool_L);
		LuarefGuard coro_ref(tarantool_L);
		execute_lua_call(L, func, request, out);
	} catch (Exception *e) {
		txn_rollback();
		/* Let all well-behaved exceptions pass through. */
		throw;
	} catch (...) {
		txn_rollback();
		/* Convert Lua error to a Tarantool exception. */
		tnt_raise(LuajitError, L != NULL ? L : tarantool_L);
	}
}

static inline void
execute_eval(lua_State *L, struct request *request, struct obuf *out)
{
	/* Check permissions */
	access_check_universe(PRIV_X);

	/* Compile expression */
	const char *expr = request->key;
	uint32_t expr_len = mp_decode_strl(&expr);
	if (luaL_loadbuffer(L, expr, expr_len, "=eval"))
		tnt_raise(LuajitError, L);

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
	struct obuf_svp svp = iproto_prepare_select(out);
	struct mpstream stream;
	mpstream_init(&stream, out, obuf_reserve_cb, obuf_alloc_cb);
	int nrets = lua_gettop(L);
	try {
		for (int k = 1; k <= nrets; ++k) {
			luamp_encode(L, luaL_msgpack_default, &stream, k);
		}
		mpstream_flush(&stream);
		iproto_reply_select(out, &svp, request->header->sync, nrets);
	} catch (...) {
		obuf_rollback_to_svp(out, &svp);
		throw;
	}
}

void
box_lua_eval(struct request *request, struct obuf *out)
{
	lua_State *L = NULL;
	try {
		L = lua_newthread(tarantool_L);
		LuarefGuard coro_ref(tarantool_L);
		execute_eval(L, request, out);
	} catch (Exception *e) {
		/* Let all well-behaved exceptions pass through. */
		throw;
	} catch (...) {
		/* Convert Lua error to a Tarantool exception. */
		tnt_raise(LuajitError, L != NULL ? L : tarantool_L);
	}
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
	{"commit", lbox_commit},
	{NULL, NULL}
};

static const struct luaL_reg boxlib_internal[] = {
	{"call_loadproc",  lbox_call_loadproc},
	{"select", lbox_select},
	{"insert", lbox_insert},
	{"replace", lbox_replace},
	{"update", lbox_update},
	{"delete", lbox_delete},
	{"upsert", lbox_upsert},
	{NULL, NULL}
};

void
box_lua_init(struct lua_State *L)
{
	/* Use luaL_register() to set _G.box */
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);
	luaL_register(L, "box.internal", boxlib_internal);
	lua_pop(L, 1);
	luaopen_net_box(L);
	lua_pop(L, 1);

#if 0
	/* Get CTypeID for `struct port *' */
	int rc = luaL_cdef(L, "struct port;");
	assert(rc == 0);
	(void) rc;
	CTID_STRUCT_PORT_PTR = luaL_ctypeid(L, "struct port *");
	assert(CTID_CONST_STRUCT_TUPLE_REF != 0);
#endif
	box_lua_error_init(L);
	box_lua_tuple_init(L);
	box_lua_index_init(L);
	box_lua_space_init(L);
	box_lua_info_init(L);
	box_lua_stat_init(L);
	box_lua_sophia_init(L);
	box_lua_session_init(L);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	assert(lua_gettop(L) == 0);
}
