## bugfix/memtx

* Fixed a bug when getting the index size wasn't recorded in the
  MVCC and so its change didn't lead to the transaction conflict
  (gh-10149).
