#ifndef __BOX_LUA_UUID_H__
#define __BOX_LUA_UUID_H__

struct lua_State;

int lbox_uuid(struct lua_State *L);
int lbox_uuid_hex(struct lua_State *L);

#endif /* __BOX_LUA_UUID_H__ */
