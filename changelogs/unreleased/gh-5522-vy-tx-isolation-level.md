## feature/vinyl

* Added support of transaction isolation levels for the Vinyl engine.
  The `txn_isolation` option passed to `box.begin()` now has the same
  effect for Vinyl and memtx (gh-5522).
