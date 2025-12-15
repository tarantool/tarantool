#include <lua.h>
#include <lauxlib.h>

int allocate_string(lua_State *L) {
    lua_pushstring(L, "test string");
    return 1;
}

static const struct luaL_Reg resgnuhash [] = {
    {"allocate_string", allocate_string},
    {NULL, NULL}
};

int luaopen_resgnuhash(lua_State *L) {
    luaL_register(L, "resgnuhash", resgnuhash);
    return 1;
}
