## bugfix/lua

* The `box.stat.memtx` function is now callable, it returns
  all memtx statistics. The `box.stat.memtx.tx()` function
  is now equivalent to the `box.stat.memtx().tx` function (gh-8448).
