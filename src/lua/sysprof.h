#ifndef TARANTOOL_LUA_SYSPROF_H_INCLUDED
#define TARANTOOL_LUA_SYSPROF_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */
struct lua_State;
/**
 * Initialize misc.sysprof module.
 */
void
tarantool_lua_sysprof_init(void);

#if defined(__cplusplus)
}
#endif

#endif /* TARANTOOL_LUA_SYSPROF_H_INCLUDED */
