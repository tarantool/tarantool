#include "lua_mysql.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <coeio.h>
#include <tarantool_ev.h>


#include <lua/init.h>
#include <say.h>
#include <mysql.h>


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

	MYSQL *mysql = *(void **)lua_touserdata(L, index);
	if (pop)
		lua_pop(L, pop);
	return mysql;
}

static ssize_t
connect_mysql(va_list ap)
{
	MYSQL *mysql		= va_arg(ap, typeof(mysql));
	const char *host	= va_arg(ap, typeof(host));
	const char *port	= va_arg(ap, typeof(port));
	const char *user	= va_arg(ap, typeof(user));
	const char *password	= va_arg(ap, typeof(password));
	const char *db		= va_arg(ap, typeof(db));


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

	say_info("host=%s user=%s password=%s db=%s iport=%d usocket=%s",
		host, user, password, db, iport, usocket);

	mysql_real_connect(mysql, host, user, password, db, iport, usocket,
		CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS);
	return 0;
}


/** returns self.field as C-string */
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


static ssize_t
exec_mysql(va_list ap)
{
	MYSQL *mysql = va_arg(ap, typeof(mysql));
	const char *sql = va_arg(ap, typeof(sql));
	size_t len = va_arg(ap, typeof(len));

	int res = mysql_real_query(mysql, sql, len);
	if (res == 0) {
		return 0;
	}
	return -2;
}

static ssize_t
fetch_result(va_list ap)
{
	MYSQL *mysql = va_arg(ap, typeof(mysql));
	MYSQL_RES **result = va_arg(ap, typeof(result));
	int resno = va_arg(ap, typeof(resno));
	if (resno) {
		if (mysql_next_result(mysql) > 0)
			return -2;
	}
	*result = mysql_store_result(mysql);
	return 0;
}

int
lua_push_mysql_result(struct lua_State *L, MYSQL *mysql,
	MYSQL_RES *result, int resno)
{
	if (!result) {
		if (mysql_field_count(mysql) == 0) {
			lua_newtable(L);
			lua_pushnumber(L, mysql_affected_rows(mysql));
			return 2;
		}
		luaL_error(L, "%s", mysql_error(mysql));
	}

	int tidx;

	if (resno > 0) {
		tidx = lua_gettop(L) - 1;
	} else {
		lua_newtable(L);
		tidx = lua_gettop(L);
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
					/* hack: lua num64 doesn't work propery
						with cjson */
					long long v = atoll(row[i]);
					if (v < (1LL << 31))
						lua_pushnumber(L, v);
					else
						luaL_pushnumber64(L, v);
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

	if (resno > 0) {
		double v = lua_tonumber(L, -1);
		v += mysql_affected_rows(mysql);
		lua_pop(L, 1);
		lua_pushnumber(L, v);
	} else {
		lua_pushnumber(L, mysql_affected_rows(mysql));
	}
	return 2;
}

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
		char *nq = realloc(q, l * 2 + 1);
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


	int res = coeio_custom(exec_mysql, TIMEOUT_INFINITY, mysql, sql, len);
	if (res == -1)
		luaL_error(L, "%s", strerror(errno));

	if (res != 0)
		luaL_error(L, "%s", mysql_error(mysql));

	int resno = 0;
	do {
		MYSQL_RES *result = NULL;
		res = coeio_custom(fetch_result, TIMEOUT_INFINITY,
			mysql, &result, resno);
		if (res == -1)
			luaL_error(L, "%s", strerror(errno));

		lua_push_mysql_result(L, mysql, result, resno++);
		mysql_free_result(result);

	} while(mysql_more_results(mysql));

	return 2;
}


int
lua_mysql_gc(struct lua_State *L)
{
	MYSQL *mysql = lua_check_mysql(L, 1);
	mysql_close(mysql);
	return 0;
}

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


	if (coeio_custom(connect_mysql, TIMEOUT_INFINITY, mysql,
					host, port, user, pass, db) == -1) {
		mysql_close(mysql);
		luaL_error(L, "%s", strerror(errno));
	}

	if (*mysql_error(mysql)) {
		const char *estr = mysql_error(mysql);
		char *b = alloca(strlen(estr) + 1);
		strcpy(b, estr);
		mysql_close(mysql);
		luaL_error(L, "%s", estr);
	}

	lua_pushstring(L, "raw");
	void **ptr = lua_newuserdata(L, sizeof(mysql));
	*ptr = mysql;

	lua_newtable(L);
	lua_pushstring(L, "__index");

	lua_newtable(L);

	static const struct luaL_reg meta [] = {
		{"execute", lua_mysql_execute},
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
