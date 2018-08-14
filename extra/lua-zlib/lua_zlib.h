#ifndef LUA_ZLIB_H
#define LUA_ZLIB_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#include <lua.h>

LUALIB_API int
luaopen_zlib(lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* LUA_ZLIB_H */
