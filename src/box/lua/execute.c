#include "execute.h"
#include "lua/utils.h"
#include "box/sql/sqlInt.h"
#include "box/port.h"
#include "box/execute.h"

/**
 * Serialize a description of the prepared statement.
 *
 * @param stmt Prepared statement.
 * @param L Lua stack.
 * @param column_count Statement's column count.
 */
static inline void
lua_sql_get_metadata(struct sql_stmt *stmt, struct lua_State *L,
		     int column_count)
{
	assert(column_count > 0);
	lua_createtable(L, column_count, 0);
	for (int i = 0; i < column_count; ++i) {
		lua_createtable(L, 0, 2);
		const char *name = sql_column_name(stmt, i);
		const char *type = sql_column_datatype(stmt, i);
		/*
		 * Can not fail, since all column names are
		 * preallocated during prepare phase and the
		 * column_name simply returns them.
		 */
		assert(name != NULL);
		assert(type != NULL);
		lua_pushstring(L, name);
		lua_setfield(L, -2, "name");
		lua_pushstring(L, type);
		lua_setfield(L, -2, "type");
		lua_rawseti(L, -2, i + 1);
	}
}

void
port_sql_dump_lua(struct port *port, struct lua_State *L)
{
	assert(port->vtab == &port_sql_vtab);
	struct sql *db = sql_get();
	struct sql_stmt *stmt = ((struct port_sql *)port)->stmt;
	int column_count = sql_column_count(stmt);
	if (column_count > 0) {
		lua_createtable(L, 0, 2);
		lua_sql_get_metadata(stmt, L, column_count);
		lua_setfield(L, -2, "metadata");
		port_tuple_vtab.dump_lua(port, L);
		lua_setfield(L, -2, "rows");
	} else {
		assert(((struct port_tuple *)port)->size == 0);
		struct stailq *autoinc_id_list =
			vdbe_autoinc_id_list((struct Vdbe *)stmt);
		lua_createtable(L, 0, stailq_empty(autoinc_id_list) ? 1 : 2);

		luaL_pushuint64(L, db->nChange);
		lua_setfield(L, -2, sql_info_key_strs[SQL_INFO_ROW_COUNT]);

		if (!stailq_empty(autoinc_id_list)) {
			lua_newtable(L);
			int i = 1;
			struct autoinc_id_entry *id_entry;
			stailq_foreach_entry(id_entry, autoinc_id_list, link) {
				if (id_entry->id >= 0)
					luaL_pushuint64(L, id_entry->id);
				else
					luaL_pushint64(L, id_entry->id);
				lua_rawseti(L, -2, i++);
			}
			const char *field_name =
				sql_info_key_strs[SQL_INFO_AUTOINCREMENT_IDS];
			lua_setfield(L, -2, field_name);
		}
	}
}

static int
lbox_execute(struct lua_State *L)
{
	struct sql_bind *bind = NULL;
	int bind_count = 0;
	size_t length;
	struct port port;
	int top = lua_gettop(L);

	if (top != 1 || ! lua_isstring(L, 1))
		return luaL_error(L, "Usage: box.execute(sqlstring)");

	const char *sql = lua_tolstring(L, 1, &length);
	if (sql_prepare_and_execute(sql, length, bind, bind_count, &port,
				    &fiber()->gc) != 0)
		return luaT_error(L);
	port_dump_lua(&port, L);
	port_destroy(&port);
	return 1;
}

void
box_lua_execute_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "execute");
	lua_pushcfunction(L, lbox_execute);
	lua_settable(L, -3);
	lua_pop(L, 1);
}
