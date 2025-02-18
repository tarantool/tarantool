## bugfix/memtx

* Fixed a bug when Tarantool with memtx MVCC enabled was aborted on
  workload with many `index:get()` operations reading nothing (gh-11022).
