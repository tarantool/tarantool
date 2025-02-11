## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-11055). The following
issues were fixed as part of this activity:

* Fixed compiler warning in `setfenv()` / `getfenv()` with negative levels as
  the argument.
* Fixed register allocation for stores into sunk values (gh-10746).
* Fixed a crash when using a Lua C function as a vmevent handler for trace
  events.
* Fixed the compilation of `...` in `select()`.
