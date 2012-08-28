#include "box_lua_info.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <tarantool_lua.h>
#include <tarantool.h>
#include <say.h>
#include <string.h>
#include <recovery.h>


static int lbox_info_index(struct lua_State *L) {

    const char *key = lua_tolstring(L, -1, NULL);

    if (strcmp(key, "lsn") == 0) {
        luaL_pushnumber64(L, recovery_state->confirmed_lsn);
        return 1;
    }

    if (strcmp(key, "recovery_lag") == 0) {

        if (recovery_state->remote)
            lua_pushnumber(L, recovery_state->remote->recovery_lag);
        else
            lua_pushnil(L);
        return 1;
    }

    if (strcmp(key, "recovery_last_update") == 0) {

        if (recovery_state->remote)
            lua_pushnumber(L,
                recovery_state->remote->recovery_last_update_tstamp);
        else
            lua_pushnil(L);
        return 1;
    }

    if (strcmp(key, "status") == 0) {
        lua_pushstring(L, mod_status());
        return 1;
    }
    

    return 0;
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


    lua_settable(L, -3);    /* box.info = created table */
    lua_pop(L, 1);          /* cleanup stack */
}
