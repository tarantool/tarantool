/*
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

#include <tarantool.h>

#include <stddef.h>

extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <mysql.h>

/**
 * gets MYSQL connector from lua stack (or object)
 */
static MYSQL *
lua_check_mysql(struct lua_State *L, int index)
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

	MYSQL *mysql = *(MYSQL **)lua_touserdata(L, index);
	if (pop)
		lua_pop(L, pop);
	return mysql;
}


/**
 * connect to MySQL server
 */
static ssize_t
connect_mysql(va_list ap)
{
	MYSQL *mysql		= va_arg(ap, MYSQL*);
	const char *host	= va_arg(ap, const char*);
	const char *port	= va_arg(ap, const char*);
	const char *user	= va_arg(ap, const char*);
	const char *password	= va_arg(ap, const char*);
	const char *db		= va_arg(ap, const char*);


	int iport = 0;
	const char *usocket = 0;

	if (strcmp(host, "unix/") == 0) {
		usocket = port;
		host = NULL;
	} else {
		iport = atoi(port);
		if (iport == 0)
			iport = 3306;
	}

	mysql_real_connect(mysql, host, user, password, db, iport, usocket,
		CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS);
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
	const char *res;
	if (lua_isnil(L, -1))
		res = NULL;
	else
		res = lua_tostring(L, -1);
	lua_pop(L, 1);
	return res;
}


/**
 * execute request
 */
static ssize_t
exec_mysql(va_list ap)
{
	MYSQL *mysql = va_arg(ap, MYSQL*);
	const char *sql = va_arg(ap, const char*);
	size_t len = va_arg(ap, size_t);

	int res = mysql_real_query(mysql, sql, len);
	if (res == 0) {
		return 0;
	}
	return -2;
}

/**
 * fetch one result from socket
 */
static ssize_t
fetch_result(va_list ap)
{
	MYSQL *mysql = va_arg(ap, MYSQL*);
	MYSQL_RES **result = va_arg(ap, MYSQL_RES**);
	int resno = va_arg(ap, int);
	if (resno) {
		if (mysql_next_result(mysql) > 0)
			return -2;
	}
	*result = mysql_store_result(mysql);
	return 0;
}

/**
 * push results to lua stack
 */
static int
lua_mysql_pushresult(struct lua_State *L, MYSQL *mysql,
	MYSQL_RES *result, int resno)
{
	int tidx;

	if (resno > 0) {
		/* previous push is in already on stack */
		tidx = lua_gettop(L) - 1;
	} else {
		lua_newtable(L);
		tidx = lua_gettop(L);
		lua_pushnumber(L, 0);
	}

	if (!result) {
		if (mysql_field_count(mysql) == 0) {
			double v = lua_tonumber(L, -1);
			v += mysql_affected_rows(mysql);
			lua_pop(L, 1);
			lua_pushnumber(L, v);
			return 2;
		}
		mysql_free_result(result);
		luaL_error(L, "%s", mysql_error(mysql));
	}


	MYSQL_ROW row;
	MYSQL_FIELD * fields = mysql_fetch_fields(result);
	while((row = mysql_fetch_row(result))) {
		lua_pushnumber(L, luaL_getn(L, tidx) + 1);
		lua_newtable(L);

		unsigned long *len = mysql_fetch_lengths(result);

		for (int i = 0; i < mysql_num_fields(result); i++) {
			lua_pushstring(L, fields[i].name);


			switch(fields[i].type) {
				case MYSQL_TYPE_TINY:
				case MYSQL_TYPE_SHORT:
				case MYSQL_TYPE_LONG:
				case MYSQL_TYPE_FLOAT:
				case MYSQL_TYPE_INT24:
				case MYSQL_TYPE_DOUBLE: {
					lua_pushlstring(L, row[i], len[i]);
					double v = lua_tonumber(L, -1);
					lua_pop(L, 1);
					lua_pushnumber(L, v);
					break;
				}

				case MYSQL_TYPE_NULL:
					lua_pushnil(L);
					break;


				case MYSQL_TYPE_LONGLONG:
				case MYSQL_TYPE_TIMESTAMP: {
					long long v = atoll(row[i]);
					luaL_pushuint64(L, v);
					break;
				}

				/* AS string */
				case MYSQL_TYPE_NEWDECIMAL:
				case MYSQL_TYPE_DECIMAL:
				default:
					lua_pushlstring(L, row[i], len[i]);
					break;

			}
			lua_settable(L, -3);
		}


		lua_settable(L, tidx);
	}

	/* sum(affected_rows) */
	double v = lua_tonumber(L, -1);
	v += mysql_affected_rows(mysql);
	lua_pop(L, 1);
	lua_pushnumber(L, v);
	mysql_free_result(result);
	return 2;
}

/**
 * mysql:execute() method
 */
