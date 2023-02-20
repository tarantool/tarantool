## bugfix/lua

* Fixed a bug that prevented using C module API methods `luaL_iscallable()`,
  `luaL_checkcdata()`, and `luaL_setcdatagc()` with the upvalue indexes
  (gh-8249).
