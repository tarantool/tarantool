## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed incorrect fold rule for `x - (-0)` on trace (for `x = -0` the result
  should be `0`).
