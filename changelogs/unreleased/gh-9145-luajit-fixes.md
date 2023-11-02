## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed `math.ceil(x)` result sign for -1 < x < 0.5.
