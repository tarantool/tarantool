#include "sql.h"
#include "box/sql.h"

#include "box/sql/sqlite3.h"
#include "lua/utils.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

struct prep_stmt
{
	sqlite3_stmt *stmt;
};

struct prep_stmt_list
{
	uint8_t         *mem_end;   /* denotes actual size of sql_ctx struct */
	uint32_t         pool_size; /* mem at the end used for aux allocations;
				       pool grows from mem_end
				       towards stmt[] array */
	uint32_t         last_select_stmt_index; /* UINT32_MAX if no selects */
	uint32_t         column_count; /* in last select stmt */
	uint32_t         stmt_count;
	struct prep_stmt stmt[6];  /* overlayed with the mem pool;
				      actual size could be larger or smaller */
	/* uint8_t mem_pool[] */
};

static inline int
prep_stmt_list_needs_free(struct prep_stmt_list *l)
{
	return (uint8_t *)(l + 1) != l->mem_end;
}

/* Release resources and free the list itself, unless it was preallocated
 * (i.e. l points to an automatic variable) */
static void
prep_stmt_list_free(struct prep_stmt_list *l)
{
	if (l == NULL)
		return;
	for (size_t i = 0, n = l->stmt_count; i < n; i++)
		sqlite3_finalize(l->stmt[i].stmt);
	if (prep_stmt_list_needs_free(l))
		free(l);
}

static struct prep_stmt_list *
prep_stmt_list_init(struct prep_stmt_list *prealloc)
{
	prealloc->mem_end = (uint8_t *)(prealloc + 1);
	prealloc->pool_size = 0;
	prealloc->last_select_stmt_index = UINT32_MAX;
	prealloc->column_count = 0;
	prealloc->stmt_count = 0;
	return prealloc;
}

/* Allocate mem from the prep_stmt_list pool.
 * If not enough space is available, reallocates the list.
 * If reallocation is needed but l was preallocated, old mem is left
 * intact and a new memory chunk is allocated. */
static void *
prep_stmt_list_palloc(struct prep_stmt_list **pl,
		      size_t size, size_t alignment)
{
	assert((alignment & (alignment - 1)) == 0); /* 2 ^ k */
	assert(alignment <= __alignof__((*pl)->stmt[0]));

	struct prep_stmt_list *l = *pl;
	uint32_t pool_size = l->pool_size;
	uint32_t pool_size_max = (uint32_t)(
		l->mem_end - (uint8_t *)(l->stmt + l->stmt_count)
	);

	assert(UINT32_MAX - pool_size >= size);
	pool_size += size;

	assert(UINT32_MAX - pool_size >= alignment - 1);
	pool_size += alignment - 1;
	pool_size &= ~(alignment - 1);

	if (pool_size > pool_size_max) {
		size_t prev_size = l->mem_end - (uint8_t *)l;
		size_t size = prev_size;
		while (size < prev_size + (pool_size - pool_size_max)) {
			assert(SIZE_MAX - size >= size);
			size += size;
		}
		if (prep_stmt_list_needs_free(l)) {
			l = realloc(l, size);
			if (l == NULL)
				return NULL;
		} else {
			l = malloc(size);
			if (l == NULL)
				return NULL;
			memcpy(l, *pl, prev_size);
		}
		l->mem_end = (uint8_t *)l + size;
		/* move the pool data */
		memmove((uint8_t *)l + prev_size - l->pool_size,
			l->mem_end - l->pool_size,
			l->pool_size);
		*pl = l;
	}

	l->pool_size = pool_size;
	return l->mem_end - pool_size;
}

/* push new stmt; reallocate memory if needed
 * returns a pointer to the new stmt or NULL if out of memory.
 * If reallocation is needed but l was preallocated, old mem is left
 * intact and a new memory chunk is allocated. */
static struct prep_stmt *
prep_stmt_list_push(struct prep_stmt_list **pl)
{
	struct prep_stmt_list *l;
	/* make sure we don't collide with the pool */
	if (prep_stmt_list_palloc(pl, sizeof(l->stmt[0]), 1
				  ) == NULL)
		return NULL;
	l = *pl;
	l->pool_size -= sizeof(l->stmt[0]);
	return l->stmt + (l->stmt_count++);
}

static void
lua_push_column_names(struct lua_State *L, struct prep_stmt_list *l)
{
	sqlite3_stmt *stmt = l->stmt[l->last_select_stmt_index].stmt;
	int n = l->column_count;
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const char *name = sqlite3_column_name(stmt, i);
		lua_pushstring(L, name == NULL ? "" : name);
		lua_rawseti(L, -2, i+1);
	}
}

