#ifndef TARANTOOL_FIBER_IFC_LUA_H_INCLUDED
#define TARANTOOL_FIBER_IFC_LUA_H_INCLUDED

struct lua_State;
void fiber_ifc_lua_init(struct lua_State *L);
int lbox_fiber_semaphore(struct lua_State *L);
int lbox_fiber_mutex(struct lua_State *L);
int lbox_fiber_channel(struct lua_State *L);


#endif /* TARANTOOL_FIBER_IFC_LUA_H_INCLUDED */
