## bugfix/lua

* Made `box.stat.memtx` callable. `box.stat.memtx()` now returns all memtx
  statistics while `box.stat.memtx.tx()` is equivalent to `box.stat.memtx().tx`
  (gh-8448).