static void
lua_push_row(struct lua_State *L, struct prep_stmt_list *l)
{
	sqlite3_stmt *stmt = l->stmt[l->last_select_stmt_index].stmt;
	int column_count = l->column_count;
	char *typestr = (void *)(l->mem_end - column_count);

	lua_createtable(L, column_count, 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_array_metatable_ref);
	lua_setmetatable(L, -2);

	for (int i = 0; i < column_count; i++) {
		int type = sqlite3_column_type(stmt, i);
		switch (type) {
		case SQLITE_INTEGER:
			typestr[i] = 'i';
			lua_pushinteger(L, sqlite3_column_int(stmt, i));
			break;
		case SQLITE_FLOAT:
			typestr[i] = 'f';
			lua_pushnumber(L, sqlite3_column_double(stmt, i));
			break;
		case SQLITE_TEXT: {
			const void *text = sqlite3_column_text(stmt, i);
			typestr[i] = 's';
			lua_pushlstring(L, text,
					sqlite3_column_bytes(stmt, i));
			break;
		}
		case SQLITE_BLOB: {
			const void *blob = sqlite3_column_blob(stmt, i);
			typestr[i] = 'b';
			lua_pushlstring(L, blob,
					sqlite3_column_bytes(stmt, i));
			break;
		}
		case SQLITE_NULL:
			typestr[i] = '-';
			lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_nil_ref);
			break;
		default:
			typestr[i] = '?';
			assert(0);
		}
		lua_rawseti(L, -2, i+1);
	}

	lua_pushlstring(L, typestr, column_count);
	lua_rawseti(L, -2, 0);
}

static int
lua_sql_execute(struct lua_State *L)
{
	int rc;
	sqlite3 *db = sql_get();
	struct prep_stmt_list *l = NULL, stock_l;
	size_t length;
	const char *sql, *sql_end;

	if (db == NULL)
		return luaL_error(L, "not ready");

	sql = lua_tolstring(L, 1, &length);
	if (sql == NULL)
		return luaL_error(L, "usage: box.sql.execute(sqlstring)");

	assert(length <= INT_MAX);
	sql_end = sql + length;

	l = prep_stmt_list_init(&stock_l);
	while (sql != sql_end) {

		struct prep_stmt *ps = prep_stmt_list_push(&l);
		if (ps == NULL)
			goto outofmem;
		rc = sqlite3_prepare_v2(db, sql, (int)(sql_end - sql),
					&ps->stmt, &sql);
		if (rc != SQLITE_OK)
			goto sqlerror;

		if (ps->stmt == NULL) {
			/* only whitespace */
			assert(sql == sql_end);
			l->stmt_count --;
			break;
		}

		int column_count = sqlite3_column_count(ps->stmt);
		if (column_count == 0) {
			while ((rc = sqlite3_step(ps->stmt)) == SQLITE_ROW) { ; }
		} else {
			char *typestr;
			l->column_count = column_count;
			l->last_select_stmt_index = l->stmt_count - 1;

			assert(l->pool_size == 0);
			/* This might possibly call realloc() and ruin *ps.  */
			typestr = prep_stmt_list_palloc(&l, column_count, 1);
			if (typestr == NULL)
				goto outofmem;
			/* Refill *ps.  */
			ps = l->stmt + l->stmt_count - 1;

			lua_settop(L, 1); /* discard any results */

			/* create result table */
			lua_createtable(L, 7, 0);
			lua_pushvalue(L, lua_upvalueindex(1));
			lua_setmetatable(L, -2);
			lua_push_column_names(L, l);
			lua_rawseti(L, -2, 0);

			int row_count = 0;
			while ((rc = sqlite3_step(ps->stmt)) == SQLITE_ROW) {
				lua_push_row(L, l);
				row_count++;
				lua_rawseti(L, -2, row_count);
			}
			l->pool_size = 0;
		}
        if (rc != SQLITE_OK && rc != SQLITE_DONE)
            goto sqlerror;
	}
	prep_stmt_list_free(l);
	return lua_gettop(L) - 1;
sqlerror:
	lua_pushstring(L, sqlite3_errmsg(db));
	prep_stmt_list_free(l);	
	return lua_error(L);
outofmem:
	prep_stmt_list_free(l);
	return luaL_error(L, "out of memory");
}

void
box_lua_sqlite_init(struct lua_State *L)
{
	static const struct luaL_Reg module_funcs [] = {
		{"execute", lua_sql_execute},
		{NULL, NULL}
	};

	/* used by lua_sql_execute via upvalue */
	lua_createtable(L, 0, 1);
	lua_pushstring(L, "sequence");
	lua_setfield(L, -2, "__serialize");

	luaL_openlib(L, "box.sql", module_funcs, 1);
	lua_pop(L, 1);
}

