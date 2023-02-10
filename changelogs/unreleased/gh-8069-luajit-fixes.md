## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-8069). In the scope of this
activity, the following issues have been resolved:

* Fix `pcall()` call without arguments on arm64.
