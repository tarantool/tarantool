## bugfix/lua

* When error is raised during encoding call results, auxiliary lightuserdata
  value is not removed from the main Lua coroutine stack. Prior to the fix it
  leads to undefined behaviour during the next usage of this Lua coroutine
  (gh-4617).
