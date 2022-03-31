## bugfix/lua

* When an error is raised during encoding call results, the auxiliary
  lightuserdata value is not removed from the main Lua coroutine stack. Prior
  to the fix, it leads to undefined behavior during the next usage of this Lua
  coroutine (gh-4617).
