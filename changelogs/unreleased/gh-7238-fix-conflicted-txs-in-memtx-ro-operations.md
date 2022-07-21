## bugfix/memtx

* Fixed the ability to perform read-only operations in conflicting transactions
  in memtx, which led to spurious results (gh-7238).
