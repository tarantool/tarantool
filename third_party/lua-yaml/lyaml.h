#ifndef LYAML_H
#define LYAML_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lua.h>

LUALIB_API int
luaopen_yaml(lua_State *L);

#ifdef __cplusplus
}
#endif
#endif /* #ifndef LYAML_H */
