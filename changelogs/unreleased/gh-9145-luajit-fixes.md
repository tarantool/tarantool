## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9145). The following issues
were fixed as part of this activity:

* Fixed `math.min()`/`math.max()` inconsistencies for x86/x86_64 architectures
  when called with a NaN argument or `-0` and `0`.
