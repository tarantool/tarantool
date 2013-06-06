#include "lua_sql.h"
extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}
#include <string.h>

#include <config.h>

#if USE_PSQL_CLIENT
	#include "lua_pg.h"
#endif

#if USE_MYSQL_CLIENT
	#include "lua_mysql.h"
#endif

int
lbox_net_sql_do_connect(struct lua_State *L)
{
	lua_pushstring(L, "driver");
	lua_rawget(L, -2);

	const char *drv = lua_tostring(L, -1);
	lua_pop(L, 1);


	if (strcmp(drv, "pg") == 0 || strcmp(drv, "postgresql") == 0)
		#if USE_PSQL_CLIENT
			return lbox_net_pg_connect(L);
		#else
			luaL_error(L,
				"Tarantool was not compiled with postgresql. "
				"Use cmake with '-DENABLE_PSQL=ON' option."
			);
		#endif

	if (strcmp(drv, "mysql") == 0)
		#if USE_MYSQL_CLIENT
			return lbox_net_mysql_connect(L);
		#else
			luaL_error(L,
				"Tarantool was not compiled with mysqlclient. "
				"Use cmake with '-DENABLE_MYSQL=ON' option."
			);
		#endif

	luaL_error(L, "Unknown driver '%s'", drv);
	return 0;
}


void
tarantool_lua_sql_init(struct lua_State *L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "box");	/* stack: box */

	lua_pushstring(L, "net");			/* stack: box.net */
	lua_rawget(L, -2);

	lua_pushstring(L, "sql");			/* stack: box.net.sql */
	lua_rawget(L, -2);

	/* stack: box, box.net.sql */
	lua_pushstring(L, "do_connect");
	lua_pushcfunction(L, lbox_net_sql_do_connect);
	lua_rawset(L, -3);

	/* cleanup stack */
	lua_pop(L, 3);
}