int
lua_mysql_execute(struct lua_State *L)
{
	MYSQL *mysql = lua_check_mysql(L, 1);
	size_t len;
	const char *sql = lua_tolstring(L, 2, &len);

	luaL_Buffer b;
	luaL_buffinit(L, &b);
	int idx = 3;

	char *q = NULL;
	/* process placeholders */
	for (size_t i = 0; i < len; i++) {
		if (sql[i] != '?') {
			luaL_addchar(&b, sql[i]);
			continue;
		}

		if (lua_gettop(L) < idx) {
			free(q);
			luaL_error(L,
				"Can't find value for %d placeholder", idx);
		}

		if (lua_isboolean(L, idx)) {
			int v = lua_toboolean(L, idx++);
			luaL_addstring(&b, v ? "TRUE" : "FALSE");
			continue;
		}

		if (lua_isnil(L, idx)) {
			idx++;
			luaL_addstring(&b, "NULL");
			continue;
		}

		if (lua_isnumber(L, idx)) {
			const char *s = lua_tostring(L, idx++);
			luaL_addstring(&b, s);
			continue;
		}

		size_t l;
		const char *s = lua_tolstring(L, idx++, &l);
		char *nq = (char *)realloc(q, l * 2 + 1);
		if (!nq) {
			free(q);
			luaL_error(L, "Can't allocate memory for variable");
		}
		q = nq;
		l = mysql_real_escape_string(mysql, q, s, l);
		luaL_addchar(&b, '\'');
		luaL_addlstring(&b, q, l);
		luaL_addchar(&b, '\'');
	}
	free(q);

	luaL_pushresult(&b);

	sql = lua_tolstring(L, -1, &len);


	int res = coio_call(exec_mysql, mysql, sql, len);
	lua_pop(L, 1);
	if (res == -1)
		luaL_error(L, "%s", strerror(errno));

	if (res != 0)
		luaL_error(L, "%s", mysql_error(mysql));

	int resno = 0;
	do {
		MYSQL_RES *result = NULL;
		res = coio_call(fetch_result, mysql, &result, resno);
		if (res == -1)
			luaL_error(L, "%s", strerror(errno));

		lua_mysql_pushresult(L, mysql, result, resno++);

	} while(mysql_more_results(mysql));

	return 2;
}


/**
 * collect MYSQL object
 */
int
lua_mysql_gc(struct lua_State *L)
{
	MYSQL *mysql = lua_check_mysql(L, 1);
	mysql_close(mysql);
	return 0;
}

/**
 * quote variable
 */
int
lua_mysql_quote(struct lua_State *L)
{
	MYSQL *mysql = lua_check_mysql(L, 1);
	if (lua_gettop(L) < 2) {
		lua_pushnil(L);
		return 1;
	}

	size_t len;
	const char *s = lua_tolstring(L, -1, &len);
	char *sout = (char*)malloc(len * 2 + 1);
	if (!sout) {
		luaL_error(L, "Can't allocate memory for variable");
	}

	len = mysql_real_escape_string(mysql, sout, s, len);
	lua_pushlstring(L, sout, len);
	free(sout);
	return 1;
}

/**
 * connect to MYSQL server (lua binding)
 */
int
lbox_net_mysql_connect(struct lua_State *L)
{
	MYSQL *mysql = mysql_init(NULL);
	if (!mysql)
		luaL_error(L, "Can not allocate memory for connector");

	const char *host = self_field(L, "host", 1);
	const char *port = self_field(L, "port", 1);
	const char *user = self_field(L, "user", 1);
	const char *pass = self_field(L, "password", 1);
	const char *db   = self_field(L, "db", 1);


	if (!host || (!port) || (!user) || (!pass) || (!db)) {
		luaL_error(L,
			 "Usage: box.net.sql.connect"
			 "('mysql', host, port, user, password, db, ...)"
		);
	}


	if (coio_call(connect_mysql, mysql, host, port, user, pass, db) == -1) {
		mysql_close(mysql);
		luaL_error(L, "%s", strerror(errno));
	}

	if (*mysql_error(mysql)) {
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		luaL_addstring(&b, mysql_error(mysql));
		luaL_pushresult(&b);
		mysql_close(mysql);
		lua_error(L);
	}

	lua_pushstring(L, "raw");
	MYSQL **ptr = (MYSQL **)lua_newuserdata(L, sizeof(mysql));
	*ptr = mysql;

	lua_newtable(L);
	lua_pushstring(L, "__index");

	lua_newtable(L);

	static const struct luaL_reg meta [] = {
		{"execute", lua_mysql_execute},
		{"quote",   lua_mysql_quote},
		{NULL, NULL}
	};
	luaL_register(L, NULL, meta);
	lua_settable(L, -3);

	lua_pushstring(L, "__gc");
	lua_pushcfunction(L, lua_mysql_gc);
	lua_settable(L, -3);


	lua_setmetatable(L, -2);
	lua_rawset(L, 1);

	/* return self */
	lua_pushvalue(L, 1);
	return 1;
}

extern "C" {
	int LUA_API luaopen_box_net_mysql(lua_State*);
}

int LUA_API luaopen_box_net_mysql(lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");	/* stack: box */

	lua_pushstring(L, "net");
	lua_rawget(L, -2);		/* stack: box.net */

	lua_pushstring(L, "sql");
	lua_rawget(L, -2);		/* stack: box.net.sql */

	lua_pushstring(L, "connectors");
	lua_rawget(L, -2);		/* stack: box.net.sql.connectors */

	/* stack: box, box.net.sql.connectors */
	lua_pushstring(L, "mysql");
	lua_pushcfunction(L, lbox_net_mysql_connect);
	lua_rawset(L, -3);

	/* cleanup stack */
	lua_pop(L, 4);
	return 0;
}
