#include "sql.h"
#include "box/sql.h"
#include "lua/msgpack.h"

#include "box/sql/sqlInt.h"
#include <info.h>
#include "lua/info.h"
#include "lua/utils.h"

static void
lua_push_column_names(struct lua_State *L, struct sql_stmt *stmt)
{
	int column_count = sql_column_count(stmt);
	lua_createtable(L, column_count, 0);
	for (int i = 0; i < column_count; i++) {
		const char *name = sql_column_name(stmt, i);
		lua_pushstring(L, name == NULL ? "" : name);
		lua_rawseti(L, -2, i+1);
	}
}

static void
lua_push_row(struct lua_State *L, struct sql_stmt *stmt)
{
	int column_count = sql_column_count(stmt);

	lua_createtable(L, column_count, 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_array_metatable_ref);
	lua_setmetatable(L, -2);

	for (int i = 0; i < column_count; i++) {
		int type = sql_column_type(stmt, i);
		switch (type) {
		case SQL_INTEGER:
			luaL_pushint64(L, sql_column_int64(stmt, i));
			break;
		case SQL_FLOAT:
			lua_pushnumber(L, sql_column_double(stmt, i));
			break;
		case SQL_TEXT: {
			const void *text = sql_column_text(stmt, i);
			lua_pushlstring(L, text,
					sql_column_bytes(stmt, i));
			break;
		}
		case SQL_BLOB: {
			const void *blob = sql_column_blob(stmt, i);
			if (sql_column_subtype(stmt,i) == SQL_SUBTYPE_MSGPACK) {
				luamp_decode(L, luaL_msgpack_default,
					     (const char **)&blob);
			} else {
				lua_pushlstring(L, blob,
					sql_column_bytes(stmt, i));
			}
			break;
		}
		case SQL_NULL:
			lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_nil_ref);
			break;
		default:
			assert(0);
		}
		lua_rawseti(L, -2, i+1);
	}
}

static int
lua_sql_execute(struct lua_State *L)
{
	sql *db = sql_get();
	if (db == NULL)
		return luaL_error(L, "not ready");

	size_t length;
	const char *sql = lua_tolstring(L, 1, &length);
	if (sql == NULL)
		return luaL_error(L, "usage: box.sql.execute(sqlstring)");

	struct sql_stmt *stmt;
	if (sql_prepare_v2(db, sql, length, &stmt, &sql) != SQL_OK)
		goto sqlerror;
	assert(stmt != NULL);

	int rc;
	int retval_count;
	if (sql_column_count(stmt) == 0) {
		while ((rc = sql_step(stmt)) == SQL_ROW);
		retval_count = 0;
	} else {
		lua_newtable(L);
		lua_pushvalue(L, lua_upvalueindex(1));
		lua_setmetatable(L, -2);
		lua_push_column_names(L, stmt);
		lua_rawseti(L, -2, 0);

		int row_count = 0;
		while ((rc = sql_step(stmt)) == SQL_ROW) {
			lua_push_row(L, stmt);
			lua_rawseti(L, -2, ++row_count);
		}
		retval_count = 1;
	}
        if (rc != SQL_OK && rc != SQL_DONE)
		goto sqlerror;
	sql_finalize(stmt);
	return retval_count;
sqlerror:
	lua_pushstring(L, sql_errmsg(db));
	sql_finalize(stmt);
	return lua_error(L);
}

static int
lua_sql_debug(struct lua_State *L)
{
	struct info_handler info;
	luaT_info_handler_create(&info, L);
	sql_debug_info(&info);
	return 1;
}

void
box_lua_sql_init(struct lua_State *L)
{
	static const struct luaL_Reg module_funcs [] = {
		{"execute", lua_sql_execute},
		{"debug", lua_sql_debug},
		{NULL, NULL}
	};

	/* used by lua_sql_execute via upvalue */
	lua_createtable(L, 0, 1);
	lua_pushstring(L, "sequence");
	lua_setfield(L, -2, "__serialize");

	luaL_openlib(L, "box.sql", module_funcs, 1);
	lua_pop(L, 1);
}

