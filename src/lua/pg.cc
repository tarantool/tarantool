extern "C" {
	#include <postgres.h>
	#include <libpq-fe.h>
	#include <catalog/pg_type.h>
	#undef PACKAGE_VERSION
}


#include "lua_pg.h"
extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <coeio.h>
#include <tarantool_ev.h>


#include <lua/init.h>
#include <say.h>
#include <guard.h>


static PGconn *
lua_check_pgconn(struct lua_State *L, int index)
{
	int pop = 0;
	if (lua_istable(L, index)) {
		if (index < 0)
			index--;
		lua_pushstring(L, "raw");
		lua_rawget(L, index);

		pop = 1;
		index = -1;
	}

	if (!lua_isuserdata(L, index))
		luaL_error(L, "Can't extract userdata from lua-stack");

	PGconn *conn = *(PGconn **)lua_touserdata(L, index);
	if (pop)
		lua_pop(L, pop);
	return conn;
}


/** do execute request (is run in the other thread) */
static ssize_t
pg_exec(va_list ap)
{
	PGconn *conn			= va_arg(ap, typeof(conn));
	const char *sql			= va_arg(ap, typeof(sql));
	int count			= va_arg(ap, typeof(count));
	Oid *paramTypes			= va_arg(ap, typeof(paramTypes));
	const char **paramValues	= va_arg(ap, typeof(paramValues));
	const int *paramLengths		= va_arg(ap, typeof(paramLengths));
	const int *paramFormats		= va_arg(ap, typeof(paramFormats));
	PGresult **res			= va_arg(ap, typeof(res));

	*res = PQexecParams(conn, sql,
		count, paramTypes, paramValues, paramLengths, paramFormats, 0);
	return 0;
}


/** push query result into lua stack */
static int
lua_push_pgres(struct lua_State *L, PGresult *r)
{
	if (!r)
		luaL_error(L, "PG internal error: zero rults");

	switch(PQresultStatus(r)) {
		case PGRES_COMMAND_OK:
			lua_newtable(L);
			if (*PQcmdTuples(r) == 0) {
				lua_pushnumber(L, 0);
			} else {
				lua_pushstring(L, PQcmdTuples(r));
				double v = lua_tonumber(L, -1);
				lua_pop(L, 1);
				lua_pushnumber(L, v);
			}
			lua_pushstring(L, PQcmdStatus(r));
			return 3;

		case PGRES_TUPLES_OK:
			break;

		case PGRES_BAD_RESPONSE:
			luaL_error(L, "Broken postgresql response");

		case PGRES_FATAL_ERROR:
		case PGRES_NONFATAL_ERROR:
		case PGRES_EMPTY_QUERY:
			luaL_error(L, "%s", PQresultErrorMessage(r));

		default:
			luaL_error(L, "box.net.sql.pg: internal error");
	}

	lua_newtable(L);
	int count = PQntuples(r);
	int cols = PQnfields(r);
	for (int i = 0; i < count; i++) {
		lua_pushnumber(L, i + 1);
		lua_newtable(L);

			for (int j = 0; j < cols; j++) {
				if (PQgetisnull(r, i, j))
					continue;

				lua_pushstring(L, PQfname(r, j));
				const char *s = PQgetvalue(r, i, j);
				int len = PQgetlength(r, i, j);

				switch (PQftype(r, j)) {
					case INT2OID:
					case INT4OID:
					case INT8OID:
					case NUMERICOID: {
						lua_pushlstring(L, s, len);
						double v = lua_tonumber(L, -1);
						lua_pop(L, 1);
						lua_pushnumber(L, v);
						break;
					}
					case BOOLOID:
						if (*s == 't' || *s == 'T')
							lua_pushboolean(L, 1);
						else
							lua_pushboolean(L, 0);
						break;

					default:
						lua_pushlstring(L, s, len);
						break;

				}


				lua_settable(L, -3);
			}

		lua_settable(L, -3);
	}

	if (*PQcmdTuples(r) == 0) {
		lua_pushnumber(L, 0);
	} else {
		lua_pushstring(L, PQcmdTuples(r));
		double v = lua_tonumber(L, -1);
		lua_pop(L, 1);
		lua_pushnumber(L, v);
	}
	lua_pushstring(L, PQcmdStatus(r));
	return 3;
}


/** execute method */
static int
lua_pg_execute(struct lua_State *L)
{
	PGconn *conn = lua_check_pgconn(L, 1);
	const char *sql = lua_tostring(L, 2);

	int count = lua_gettop(L) - 2;
	const char **paramValues = NULL;
	int  *paramLengths = NULL;
	int  *paramFormats = NULL;
	Oid *paramTypes = NULL;

	if (count > 0) {
		paramValues = (typeof(paramValues))
			alloca( count * sizeof(*paramValues) );
		paramLengths = (typeof(paramLengths))
			alloca( count * sizeof(*paramLengths) );
		paramFormats = (typeof(paramFormats))
			alloca( count * sizeof(*paramFormats) );
		paramTypes = (typeof(paramTypes))
			alloca( count * sizeof(*paramTypes) );

		for(int i = 0, idx = 3; i < count; i++, idx++) {
			if (lua_isnil(L, idx)) {
				paramValues[i] = NULL;
				paramLengths[i] = 0;
				paramFormats[i] = 0;
				paramTypes[i] = 0;
				continue;
			}

			if (lua_isboolean(L, idx)) {
				int v = lua_toboolean(L, idx);
				static const char pg_true[] = "t";
				static const char pg_false[] = "f";
				paramValues[i] = v ? pg_true : pg_false;
				paramLengths[i] = 1;
				paramFormats[i] = 0;
				paramTypes[i] = BOOLOID;
				continue;
			}

			size_t len;
			const char *s = lua_tolstring(L, idx, &len);

			if (lua_isnumber(L, idx)) {
				paramTypes[i] = NUMERICOID;
				paramValues[i] = s;
				paramLengths[i] = len;
				paramFormats[i] = 0;
				continue;
			}


			paramValues[i] = s;
			paramLengths[i] = len;
			paramFormats[i] = 0;
			paramTypes[i] = TEXTOID;

		}

		/* transform sql placeholders */
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		char num[10];
		for (int i = 0, j = 1; sql[i]; i++) {
			if (sql[i] != '?') {
				luaL_addchar(&b, sql[i]);
				continue;
			}
			luaL_addchar(&b, '$');

			snprintf(num, 10, "%d", j++);
			luaL_addstring(&b, num);
		}
		luaL_pushresult(&b);
		sql = lua_tostring(L, -1);
	}

	PGresult *res = NULL;
	if (coeio_custom(pg_exec, TIMEOUT_INFINITY, conn,
			sql, count, paramTypes, paramValues,
			paramLengths, paramFormats, &res) == -1) {

		luaL_error(L, "Can't execute sql: %s",
			strerror(errno));
	}

	GUARD([&]{ PQclear(res); });
	return lua_push_pgres(L, res);
}

