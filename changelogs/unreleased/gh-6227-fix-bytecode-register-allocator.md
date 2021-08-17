## bugfix/luajit

* Fixed the order VM registers are allocated by LuaJIT frontend in case of
  `BC_ISGE` and `BC_ISGT` (gh-6227).
