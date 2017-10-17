/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *	copyright notice, this list of conditions and the
 *	following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials
 *	provided with the distribution.
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
#undef likely
#undef SWAP
#undef unlikely
#include <box/sql/sqliteInt.h>
#include <box/sql/vdbeInt.h>
#include <box/sql.h>

#define LUA_WRONG_TYPE_MESG "Unsupported type passed to lua"

struct lua_sql_func_info{
	int func_ref;
};


/**
 * This function is callback which is called by sql engine.
 *
 * Purpose of this function is to call lua func from sql.
 * Lua func should be previously registered in sql (see lbox_sql_create_function).
 *
 */
static void lua_sql_call(sqlite3_context *pCtx,
						 int nVal,
						 sqlite3_value **apVal) {
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	struct lua_sql_func_info *func_info = sqlite3_user_data(pCtx);

	lua_rawgeti(L, LUA_REGISTRYINDEX, func_info->func_ref);
	for (int i = 0; i < nVal; i++) {
		sqlite3_value *param = apVal[i];
		switch (sqlite3_value_type(param)) {
		case SQLITE_INTEGER: {
			luaL_pushint64(L, sqlite3_value_int64(param));
			break;
		}
		case SQLITE_FLOAT: {
			lua_pushnumber(L, sqlite3_value_double(param));
			break;
		}
		case SQLITE_TEXT: {
			lua_pushstring(L,
			               (const char *) sqlite3_value_text(param));
			break;
		}
		case SQLITE_BLOB: {
			const void *blob = sqlite3_value_blob(param);
			lua_pushlstring(L, blob,
			                (size_t) sqlite3_value_bytes(param));
			break;
		}
		case SQLITE_NULL: {
			lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_nil_ref);
			break;
		}
		default: {
			sqlite3_result_error(pCtx,
			                     LUA_WRONG_TYPE_MESG,
			                     -1);
			goto lua_sql_call_exit_1;
		}
		}
	}
	if (lua_pcall(L, lua_gettop(L) - 1, 1, 0) != 0){
		sqlite3_result_error(pCtx, lua_tostring(L, -1), -1);
		goto lua_sql_call_exit_1;
	}

	switch(lua_type(L, -1)){
	case LUA_TBOOLEAN:
		sqlite3_result_int(pCtx, lua_toboolean(L, -1));
		break;
	case LUA_TNUMBER:
		sqlite3_result_double(pCtx, lua_tonumber(L, -1));
		break;
	case LUA_TSTRING:
		sqlite3_result_text(pCtx,
		                    lua_tostring(L, -1),
		                    -1,
		                    SQLITE_TRANSIENT);
		break;
	case LUA_TNIL:
		sqlite3_result_null(pCtx);
		break;
	default:
		sqlite3_result_error(pCtx,
		                     "Unsupported type returned from lua",
		                     -1);
		goto lua_sql_call_exit_1;
	}
	lua_sql_call_exit_1:
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	return;
}

static void lua_sql_destroy(void * p)
{
	struct lua_sql_func_info *func_info = p;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, func_info->func_ref);
	free(func_info);
	return;
}

/**
 * A helper to register lua function in sql in runtime
 * It makes available queries like this: "SELECT lua_func(arg);"
 *
 * sqlite3_create_function *p argument is used to store func ref to
 * lua function (it identifies actual lua func to call if there are many of them)
 */

int lbox_sql_create_function(struct lua_State *L)
{
	int argc = lua_gettop(L);
	int func_arg_num = -1; // -1 is any arg num
	const char *name;
	size_t name_len;
	char *normalized_name;
	struct lua_sql_func_info *func_info;
	int rc;
	sqlite3 *db = sql_get();
	/**
	 * Check args. Two types are possible:
	 * sql_create_function("func_name", func)
	 * sql_create_function("func_name", func, func_arg_num)
	 */
	if (    !(argc == 2 && lua_isstring(L, 1) && lua_isfunction(L, 2)) &&
		!(argc == 3 && lua_isstring(L, 1) && lua_isfunction(L, 2) &&
			lua_isnumber(L, 3))){
		luaL_error(L, "Invalid arguments");
			return 0;
	}
	if (db == NULL){
		luaL_error(L, "Please call box.cfg{} first");
		return 0;
	}
	if (argc == 3){
		func_arg_num = (int) lua_tonumber(L, 3);
		// func should be on top of the stack because of luaL_ref api
		lua_pop(L, 1);
	}
	name = lua_tostring(L, 1);
	name_len = strlen(name);
	normalized_name = (char*) malloc(name_len+1);
	memcpy(normalized_name, name, name_len+1);
	sqlite3NormalizeName(normalized_name);
	func_info = (struct lua_sql_func_info*)malloc(sizeof(struct lua_sql_func_info));
	func_info->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	rc = sqlite3_create_function_v2(db, normalized_name, func_arg_num,
				   SQLITE_UTF8, func_info,
				   lua_sql_call, NULL, NULL, lua_sql_destroy);
	free(normalized_name);
	return rc;
}

