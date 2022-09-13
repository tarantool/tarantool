## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following issues have been resolved:

* Fix handling of errors during trace snapshot restore.

## feature/luajit
Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following features is completed:

* `assert()` now accepts any type of error object (from Lua 5.3).
