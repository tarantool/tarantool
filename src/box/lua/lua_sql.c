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
#include "box/sql/sqliteInt.h"
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
lua_sql_call(sqlite3_context *pCtx, int nVal, sqlite3_value **apVal) {
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	struct lua_sql_func_info *func_info = sqlite3_user_data(pCtx);

	lua_rawgeti(L, LUA_REGISTRYINDEX, func_info->func_ref);
	for (int i = 0; i < nVal; i++) {
		sqlite3_value *param = apVal[i];
		switch (sqlite3_value_type(param)) {
		case SQLITE_INTEGER:
			luaL_pushint64(L, sqlite3_value_int64(param));
			break;
		case SQLITE_FLOAT:
			lua_pushnumber(L, sqlite3_value_double(param));
			break;
		case SQLITE_TEXT:
			lua_pushstring(L, (const char *) sqlite3_value_text(param));
			break;
		case SQLITE_BLOB:
			lua_pushlstring(L, sqlite3_value_blob(param),
					(size_t) sqlite3_value_bytes(param));
			break;
		case SQLITE_NULL:
			lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_nil_ref);
			break;
		default:
			sqlite3_result_error(pCtx, "Unsupported type passed "
					     "to Lua", -1);
			goto error;
		}
	}
	if (lua_pcall(L, lua_gettop(L) - 1, 1, 0) != 0){
		sqlite3_result_error(pCtx, lua_tostring(L, -1), -1);
		goto error;
	}
	switch(lua_type(L, -1)) {
	case LUA_TBOOLEAN:
		sqlite3_result_int(pCtx, lua_toboolean(L, -1));
		break;
	case LUA_TNUMBER:
		sqlite3_result_double(pCtx, lua_tonumber(L, -1));
		break;
	case LUA_TSTRING:
		sqlite3_result_text(pCtx, lua_tostring(L, -1), -1,
				    SQLITE_TRANSIENT);
		break;
	case LUA_TNIL:
		sqlite3_result_null(pCtx);
		break;
	default:
		sqlite3_result_error(pCtx, "Unsupported type returned from Lua",
				     -1);
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
 * sqlite3_create_function *p argument is used to store func ref
 * to lua function (it identifies actual lua func to call if there
 * are many of them). SQL function must have name and type of
 * returning value. Additionally, it can feature number of
 * arguments and deterministic flag.
 */
int
lbox_sql_create_function(struct lua_State *L)
{
	struct sqlite3 *db = sql_get();
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
	enum affinity_type type = AFFINITY_UNDEFINED;
	const char *type_arg = lua_tostring(L, 2);
	if (strcmp(type_arg, "INT") == 0 || strcmp(type_arg, "INTEGER") == 0)
		type = AFFINITY_INTEGER;
	else if (strcmp(type_arg, "TEXT") == 0)
		type = AFFINITY_TEXT;
	else if (strcmp(type_arg, "FLOAT") == 0)
		type = AFFINITY_REAL;
	else if (strcmp(type_arg, "NUM") == 0)
		type = AFFINITY_REAL;
	else if (strcmp(type_arg, "BLOB") == 0)
		type = AFFINITY_BLOB;
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
	char *normalized_name = (char *) region_alloc(&fiber()->gc,
						      name_len + 1);
	if (normalized_name == NULL)
		return luaL_error(L, "out of memory");
	memcpy(normalized_name, name, name_len);
	normalized_name[name_len] = '\0';
	sqlite3NormalizeName(normalized_name);
	struct lua_sql_func_info *func_info =
		(struct lua_sql_func_info *) malloc(sizeof(*func_info));
	if (func_info == NULL)
		return luaL_error(L, "out of memory");
	func_info->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	int rc = sqlite3_create_function_v2(db, normalized_name, type, func_arg_num,
					   is_deterministic ? SQLITE_DETERMINISTIC : 0,
					   func_info, lua_sql_call, NULL, NULL,
					   lua_sql_destroy);
	if (rc != 0)
		return luaL_error(L, sqlite3ErrStr(rc));
	return 0;
}