/**
 * collect connection
 */
static int
lua_pg_gc(struct lua_State *L)
{
	PGconn *conn = lua_check_pgconn(L, 1);
	PQfinish(conn);
	return 0;
}

/**
 * prints warnings from Postgresql into tarantool log
 */
static void
pg_notice(void *arg, const char *message)
{
	say_info("Postgresql: %s", message);
	(void)arg;
}

/**
 * do connect to postgresql (is run in the other thread)
 */
static ssize_t
pg_connect(va_list ap)
{
	const char *constr = va_arg(ap, typeof(constr));
	PGconn **conn = va_arg(ap, typeof(conn));
	*conn = PQconnectdb(constr);
	if (*conn)
		PQsetNoticeProcessor(*conn, pg_notice, NULL);
	return 0;
}

/**
 * returns self.field as C-string
 */
static const char *
self_field(struct lua_State *L, const char *name, int index)
{
	lua_pushstring(L, name);
	if (index < 0)
		index--;
	lua_rawget(L, index);
	const char *res = lua_tostring(L, -1);
	lua_pop(L, 1);
	return res;
}


/**
 * quote variable
 */
static int
lua_pg_quote(struct lua_State *L)
{
	if (lua_gettop(L) < 2) {
		lua_pushnil(L);
		return 1;
	}
	PGconn *conn = lua_check_pgconn(L, 1);
	size_t len;
	const char *s = lua_tolstring(L, -1, &len);

	s = PQescapeLiteral(conn, s, len);

	if (!s)
		luaL_error(L, "Can't allocate memory");
	lua_pushstring(L, s);
	free((void *)s);
	return 1;
}


/**
 * quote identifier
 */
static int
lua_pg_quote_ident(struct lua_State *L)
{
	if (lua_gettop(L) < 2) {
		lua_pushnil(L);
		return 1;
	}
	PGconn *conn = lua_check_pgconn(L, 1);
	size_t len;
	const char *s = lua_tolstring(L, -1, &len);

	s = PQescapeIdentifier(conn, s, len);

	if (!s)
		luaL_error(L, "Can't allocate memory");
	lua_pushstring(L, s);
	free((void *)s);
	return 1;
}


/**
 * connect to postgresql
 */
int
lbox_net_pg_connect(struct lua_State *L)
{
	PGconn     *conn = NULL;

	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addstring(&b, "host='");
	luaL_addstring(&b, self_field(L, "host", 1));

	luaL_addstring(&b, "' port='");
	luaL_addstring(&b, self_field(L, "port", 1));

	luaL_addstring(&b, "' user='");
	luaL_addstring(&b, self_field(L, "user", 1));

	luaL_addstring(&b, "' password='");
	luaL_addstring(&b, self_field(L, "password", 1));

	luaL_addstring(&b, "' dbname='");
	luaL_addstring(&b, self_field(L, "db", 1));

	luaL_addchar(&b, '\'');
	luaL_pushresult(&b);

	const char *constr = lua_tostring(L, -1);

	if (coeio_custom(pg_connect, TIMEOUT_INFINITY, constr, &conn) == -1) {
		luaL_error(L, "Can't connect to postgresql: %s",
			strerror(errno));
	}

	/* cleanup stack */
	lua_pop(L, 1);

	if (PQstatus(conn) != CONNECTION_OK) {
		char *msg = (typeof(msg))
			alloca(strlen(PQerrorMessage(conn)) + 1);
		strcpy(msg, PQerrorMessage(conn));
		PQfinish(conn);
		luaL_error(L, constr);
	}

	lua_pushstring(L, "raw");
	PGconn **ptr = (PGconn **)lua_newuserdata(L, sizeof(conn));
	*ptr = conn;

	lua_newtable(L);
	lua_pushstring(L, "__index");

	lua_newtable(L);

	static const struct luaL_reg meta [] = {
		{"execute",	lua_pg_execute},
		{"quote",	lua_pg_quote},
		{"quote_ident", lua_pg_quote_ident},
		{NULL, NULL}
	};
	luaL_register(L, NULL, meta);
	lua_settable(L, -3);

	lua_pushstring(L, "__gc");
	lua_pushcfunction(L, lua_pg_gc);
	lua_settable(L, -3);


	lua_setmetatable(L, -2);
	lua_rawset(L, 1);

	/* return self */
	lua_pushvalue(L, 1);
	return 1;
}
