## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9595). The following
issues were fixed as part of this activity:

* Fixed recording of `select()` in case with negative first argument.
