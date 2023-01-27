#ifndef LYAML_H
#define LYAML_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lua.h>

struct luaL_serializer;

LUALIB_API int
luaopen_yaml(lua_State *L);

/** @Sa luaL_newserializer(). */
struct luaL_serializer *
lua_yaml_new_serializer(lua_State *L);

/**
 * Encode an object on Lua stack into YAML stream.
 * @param L Lua stack to get an argument and push the result.
 * @param serializer Lua YAML serializer.
 * @param tag_handle NULL, or a global tag handle. For global tags
 *        details see the standard:
 *        http://yaml.org/spec/1.2/spec.html#tag/shorthand/.
 * @param tag_prefix NULL, or a global tag prefix, to which @a
 *        handle is expanded. Example of a tagged document:
 *              handle          prefix
 *               ____   ________________________
 *              /    \ /                        \
 *        '%TAG !push! tag:tarantool.io/push,2018
 *         --- value
 *         ...
 *
 * @retval 2 Pushes two values on error: nil, error description.
 * @retval 1 Pushes one value on success: string with dumped
 *         object.
 */
int
lua_yaml_encode(lua_State *L, struct luaL_serializer *serializer,
		const char *tag_handle, const char *tag_prefix);

#ifdef __cplusplus
}
#endif
#endif /* #ifndef LYAML_H */
