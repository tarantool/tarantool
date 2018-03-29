#include "sql.h"
#include "box/sql.h"

#include "box/sql/sqliteInt.h"
#include "box/info.h"
#include "lua/utils.h"
#include "info.h"

static void
lua_push_column_names(struct lua_State *L, struct sqlite3_stmt *stmt)
{
	int column_count = sqlite3_column_count(stmt);
	lua_createtable(L, column_count, 0);
	for (int i = 0; i < column_count; i++) {
		const char *name = sqlite3_column_name(stmt, i);
		lua_pushstring(L, name == NULL ? "" : name);
		lua_rawseti(L, -2, i+1);
	}
}

static void
lua_push_row(struct lua_State *L, struct sqlite3_stmt *stmt)
{
	int column_count = sqlite3_column_count(stmt);

	lua_createtable(L, column_count, 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_array_metatable_ref);
	lua_setmetatable(L, -2);

	for (int i = 0; i < column_count; i++) {
		int type = sqlite3_column_type(stmt, i);
		switch (type) {
		case SQLITE_INTEGER:
			luaL_pushint64(L, sqlite3_column_int64(stmt, i));
			break;
		case SQLITE_FLOAT:
			lua_pushnumber(L, sqlite3_column_double(stmt, i));
			break;
		case SQLITE_TEXT: {
			const void *text = sqlite3_column_text(stmt, i);
			lua_pushlstring(L, text,
					sqlite3_column_bytes(stmt, i));
			break;
		}
		case SQLITE_BLOB: {
			const void *blob = sqlite3_column_blob(stmt, i);
			lua_pushlstring(L, blob,
					sqlite3_column_bytes(stmt, i));
			break;
		}
		case SQLITE_NULL:
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
	sqlite3 *db = sql_get();
	if (db == NULL)
		return luaL_error(L, "not ready");

	size_t length;
	const char *sql = lua_tolstring(L, 1, &length);
	if (sql == NULL)
		return luaL_error(L, "usage: box.sql.execute(sqlstring)");

	struct sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db, sql, length, &stmt, &sql) != SQLITE_OK)
		goto sqlerror;
	assert(stmt != NULL);

	int rc;
	int retval_count;
	if (sqlite3_column_count(stmt) == 0) {
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW);
		retval_count = 0;
	} else {
		lua_newtable(L);
		lua_pushvalue(L, lua_upvalueindex(1));
		lua_setmetatable(L, -2);
		lua_push_column_names(L, stmt);
		lua_rawseti(L, -2, 0);

		int row_count = 0;
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			lua_push_row(L, stmt);
			lua_rawseti(L, -2, ++row_count);
		}
		retval_count = 1;
	}
        if (rc != SQLITE_OK && rc != SQLITE_DONE)
		goto sqlerror;
	sqlite3_finalize(stmt);
	return retval_count;
sqlerror:
	lua_pushstring(L, sqlite3_errmsg(db));
	sqlite3_finalize(stmt);
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
box_lua_sqlite_init(struct lua_State *L)
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

