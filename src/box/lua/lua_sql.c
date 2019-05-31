/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include "lua.h"
#include "lua/utils.h"

#include "box/lua/call.h"
#include "box/sql/sqlInt.h"
#include "box/sql/vdbeInt.h"

struct lua_sql_func_info {
	int func_ref;
};

/**
 * This function is callback which is called by sql engine.
 *
 * Purpose of this function is to call lua func from sql.
 * Lua func should be previously registered in sql
 * (see lbox_sql_create_function).
 */
static void
lua_sql_call(sql_context *pCtx, int nVal, sql_value **apVal) {
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	struct lua_sql_func_info *func_info = sql_user_data(pCtx);

	lua_rawgeti(L, LUA_REGISTRYINDEX, func_info->func_ref);
	for (int i = 0; i < nVal; i++) {
		sql_value *param = apVal[i];
		switch (sql_value_type(param)) {
		case MP_INT:
			luaL_pushint64(L, sql_value_int64(param));
			break;
		case MP_UINT:
			luaL_pushuint64(L, sql_value_int64(param));
			break;
		case MP_DOUBLE:
			lua_pushnumber(L, sql_value_double(param));
			break;
		case MP_STR:
			lua_pushstring(L, (const char *) sql_value_text(param));
			break;
		case MP_BIN:
			lua_pushlstring(L, sql_value_blob(param),
					(size_t) sql_value_bytes(param));
			break;
		case MP_NIL:
			lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_nil_ref);
			break;
		case MP_BOOL:
			lua_pushboolean(L, sql_value_boolean(param));
			break;
		default:
			diag_set(ClientError, ER_SQL_EXECUTE, "Unsupported "\
				 "type passed to Lua");
			pCtx->is_aborted = true;
			goto error;
		}
	}
	if (lua_pcall(L, lua_gettop(L) - 1, 1, 0) != 0){
		diag_set(ClientError, ER_SQL_EXECUTE, lua_tostring(L, -1));
		pCtx->is_aborted = true;
		goto error;
	}
	switch(lua_type(L, -1)) {
	case LUA_TBOOLEAN:
		sql_result_bool(pCtx, lua_toboolean(L, -1));
		break;
	case LUA_TNUMBER:
		sql_result_double(pCtx, lua_tonumber(L, -1));
		break;
	case LUA_TSTRING:
		sql_result_text(pCtx, lua_tostring(L, -1), -1,
				    SQL_TRANSIENT);
		break;
	case LUA_TNIL:
		sql_result_null(pCtx);
		break;
	default:
		diag_set(ClientError, ER_SQL_EXECUTE, "Unsupported type "\
			 "passed from Lua");
		pCtx->is_aborted = true;
		goto error;
	}
error:
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	return;
}

static void
lua_sql_destroy(void *p)
{
	struct lua_sql_func_info *func_info = p;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, func_info->func_ref);
	free(func_info);
	return;
}

/**
 * A helper to register lua function in SQL during runtime.
 * It makes available queries like this: "SELECT lua_func(arg);"
 *
 * sql_create_function *p argument is used to store func ref
 * to lua function (it identifies actual lua func to call if there
 * are many of them). SQL function must have name and type of
 * returning value. Additionally, it can feature number of
 * arguments and deterministic flag.
 */
int
lbox_sql_create_function(struct lua_State *L)
{
	struct sql *db = sql_get();
	if (db == NULL)
		return luaL_error(L, "Please call box.cfg{} first");
	int argc = lua_gettop(L);
	/*
	 * Three function prototypes are possible:
	 * 1. sql_create_function("func_name", "type", func);
	 * 2. sql_create_function("func_name", "type", func,
	 *                        func_arg_num);
	 * 3. sql_create_function("func_name", "type", func,
	 *                        func_arg_num, is_deterministic);
	 */
	if (!(argc == 3 && lua_isstring(L, 1) && lua_isstring(L, 2) &&
	    lua_isfunction(L, 3)) &&
	    !(argc == 4 && lua_isstring(L, 1) && lua_isstring(L, 2) &&
	      lua_isfunction(L, 3) && lua_isnumber(L, 4)) &&
	    !(argc == 5 && lua_isstring(L, 1) && lua_isstring(L, 2) &&
	      lua_isfunction(L, 3) && lua_isnumber(L, 4) &&
	      lua_isboolean(L, 5)))
		return luaL_error(L, "Invalid arguments");
	enum field_type type;
	const char *type_arg = lua_tostring(L, 2);
	if (strcmp(type_arg, "INT") == 0 || strcmp(type_arg, "INTEGER") == 0)
		type = FIELD_TYPE_INTEGER;
	else if (strcmp(type_arg, "TEXT") == 0)
		type = FIELD_TYPE_STRING;
	else if (strcmp(type_arg, "FLOAT") == 0)
		type = FIELD_TYPE_NUMBER;
	else if (strcmp(type_arg, "NUM") == 0)
		type = FIELD_TYPE_NUMBER;
	else if (strcmp(type_arg, "BLOB") == 0)
		type = FIELD_TYPE_SCALAR;
	else if (strcmp(type_arg, "BOOL") == 0 ||
		 strcmp(type_arg, "BOOLEAN") == 0)
		type = FIELD_TYPE_BOOLEAN;
	else
		return luaL_error(L, "Unknown type");
	/* -1 indicates any number of arguments. */
	int func_arg_num = -1;
	bool is_deterministic = false;
	if (argc == 4) {
		func_arg_num = lua_tointeger(L, 4);
		lua_pop(L, 1);
	} else if (argc == 5) {
		is_deterministic = lua_toboolean(L, 5);
		func_arg_num = lua_tointeger(L, 4);
		lua_pop(L, 2);
	}
	size_t name_len;
	const char *name = lua_tolstring(L, 1, &name_len);
	char *normalized_name =
		sql_normalized_name_region_new(&fiber()->gc, name, name_len);
	if (normalized_name == NULL)
		return luaT_error(L);
	struct lua_sql_func_info *func_info =
		(struct lua_sql_func_info *) malloc(sizeof(*func_info));
	if (func_info == NULL)
		return luaL_error(L, "out of memory");
	func_info->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	int rc = sql_create_function_v2(db, normalized_name, type, func_arg_num,
					   is_deterministic ? SQL_DETERMINISTIC : 0,
					   func_info, lua_sql_call, NULL, NULL,
					   lua_sql_destroy);
	if (rc != 0)
		return luaT_error(L);
	return 0;
}
