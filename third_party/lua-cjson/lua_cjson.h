#ifndef LUA_CJSON_H
#define LUA_CJSON_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#include <lua.h>

LUALIB_API  int
luaopen_json(lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* LUA_CJSON_H */
