## feature/lua

* Added `box.runtime.info().tuple` metric to track the amount of memory
  occupied by tuples allocated on runtime arena (gh-5872).

  It does not count tuples that arrive from memtx or vinyl but counts tuples
  created on-the-fly: say, using `box.tuple.new(<...>)`.
