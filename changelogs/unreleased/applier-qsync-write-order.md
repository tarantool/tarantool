## bugfix/replication

* Fix the bug when a synchronous transaction could be confirmed and visible on
  a replica, but then not confirmed / invisible again after restart. Could
  happen more likely on memtx spaces with `memtx_use_mvcc_engine` enabled
  (gh-5213).
