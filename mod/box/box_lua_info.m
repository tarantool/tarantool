#include "box_lua_info.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <tarantool_lua.h>
#include <tarantool.h>
#include <say.h>
#include <string.h>
#include <recovery.h>


static int lbox_info_index_get_recovery_lag(struct lua_State *L) {
	if (recovery_state->remote)
		lua_pushnumber(L, recovery_state->remote->recovery_lag);
	else
		lua_pushnil(L);
	return 1;
}

static int
lbox_info_index_get_recovery_last_update_tstamp(struct lua_State *L) {
	if (recovery_state->remote)
		lua_pushnumber(L,
			recovery_state->remote->recovery_last_update_tstamp);
	else
		lua_pushnil(L);
	return 1;
}

static int lbox_info_get_lsn(struct lua_State *L) {
	luaL_pushnumber64(L, recovery_state->confirmed_lsn);
	return 1;
}


static int lbox_info_get_status(struct lua_State *L) {
	lua_pushstring(L, mod_status());
	return 1;
}

static int lbox_info_uptime(struct lua_State *L) {
	lua_pushnumber(L, (unsigned)tarantool_uptime() + 1); 
	return 1;
}

static const struct luaL_reg lbox_info_dynamic_meta [] = {
	{"recovery_lag", lbox_info_index_get_recovery_lag},
	{"recovery_last_update",
		lbox_info_index_get_recovery_last_update_tstamp},
	{"lsn", lbox_info_get_lsn},
	{"status", lbox_info_get_status},
	{"uptime", lbox_info_uptime},
	{NULL, NULL}
};

static int lbox_info_index(struct lua_State *L) {

	const char *key = lua_tolstring(L, -1, NULL);
	unsigned i;

	for (i = 0; lbox_info_dynamic_meta[i].name; i++) {
		if (strcmp(key, lbox_info_dynamic_meta[i].name) == 0) {
			return lbox_info_dynamic_meta[i].func(L);
		}

	}

	lua_pushnil(L);
	return 1;
}


static const struct luaL_reg lbox_info_meta [] = {
	{"__index", lbox_info_index},
	{NULL, NULL}
};

void lbox_info_init(struct lua_State *L) {

	lua_getfield(L, LUA_GLOBALSINDEX, "box");

	lua_pushstring(L, "info");
	lua_newtable(L);

	lua_newtable(L);
	luaL_register(L, NULL, lbox_info_meta);
	lua_setmetatable(L, -2);


	/* tarantool version */
	lua_pushstring(L, "version");
	lua_pushstring(L, tarantool_version());
	lua_settable(L, -3);

	/* pid */
	lua_pushstring(L, "pid");
	lua_pushnumber(L, getpid());
	lua_settable(L, -3);

	/* logger_pid */
	lua_pushstring(L, "logger_pid");
	lua_pushnumber(L, logger_pid);
	lua_settable(L, -3);

	/* config */
	lua_pushstring(L, "config");
	lua_pushstring(L, cfg_filename_fullpath);
	lua_settable(L, -3);


	/* build */
	lua_pushstring(L, "build");
	lua_newtable(L);

	/* box.info.build.target */
	lua_pushstring(L, "target");
	lua_pushstring(L, BUILD_INFO);
	lua_settable(L, -3);

	/* box.info.build.options */
	lua_pushstring(L, "options");
	lua_pushstring(L, BUILD_OPTIONS);
	lua_settable(L, -3);

	/* box.info.build.compiler */
	lua_pushstring(L, "compiler");
	lua_pushstring(L, COMPILER_INFO);
	lua_settable(L, -3);

	/* box.info.build.flags */
	lua_pushstring(L, "flags");
	lua_pushstring(L, COMPILER_CFLAGS);
	lua_settable(L, -3);

	lua_settable(L, -3);    /* box.info.build */

	lua_settable(L, -3);    /* box.info = created table */
	lua_pop(L, 1);          /* cleanup stack */
}
