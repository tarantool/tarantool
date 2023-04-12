## bugfix/core

* Relaxed the tuple format requirements on tuples passed as the page starting
  position to `index:tuple_pos()` or to the `after` option of `index:select`.
  Now, Tarantool validates only the key parts of the index being used and all
  primary indexes (gh-8511).
