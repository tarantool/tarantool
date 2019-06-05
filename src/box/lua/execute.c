#include "execute.h"
#include "lua/utils.h"
#include "lua/msgpack.h"
#include "box/sql/sqlInt.h"
#include "box/port.h"
#include "box/execute.h"
#include "box/bind.h"

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
port_sql_dump_lua(struct port *port, struct lua_State *L, bool is_flat)
{
	(void) is_flat;
	assert(is_flat == false);
	assert(port->vtab == &port_sql_vtab);
	struct sql *db = sql_get();
	struct sql_stmt *stmt = ((struct port_sql *)port)->stmt;
	int column_count = sql_column_count(stmt);
	if (column_count > 0) {
		lua_createtable(L, 0, 2);
		lua_sql_get_metadata(stmt, L, column_count);
		lua_setfield(L, -2, "metadata");
		port_tuple_vtab.dump_lua(port, L, false);
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

/**
 * Decode a single bind column from Lua stack.
 *
 * @param L Lua stack.
 * @param[out] bind Bind to decode to.
 * @param idx Position of table with bind columns on Lua stack.
 * @param i Ordinal bind number.
 *
 * @retval  0 Success.
 * @retval -1 Memory or client error.
 */
static inline int
lua_sql_bind_decode(struct lua_State *L, struct sql_bind *bind, int idx, int i)
{
	struct luaL_field field;
	struct region *region = &fiber()->gc;
	char *buf;
	lua_rawgeti(L, idx, i + 1);
	bind->pos = i + 1;
	if (lua_istable(L, -1)) {
		/*
		 * Get key and value of the only table element to
		 * lua stack.
		 */
		lua_pushnil(L);
		lua_next(L, -2);
		if (! lua_isstring(L, -2)) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS, "name of the "\
				 "parameter should be a string.");
			return -1;
		}
		/* Check that the table is one-row sized. */
		lua_pushvalue(L, -2);
		if (lua_next(L, -4) != 0) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS, "SQL bind "\
				 "named parameter should be a table with "\
				 "one key - {name = value}");
			return -1;
		}
		size_t name_len;
		bind->name = lua_tolstring(L, -2, &name_len);
		/*
		 * Name should be saved in allocated memory as it
		 * will be poped from Lua stack.
		 */
		buf = region_alloc(region, name_len + 1);
		if (buf == NULL) {
			diag_set(OutOfMemory, name_len + 1, "region_alloc",
				 "buf");
			return -1;
		}
		memcpy(buf, bind->name, name_len + 1);
		bind->name = buf;
		bind->name_len = name_len;
	} else {
		bind->name = NULL;
		bind->name_len = 0;
	}
	if (luaL_tofield(L, luaL_msgpack_default, -1, &field) < 0)
		return -1;
	switch (field.type) {
	case MP_UINT:
		bind->u64 = field.ival;
		bind->bytes = sizeof(bind->u64);
		break;
	case MP_INT:
		bind->i64 = field.ival;
		bind->bytes = sizeof(bind->i64);
		break;
	case MP_STR:
		/*
		 * Data should be saved in allocated memory as it
		 * will be poped from Lua stack.
		 */
		buf = region_alloc(region, field.sval.len + 1);
		if (buf == NULL) {
			diag_set(OutOfMemory, field.sval.len + 1,
				 "region_alloc", "buf");
			return -1;
		}
		memcpy(buf, field.sval.data, field.sval.len + 1);
		bind->s = buf;
		bind->bytes = field.sval.len;
		break;
	case MP_DOUBLE:
	case MP_FLOAT:
		bind->d = field.dval;
		bind->bytes = sizeof(bind->d);
		break;
	case MP_NIL:
		bind->bytes = 1;
		break;
	case MP_BOOL:
		/* SQLite doesn't support boolean. Use int instead. */
		bind->i64 = field.bval ? 1 : 0;
		bind->bytes = sizeof(bind->i64);
		break;
	case MP_BIN:
		bind->s = mp_decode_bin(&field.sval.data, &bind->bytes);
		break;
	case MP_EXT:
		diag_set(ClientError, ER_SQL_BIND_TYPE, "USERDATA",
			 sql_bind_name(bind));
		return -1;
	case MP_ARRAY:
		diag_set(ClientError, ER_SQL_BIND_TYPE, "ARRAY",
			 sql_bind_name(bind));
		return -1;
	case MP_MAP:
		diag_set(ClientError, ER_SQL_BIND_TYPE, "MAP",
			 sql_bind_name(bind));
		return -1;
	default:
		unreachable();
	}
	bind->type = field.type;
	lua_pop(L, lua_gettop(L) - idx);
	return 0;
}

int
lua_sql_bind_list_decode(struct lua_State *L, struct sql_bind **out_bind,
			 int idx)
{
	assert(out_bind != NULL);
	uint32_t bind_count = lua_objlen(L, idx);
	if (bind_count == 0)
		return 0;
	if (bind_count > SQL_BIND_PARAMETER_MAX) {
		diag_set(ClientError, ER_SQL_BIND_PARAMETER_MAX,
			 (int) bind_count);
		return -1;
	}
	struct region *region = &fiber()->gc;
	uint32_t used = region_used(region);
	size_t size = sizeof(struct sql_bind) * bind_count;
	/*
	 * Memory allocated here will be freed in
	 * sql_finalize() or in txn_commit()/txn_rollback() if
	 * there is an active transaction.
	 */
	struct sql_bind *bind = (struct sql_bind *) region_alloc(region, size);
	if (bind == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "bind");
		return -1;
	}
	for (uint32_t i = 0; i < bind_count; ++i) {
		if (lua_sql_bind_decode(L, &bind[i], idx, i) != 0) {
			region_truncate(region, used);
			return -1;
		}
	}
	*out_bind = bind;
	return bind_count;
}

static int
lbox_execute(struct lua_State *L)
{
	struct sql_bind *bind = NULL;
	int bind_count = 0;
	size_t length;
	struct port port;
	int top = lua_gettop(L);

	if ((top != 1 && top != 2) || ! lua_isstring(L, 1))
		return luaL_error(L, "Usage: box.execute(sqlstring[, params])");

	const char *sql = lua_tolstring(L, 1, &length);

	if (top == 2) {
		if (! lua_istable(L, 2))
			return luaL_error(L, "Second argument must be a table");
		bind_count = lua_sql_bind_list_decode(L, &bind, 2);
		if (bind_count < 0)
			return luaT_error(L);
	}

	if (sql_prepare_and_execute(sql, length, bind, bind_count, &port,
				    &fiber()->gc) != 0)
		return luaT_error(L);
	port_dump_lua(&port, L, false);
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
