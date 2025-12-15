/*
** compile with
** 	gcc -Wall -O2 -I.. -ansi -shared -o lib1.so lib1.c
*/


#include "lua.h"
#include "lauxlib.h"

static int id (lua_State *L) {
  return lua_gettop(L);
}


static const struct luaL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


int luaopen_lib2 (lua_State *L) {
  luaL_register(L, "lib2", funcs);
  lua_pushnumber(L, 0.5);
  lua_setglobal(L, "x");
  return 1;
}


